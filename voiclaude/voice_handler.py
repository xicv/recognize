"""Voice Handler for VoiClaude MCP Server

This module handles speech recognition using the local `recognize` CLI.
It provides both one-shot recognition and continuous conversation modes.
"""

import asyncio
import subprocess
import logging
import signal
import os
import sys
import time
from typing import AsyncIterator, Optional, Dict, Any, Callable
from pathlib import Path
from contextlib import asynccontextmanager

logger = logging.getLogger(__name__)


class VoiceHandler:
    """Handles speech recognition using the local recognize CLI."""
    
    def __init__(self):
        """Initialize the voice handler."""
        self.recognize_path = self._find_recognize_cli()
        self.current_process: Optional[subprocess.Popen] = None
        self.conversation_active = False
        self._lock = asyncio.Lock()  # Prevent concurrent operations
        self._session_id = None
        self._last_activity = time.time()
        
    def _find_recognize_cli(self) -> str:
        """Find the recognize CLI executable."""
        # Try current directory first
        local_path = Path(__file__).parent.parent / "recognize"
        if local_path.exists() and local_path.is_file():
            return str(local_path)
            
        # Try system PATH
        try:
            result = subprocess.run(["which", "recognize"], 
                                  capture_output=True, text=True, check=True)
            return result.stdout.strip()
        except subprocess.CalledProcessError:
            pass
            
        # Try common installation locations
        common_paths = [
            "/usr/local/bin/recognize",
            "/opt/homebrew/bin/recognize", 
            "~/bin/recognize",
            "./recognize"
        ]
        
        for path in common_paths:
            expanded_path = Path(path).expanduser()
            if expanded_path.exists() and expanded_path.is_file():
                return str(expanded_path)
                
        raise FileNotFoundError(
            "Cannot find 'recognize' CLI. Please ensure it's installed and in PATH, "
            "or available in the current directory."
        )
    
    def _is_valid_transcription(self, line_text: str) -> bool:
        """Check if a line contains valid transcription content."""
        if not line_text or len(line_text.strip()) < 10:
            return False
            
        # Filter out known status/debug messages
        status_prefixes = [
            '[Start speaking]', '###', 'init:', 'main:', 'whisper_', '✅', 
            'Using', 'Auto-detected', 'model size', 'whisper_model_load:', 
            'whisper_init_', 'whisper_backend_', 'whisper_print_timings:'
        ]
        
        if any(line_text.startswith(prefix) for prefix in status_prefixes):
            return False
            
        # Check for timestamped content
        if '[' in line_text and ']' in line_text:
            if 'Background noise' in line_text or 'silence' in line_text.lower():
                return False
            return len(line_text.strip()) > 20  # Reasonable length for real speech
            
        return False
    
    def _extract_transcription_text(self, line_text: str) -> str:
        """Extract clean transcription text from a timestamped line."""
        if ']' in line_text:
            content = line_text.split(']', 1)[1].strip()
            # Remove common noise patterns
            if content and not content.lower().startswith(('background', 'silence', '(', '[')):
                return content
        return ""
    
    @asynccontextmanager
    async def _managed_process(self, cmd: list, timeout: int = 120):
        """Context manager for subprocess lifecycle management."""
        process = None
        try:
            logger.debug(f"Starting process: {' '.join(cmd)}")
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                env=None  # Inherit environment
            )
            self.current_process = process
            yield process
        except Exception as e:
            logger.error(f"Process error: {e}")
            raise
        finally:
            if process and process.returncode is None:
                try:
                    process.terminate()
                    await asyncio.wait_for(process.wait(), timeout=2)
                except asyncio.TimeoutError:
                    logger.warning("Process didn't terminate gracefully, killing...")
                    process.kill()
                except Exception as e:
                    logger.error(f"Error during process cleanup: {e}")
            self.current_process = None
    
    async def listen_once(self, 
                         model: str = "medium",
                         vad_mode: bool = True,
                         timeout_seconds: int = 120,
                         **kwargs) -> Dict[str, Any]:
        """
        Perform one-shot speech recognition.
        
        Args:
            model: Whisper model to use (default: medium)
            vad_mode: Enable voice activity detection (default: True)  
            timeout_seconds: Maximum time to wait for speech (default: 120s)
            **kwargs: Additional arguments for recognize CLI
            
        Returns:
            Dictionary with transcription result and metadata
        """
        logger.info(f"Starting one-shot recognition with model: {model}")
        
        # Track recognition phases
        status_updates = {
            "phase": "initializing",
            "message": f"Initializing speech recognition with {model} model...",
            "start_time": asyncio.get_event_loop().time()
        }
        
        
        # Build command
        cmd = [self.recognize_path, "-m", model, "--auto-copy"]
        
        # Configure audio processing parameters  
        if vad_mode:
            # Use config values for more flexible control
            step_ms = kwargs.get("step_ms", 0)  # 0 = VAD mode
            length_ms = kwargs.get("length_ms", 60000)  # 60 seconds default
            vad_threshold = kwargs.get("vad_threshold", 0.6)  # VAD sensitivity
            
            cmd.extend(["--step", str(step_ms)])
            cmd.extend(["--length", str(length_ms)]) 
            cmd.extend(["--vad-thold", str(vad_threshold)])
        else:
            # Non-VAD mode: use shorter chunks
            cmd.extend(["--step", "3000", "--length", "10000"])
        
        # Add additional arguments
        for key, value in kwargs.items():
            if key == "language" and value:
                cmd.extend(["-l", value])
            elif key == "threads" and value:
                cmd.extend(["-t", str(value)])
            elif key == "translate" and value:
                cmd.append("--translate")
            # Skip audio processing params (already handled above)
            elif key in ["step_ms", "length_ms", "vad_threshold"]:
                continue
                
        try:
            # Run recognition with timeout
            logger.info(f"Executing command: {' '.join(cmd)}")
            logger.info(f"Using model: {model}, VAD mode: {vad_mode}, timeout: {timeout_seconds}s")
            
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                # Ensure process has access to the same environment
                env=None  # Inherit parent environment completely
            )
            
            self.current_process = process
            
            # Update status: process started
            status_updates["phase"] = "loading"
            status_updates["message"] = "Loading speech recognition model..."
            
            # Read output in real-time instead of waiting for completion
            output_lines = []
            start_time = asyncio.get_event_loop().time()
            listening_started = False
            
            try:
                while True:
                    current_time = asyncio.get_event_loop().time()
                    if current_time - start_time > timeout_seconds:
                        status_updates["phase"] = "timeout"
                        status_updates["message"] = f"⏰ Recognition timed out after {timeout_seconds} seconds (no speech detected)"
                        logger.warning(f"Recognition timed out after {timeout_seconds} seconds")
                        break
                        
                    # Check if process has finished
                    if process.returncode is not None:
                        break
                        
                    # Try to read a line with a short timeout
                    try:
                        line = await asyncio.wait_for(process.stdout.readline(), timeout=1.0)
                        if line:
                            line_text = line.decode('utf-8').strip()
                            logger.debug(f"Got output line: {line_text}")
                            output_lines.append(line_text)
                            
                            # Update status based on output
                            if "[Start speaking]" in line_text and not listening_started:
                                status_updates["phase"] = "listening"
                                status_updates["message"] = "🎤 Listening for speech... (speak now)"
                                listening_started = True
                                logger.info("Status: Ready for speech input")
                            elif "whisper_print_timings" in line_text:
                                status_updates["phase"] = "processing"
                                status_updates["message"] = "🔄 Processing audio and generating transcription..."
                                logger.info("Status: Processing speech")
                            
                            # Check if this looks like a transcription result
                            # Filter out all known status/debug messages and section headers
                            status_prefixes = ['[Start speaking]', '###', 'init:', 'main:', 'whisper_', '✅', 'Using', 'Auto-detected', 'model size', 'whisper_model_load:', 'whisper_init_', 'whisper_backend_', 'whisper_print_timings:']
                            
                            # Check for actual transcription content (lines that look like timestamped text)
                            if line_text and not any(line_text.startswith(prefix) for prefix in status_prefixes) and line_text.strip() != "":
                                # Additional filtering: check if it contains actual speech content (not just timestamps with background noise)
                                if self._is_valid_transcription(line_text):
                                    logger.info(f"Found transcription: {line_text}")
                                    # Extract just the text content after the timestamp
                                    transcription_content = self._extract_transcription_text(line_text)
                                    if transcription_content:
                                        logger.info(f"Extracted transcription content: {transcription_content}")
                                        output_lines.append(transcription_content)  # Store the clean transcription
                                        status_updates["phase"] = "completed"
                                        status_updates["message"] = f"✅ Transcription completed: '{transcription_content[:50]}{'...' if len(transcription_content) > 50 else ''}'"
                                    break
                        else:
                            # No more output
                            break
                    except asyncio.TimeoutError:
                        # No output available, continue waiting
                        continue
                        
            finally:
                if process.returncode is None:
                    process.terminate()
                    try:
                        await asyncio.wait_for(process.wait(), timeout=2)
                    except:
                        process.kill()
                self.current_process = None
            
            # Process results
            transcription = ""
            if output_lines:
                # Find the actual transcription line (not status messages or headers)
                status_prefixes = ['[Start speaking]', '###', 'init:', 'main:', 'whisper_', '✅', 'Using', 'Auto-detected', 'model size', 'whisper_model_load:', 'whisper_init_', 'whisper_backend_', 'whisper_print_timings:']
                
                for line in output_lines:
                    if line and not any(line.startswith(prefix) for prefix in status_prefixes) and len(line.strip()) > 0:
                        # Check if this is a timestamped transcription line
                        if '[' in line and ']' in line and 'Background noise' not in line:
                            # Extract text content after timestamp
                            if ']' in line:
                                transcription_content = line.split(']', 1)[1].strip()
                                if len(transcription_content) > 0:
                                    transcription = transcription_content
                                    break
                        else:
                            # For lines that are already clean content (from our improved real-time processing)
                            transcription = line.strip()
                            break
                        
            # Get any remaining output
            try:
                remaining_stdout, stderr = await asyncio.wait_for(process.communicate(), timeout=1)
                if remaining_stdout:
                    additional_output = remaining_stdout.decode('utf-8').strip()
                    if additional_output and not transcription:
                        transcription = additional_output
            except:
                stderr = b""
                
            error_msg = stderr.decode('utf-8').strip() if stderr else ""
            success = bool(transcription)
            
            # Final status update
            if not success:
                if status_updates.get("phase") == "timeout":
                    # Keep the timeout message
                    pass
                elif error_msg and "No such file or directory" in error_msg:
                    status_updates["phase"] = "error"
                    status_updates["message"] = "🚫 Speech recognition CLI not found"
                else:
                    status_updates["phase"] = "no_speech"
                    status_updates["message"] = "🔇 No speech detected (try speaking louder or closer to microphone)"
            
            # Calculate duration
            total_duration = asyncio.get_event_loop().time() - status_updates["start_time"]
            
            return {
                "success": success,
                "transcription": transcription,
                "error": status_updates.get("message") if not success else None,
                "status": status_updates,
                "metadata": {
                    "model": model,
                    "vad_mode": vad_mode,
                    "return_code": process.returncode,
                    "duration_seconds": round(total_duration, 2),
                    "debug_output": error_msg if error_msg else None  # Keep debug info but separate
                }
            }
            
        except Exception as e:
            logger.error(f"Error during recognition: {e}")
            status_updates["phase"] = "error"
            status_updates["message"] = f"🚫 System error: {str(e)}"
            return {
                "success": False,
                "error": str(e),
                "transcription": "",
                "status": status_updates,
                "metadata": {"model": model, "vad_mode": vad_mode}
            }
    
    async def start_conversation(self,
                               model: str = "medium", 
                               vad_mode: bool = True,
                               **kwargs) -> AsyncIterator[Dict[str, Any]]:
        """
        Start a continuous conversation mode.
        
        Args:
            model: Whisper model to use
            vad_mode: Enable voice activity detection  
            **kwargs: Additional arguments for recognize CLI
            
        Yields:
            Dictionary with transcription results as they come in
        """
        if self.conversation_active:
            yield {
                "success": False,
                "error": "Conversation mode already active",
                "transcription": "",
                "event": "error"
            }
            return
            
        logger.info(f"Starting conversation mode with model: {model}")
        self.conversation_active = True
        
        # Build command for continuous mode
        cmd = [self.recognize_path, "-m", model, "--auto-copy"]
        
        # Configure audio processing for conversation mode
        if vad_mode:
            # For conversation, use longer processing windows
            step_ms = kwargs.get("step_ms", 0)  # VAD mode
            length_ms = kwargs.get("length_ms", 300000)  # 5 minutes for conversations
            vad_threshold = kwargs.get("vad_threshold", 0.6)
            
            cmd.extend(["--step", str(step_ms)])
            cmd.extend(["--length", str(length_ms)]) 
            cmd.extend(["--vad-thold", str(vad_threshold)])
        else:
            cmd.extend(["--step", "3000", "--length", "10000"])
            
        # Add additional arguments
        for key, value in kwargs.items():
            if key == "language" and value:
                cmd.extend(["-l", value])
            elif key == "threads" and value:
                cmd.extend(["-t", str(value)])
            elif key == "translate" and value:
                cmd.append("--translate")
            # Skip audio processing params (already handled above)
            elif key in ["step_ms", "length_ms", "vad_threshold"]:
                continue
        
        try:
            logger.debug(f"Starting conversation with command: {' '.join(cmd)}")
            
            # Start the process
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                stdin=asyncio.subprocess.PIPE
            )
            
            self.current_process = process
            
            # Initial status
            yield {
                "success": True,
                "event": "conversation_started", 
                "transcription": "",
                "metadata": {"model": model, "vad_mode": vad_mode}
            }
            
            # Read output line by line
            while self.conversation_active and process.returncode is None:
                try:
                    line = await asyncio.wait_for(
                        process.stdout.readline(), 
                        timeout=1.0
                    )
                    
                    if not line:
                        break
                        
                    text = line.decode('utf-8').strip()
                    if text:
                        yield {
                            "success": True,
                            "event": "transcription",
                            "transcription": text,
                            "metadata": {"model": model, "timestamp": asyncio.get_event_loop().time()}
                        }
                        
                except asyncio.TimeoutError:
                    # Check if process is still running
                    if process.poll() is not None:
                        break
                    continue
                    
        except Exception as e:
            logger.error(f"Error in conversation mode: {e}")
            yield {
                "success": False,
                "error": str(e),
                "transcription": "",
                "event": "error"
            }
            
        finally:
            # Cleanup
            self.conversation_active = False
            if self.current_process:
                try:
                    self.current_process.terminate()
                    await asyncio.sleep(0.1)  # Give it time to terminate gracefully
                    if self.current_process.poll() is None:
                        self.current_process.kill()
                except:
                    pass
                finally:
                    self.current_process = None
                    
            yield {
                "success": True,
                "event": "conversation_ended",
                "transcription": "",
                "metadata": {"model": model}
            }
    
    async def stop_conversation(self) -> Dict[str, Any]:
        """Stop the current conversation mode."""
        if not self.conversation_active:
            return {
                "success": False,
                "error": "No conversation is currently active",
                "event": "error"
            }
            
        logger.info("Stopping conversation mode")
        self.conversation_active = False
        
        if self.current_process:
            try:
                self.current_process.terminate()
                await asyncio.sleep(0.1)
                if self.current_process.poll() is None:
                    self.current_process.kill()
            except:
                pass
            finally:
                self.current_process = None
                
        return {
            "success": True,
            "event": "conversation_stopped",
            "transcription": ""
        }
    
    def is_conversation_active(self) -> bool:
        """Check if conversation mode is currently active."""
        return self.conversation_active
    
    async def get_available_models(self) -> Dict[str, Any]:
        """Get list of available models from recognize CLI."""
        try:
            cmd = [self.recognize_path, "--list-models"]
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )
            
            stdout, stderr = await process.communicate()
            
            if process.returncode == 0:
                return {
                    "success": True,
                    "models": stdout.decode('utf-8').strip().split('\n'),
                    "error": None
                }
            else:
                return {
                    "success": False,
                    "models": [],
                    "error": stderr.decode('utf-8').strip()
                }
                
        except Exception as e:
            return {
                "success": False,
                "models": [],
                "error": str(e)
            }