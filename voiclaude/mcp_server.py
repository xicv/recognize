#!/usr/bin/env python3
"""VoiClaude MCP Server

A Model Context Protocol server that provides voice interaction capabilities 
for Claude Code using the local `recognize` CLI for speech recognition.

This server provides two main tools:
- /listen: One-shot speech recognition
- /conv: Continuous conversation mode

All processing is done locally using the recognize CLI with CoreML acceleration.
"""

import asyncio
import logging
import sys
import time
from typing import Dict, Any, Optional, List
import json

from mcp.server.fastmcp import FastMCP

try:
    # Try relative imports first (when run as package)
    from .voice_handler import VoiceHandler
    from .config import VoiClaudeConfig
except ImportError:
    # Fall back to direct imports (when run as script)
    from voice_handler import VoiceHandler
    from config import VoiClaudeConfig

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(sys.stderr)  # Important: log to stderr, not stdout
    ]
)
logger = logging.getLogger(__name__)

# Global instances
config = VoiClaudeConfig()
voice_handler = VoiceHandler()

# Create the MCP server
mcp = FastMCP(
    name="voiclaude",
    instructions="Local voice interaction for Claude Code using recognize CLI"
)


@mcp.tool()
async def listen(
    model: str = None,
    language: str = None, 
    timeout: int = None,
    vad_mode: bool = None,
    translate: bool = None
) -> Dict[str, Any]:
    """
    Listen for speech and transcribe it using the local recognize CLI.
    
    This tool captures audio from the microphone and returns the transcribed text.
    It uses voice activity detection by default for optimal real-time performance.
    
    Args:
        model: Whisper model to use (e.g., 'base.en', 'tiny.en', 'small.en'). 
               If not specified, uses the configured default model.
        language: Source language code (e.g., 'en', 'es', 'fr'). 
                 If not specified, uses the configured default.
        timeout: Maximum seconds to wait for speech (default: 30).
        vad_mode: Enable voice activity detection for efficient processing (default: True).
        translate: Translate speech to English if source language is not English.
    
    Returns:
        Dictionary containing:
        - success: Whether the transcription succeeded
        - transcription: The transcribed text
        - error: Error message if transcription failed
        - metadata: Additional information about the recognition process
    """
    logger.info("Starting speech recognition (listen mode)")
    print(f"🎤 VoiClaude listen called: model={model}, timeout={timeout}")
    logger.info(f"🎤 LISTEN TOOL CALLED: model='{model}', timeout={timeout}")
    
    # Use config defaults if not specified
    voice_config = config.get_voice_config()
    model = model or voice_config.get("model") or "medium"  # Use medium as final fallback
    language = language or voice_config.get("language") or "en"
    timeout = timeout or voice_config.get("timeout_seconds") or 120  # Use 120s as final fallback
    vad_mode = vad_mode if vad_mode is not None else (voice_config.get("vad_mode") if voice_config.get("vad_mode") is not None else True)
    translate = translate if translate is not None else (voice_config.get("translate") if voice_config.get("translate") is not None else False)
    
    logger.debug(f"Recognition parameters - model: {model}, language: {language}, "
                f"timeout: {timeout}s, vad_mode: {vad_mode}, translate: {translate}")
    
    try:
        # Perform speech recognition
        result = await voice_handler.listen_once(
            model=model,
            language=language,
            vad_mode=vad_mode,
            timeout_seconds=timeout,
            translate=translate,
            threads=voice_config.get("threads", 4),
            step_ms=voice_config.get("step_ms", 0),
            length_ms=voice_config.get("length_ms", 60000),
            vad_threshold=voice_config.get("vad_threshold", 0.6)
        )
        
        if result["success"] and result["transcription"]:
            logger.info(f"Recognition successful: '{result['transcription'][:100]}...'")
            
            # Auto-copy to clipboard for easy pasting
            try:
                import subprocess
                subprocess.run(['pbcopy'], input=result["transcription"], text=True, check=True)
                result["clipboard_copied"] = True
                result["message"] = f"✅ Transcription copied to clipboard: '{result['transcription']}'"
            except Exception as e:
                logger.warning(f"Failed to copy to clipboard: {e}")
                result["clipboard_copied"] = False
                result["message"] = f"📝 Transcription: '{result['transcription']}'"
        else:
            logger.warning(f"Recognition failed: {result.get('error', 'Unknown error')}")
            
        # Add user-friendly status information to the result
        if "status" in result:
            status = result["status"]
            result["status_message"] = status.get("message", "Processing...")
            result["phase"] = status.get("phase", "unknown")
            result["processing_time"] = result.get("metadata", {}).get("duration_seconds", 0)
            
            # Create a user-friendly status summary
            status_phases = {
                "initializing": "🔧 Initialized speech recognition",
                "loading": "📥 Loaded speech recognition model", 
                "listening": "🎤 Listened for speech input",
                "processing": "🔄 Processed audio and generated transcription",
                "completed": "✅ Successfully transcribed speech",
                "timeout": "⏰ Timed out waiting for speech",
                "no_speech": "🔇 No speech detected",
                "error": "🚫 System error occurred"
            }
            
            result["activity_summary"] = status_phases.get(status.get("phase", "unknown"), "❓ Unknown status")
            
            # Make status more visible in the main response
            if result["success"]:
                result["user_message"] = f"{result['activity_summary']} in {result['processing_time']}s"
            else:
                result["user_message"] = f"{result['activity_summary']}: {status.get('message', 'Unknown error')}"
            
        return result
        
    except Exception as e:
        logger.error(f"Error in listen tool: {e}")
        return {
            "success": False,
            "transcription": "",
            "error": f"Speech recognition failed: {str(e)}",
            "metadata": {"model": model, "language": language}
        }


@mcp.tool()
async def conv(
    model: str = None,
    language: str = None,
    vad_mode: bool = None, 
    translate: bool = None,
    action: str = "start",
    mode: str = None,
    max_duration: int = None
) -> Dict[str, Any]:
    """
    Start, stop, or check status of continuous conversation mode.
    
    In conversation mode, the system continuously listens for speech and transcribes it
    in real-time. This is ideal for natural back-and-forth conversations with Claude.
    
    Args:
        model: Whisper model to use (e.g., 'base.en', 'tiny.en', 'small.en').
               If not specified, uses the configured default model.
        language: Source language code (e.g., 'en', 'es', 'fr').
                 If not specified, uses the configured default.
        vad_mode: Enable voice activity detection for efficient processing (default: True).
        translate: Translate speech to English if source language is not English.
        action: Action to perform - 'start', 'stop', or 'status' (default: 'start').
        mode: Alternative parameter name for action (for compatibility).
    
    Returns:
        For 'start': Begins conversation and returns real-time transcription results.
        For 'stop': Stops active conversation.
        For 'status': Returns current conversation status.
    """
    # Handle both 'mode' and 'action' parameters for user convenience
    if mode is not None:
        action = mode
    
    logger.info(f"🎯 CONV TOOL CALLED: action='{action}', mode='{mode}', model='{model}'")
    
    # Also output to stdout to ensure visibility
    print(f"🎯 VoiClaude conv called: {action}")
    
    logger.info(f"Conversation mode action: {action}")
    
    if action == "status":
        return {
            "success": True,
            "active": voice_handler.is_conversation_active(),
            "action": "status"
        }
    elif action == "stop":
        return await voice_handler.stop_conversation()
    elif action == "start":
        # Use config defaults if not specified
        voice_config = config.get_voice_config()
        model = model or voice_config.get("model") or "medium"  # Use medium as final fallback
        language = language or voice_config.get("language") or "en"
        vad_mode = vad_mode if vad_mode is not None else (voice_config.get("vad_mode") if voice_config.get("vad_mode") is not None else True)
        translate = translate if translate is not None else (voice_config.get("translate") if voice_config.get("translate") is not None else False)
        
        logger.debug(f"Starting conversation - model: {model}, language: {language}, "
                    f"vad_mode: {vad_mode}, translate: {translate}")
        
        # Check if conversation is already active
        if voice_handler.is_conversation_active():
            return {
                "success": False,
                "error": "Conversation mode is already active. Use action='stop' to stop it first.",
                "action": "start"
            }
        
        try:
            # Start conversation and collect initial results
            results = []
            async for event in voice_handler.start_conversation(
                model=model,
                language=language, 
                vad_mode=vad_mode,
                translate=translate,
                threads=voice_config.get("threads", 4)
            ):
                results.append(event)
                
                # For MCP, we need to return results rather than stream them
                # So we'll collect a reasonable amount of initial output
                if event.get("event") == "conversation_started":
                    # Return immediately with status, actual transcription will come via subsequent calls
                    return {
                        "success": True,
                        "action": "start",
                        "message": "Conversation mode started. Use the listen tool or call conv again to get transcription updates.",
                        "active": True,
                        "metadata": event.get("metadata", {})
                    }
                elif len(results) > 5:  # Limit initial results
                    break
            
            return {
                "success": True,
                "action": "start",
                "results": results,
                "active": voice_handler.is_conversation_active()
            }
            
        except Exception as e:
            logger.error(f"Error starting conversation: {e}")
            return {
                "success": False,
                "error": f"Failed to start conversation: {str(e)}",
                "action": "start"
            }
    else:
        return {
            "success": False,
            "error": f"Invalid action: {action}. Use 'start', 'stop', or 'status'.",
            "action": action
        }


@mcp.tool()
async def listen_stop() -> Dict[str, Any]:
    """
    Stop any active listening session.
    
    This tool can be used to manually stop an active /mcp__voiclaude__listen session
    or conversation mode if it's not responding to silence detection.
    
    Returns:
        Dictionary containing the stop operation status.
    """
    logger.info("Manual stop requested")
    
    try:
        # Stop any active conversation
        conv_result = await voice_handler.stop_conversation()
        
        # Also terminate any current process
        if voice_handler.current_process:
            try:
                voice_handler.current_process.terminate()
                await asyncio.sleep(0.1)
                if voice_handler.current_process.poll() is None:
                    voice_handler.current_process.kill()
            except:
                pass
            finally:
                voice_handler.current_process = None
        
        return {
            "success": True,
            "message": "All voice recognition sessions stopped",
            "conversation_was_active": conv_result.get("success", False),
            "action": "stop_all"
        }
        
    except Exception as e:
        logger.error(f"Error stopping listen: {e}")
        return {
            "success": False,
            "error": f"Failed to stop listening: {str(e)}",
            "action": "stop_all"
        }


@mcp.resource("config://voiclaude")
async def get_config() -> str:
    """
    Get the current VoiClaude configuration.
    
    Returns:
        JSON string containing the current configuration settings.
    """
    logger.debug("Retrieving configuration")
    return json.dumps(config.to_dict(), indent=2)


@mcp.resource("models://available") 
async def get_available_models() -> str:
    """
    Get list of available speech recognition models.
    
    Returns:
        JSON string containing available models and their details.
    """
    logger.debug("Retrieving available models")
    try:
        result = await voice_handler.get_available_models()
        return json.dumps(result, indent=2)
    except Exception as e:
        logger.error(f"Error getting available models: {e}")
        return json.dumps({
            "success": False,
            "error": str(e),
            "models": []
        }, indent=2)


@mcp.resource("status://voice")
async def get_voice_status() -> str:
    """
    Get current voice recognition status.
    
    Returns:
        JSON string containing voice system status.
    """
    logger.debug("Retrieving voice status")
    return json.dumps({
        "conversation_active": voice_handler.is_conversation_active(),
        "recognize_cli_path": voice_handler.recognize_path,
        "default_model": config.get("default_model"),
        "vad_mode": config.get("vad_mode"),
        "language": config.get("language")
    }, indent=2)


@mcp.prompt("listen")
async def listen_prompt(model: str = None, language: str = None, timeout: int = None) -> str:
    """
    Listen for speech and transcribe it.
    
    Prompt version of the listen tool for easier Claude Code integration.
    """
    result = await listen(model=model, language=language, timeout=timeout)
    if result["success"]:
        return f"🎤 Transcription: {result['transcription']}"
    else:
        return f"❌ Error: {result.get('error', 'Speech recognition failed')}"


@mcp.prompt("conv") 
async def conv_prompt(action: str = "start", model: str = None, language: str = None) -> str:
    """
    Control conversation mode.
    
    Prompt version of the conv tool for easier Claude Code integration.
    """
    result = await conv(action=action, model=model, language=language)
    if result["success"]:
        if action == "start":
            return f"🎯 Conversation mode started with {model or 'default'} model"
        elif action == "stop":
            return "⏹️ Conversation mode stopped"
        elif action == "status":
            return f"📊 Conversation active: {result.get('active', False)}"
    else:
        return f"❌ Error: {result.get('error', 'Command failed')}"


# Temporarily commented out new tools to test original functionality
# @mcp.tool()
# async def healthcheck() -> Dict[str, Any]:
#     """
#     Check the health of the VoiClaude system.
#     
#     Returns:
#         Dictionary containing system health status and diagnostics.
#     """
#     logger.info("Running health check")
#     
#     health_status = {
#         "success": True,
#         "timestamp": time.time(),
#         "checks": {},
#         "overall_status": "healthy"
#     }
#     
#     # Check recognize CLI availability
#     try:
#         import subprocess
#         result = subprocess.run([voice_handler.recognize_path, "--help"], 
#                               capture_output=True, timeout=5)
#         health_status["checks"]["recognize_cli"] = {
#             "status": "ok" if result.returncode == 0 else "error",
#             "path": voice_handler.recognize_path,
#             "version_check": result.returncode == 0
#         }
#     except Exception as e:
#         health_status["checks"]["recognize_cli"] = {
#             "status": "error",
#             "error": str(e),
#             "path": voice_handler.recognize_path
#         }
#         health_status["success"] = False
#         health_status["overall_status"] = "unhealthy"
#     
#     # Check configuration
#     try:
#         voice_config = config.get_voice_config()
#         health_status["checks"]["configuration"] = {
#             "status": "ok",
#             "model": voice_config.get("model"),
#             "language": voice_config.get("language"),
#             "vad_mode": voice_config.get("vad_mode")
#         }
#     except Exception as e:
#         health_status["checks"]["configuration"] = {
#             "status": "error", 
#             "error": str(e)
#         }
#         health_status["success"] = False
#         health_status["overall_status"] = "unhealthy"
#     
#     # Check conversation status
#     health_status["checks"]["conversation_mode"] = {
#         "status": "ok",
#         "active": voice_handler.is_conversation_active()
#     }
#     
#     # Check available models
#     try:
#         models_result = await voice_handler.get_available_models()
#         health_status["checks"]["models"] = {
#             "status": "ok" if models_result["success"] else "warning",
#             "available_count": len(models_result.get("models", [])),
#             "error": models_result.get("error")
#         }
#     except Exception as e:
#         health_status["checks"]["models"] = {
#             "status": "error",
#             "error": str(e)
#         }
#     
#     return health_status


# @mcp.tool()
# async def debug_info() -> Dict[str, Any]:
#     """
#     Get comprehensive debug information for troubleshooting.
#     
#     Returns:
#         Dictionary containing debug information and system state.
#     """
#     logger.info("Collecting debug information")
#     
#     import os, sys, platform
#     
#     debug_data = {
#         "timestamp": time.time(),
#         "system": {
#             "platform": platform.platform(),
#             "python_version": sys.version,
#             "working_directory": os.getcwd(),
#             "environment_vars": {
#                 k: v for k, v in os.environ.items() 
#                 if k.startswith(('VOICLAUDE_', 'WHISPER_'))
#             }
#         },
#         "voiclaude": {
#             "version": "0.1.0",
#             "recognize_path": voice_handler.recognize_path,
#             "conversation_active": voice_handler.is_conversation_active(),
#             "current_process_active": voice_handler.current_process is not None,
#             "configuration": config.to_dict()
#         },
#         "process_info": {}
#     }
#     
#     # Add process information if available
#     if voice_handler.current_process:
#         try:
#             debug_data["process_info"] = {
#                 "pid": voice_handler.current_process.pid,
#                 "returncode": voice_handler.current_process.returncode,
#                 "poll_result": voice_handler.current_process.poll()
#             }
#         except Exception as e:
#             debug_data["process_info"]["error"] = str(e)
#     
#     return debug_data


def setup_logging():
    """Setup logging configuration."""
    log_level = config.get("log_level", "INFO")
    logging.getLogger().setLevel(getattr(logging, log_level.upper()))
    
    # Silence some noisy loggers
    logging.getLogger("asyncio").setLevel(logging.WARNING)


def parse_args():
    """Parse command line arguments."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="VoiClaude MCP Server - Voice interaction for Claude Code",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  voiclaude                           # Start MCP server
  voiclaude --log-level DEBUG         # Start with debug logging
  voiclaude --model base.en           # Override default model
  
Environment Variables:
  VOICLAUDE_MODEL       Default model to use
  VOICLAUDE_LANGUAGE    Default language
  VOICLAUDE_LOG_LEVEL   Logging level (DEBUG, INFO, WARNING, ERROR)
        """
    )
    
    parser.add_argument(
        "--model", 
        help="Default model to use (overrides config)"
    )
    parser.add_argument(
        "--language",
        help="Default language (overrides config)"
    )
    parser.add_argument(
        "--log-level",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Set logging level"
    )
    parser.add_argument(
        "--config",
        help="Path to configuration file"
    )
    parser.add_argument(
        "--version", 
        action="version", 
        version="VoiClaude 0.1.0"
    )
    
    return parser.parse_args()


def main():
    """Main entry point for the MCP server.""" 
    try:
        # Parse command line arguments
        args = parse_args()
        
        # Apply CLI overrides to config
        if args.model:
            config.update_config({"default_model": args.model})
        if args.language:
            config.update_config({"language": args.language})
        if args.log_level:
            config.update_config({"log_level": args.log_level})
            
        # Setup logging
        setup_logging()
        
        logger.info("Starting VoiClaude MCP Server v0.1.0")
        logger.info(f"Configuration: {config.get('default_model')} model, "
                   f"VAD mode: {config.get('vad_mode')}, "
                   f"Language: {config.get('language')}")
        
        # Verify recognize CLI is available (synchronously)
        import subprocess
        try:
            result = subprocess.run([voice_handler.recognize_path, "--help"], 
                                  capture_output=True, timeout=5)
            if result.returncode == 0:
                logger.info(f"Recognize CLI found at: {voice_handler.recognize_path}")
            else:
                raise Exception("recognize --help failed")
        except Exception as e:
            logger.error(f"Cannot find recognize CLI: {e}")
            logger.error("Please ensure 'recognize' is installed and in your PATH")
            sys.exit(1)
        
        # Run the MCP server
        logger.info("MCP server ready - waiting for connections from Claude Code")
        mcp.run(transport="stdio")  # Synchronous call
        
    except KeyboardInterrupt:
        logger.info("Server interrupted by user")
    except Exception as e:
        logger.error(f"Server error: {e}")
        sys.exit(1)
    finally:
        # Cleanup
        if voice_handler.is_conversation_active():
            # Stop conversation synchronously for cleanup
            try:
                import asyncio
                asyncio.run(voice_handler.stop_conversation())
            except:
                # Force cleanup if async fails
                voice_handler.conversation_active = False
                if voice_handler.current_process:
                    try:
                        voice_handler.current_process.terminate()
                    except:
                        pass
        logger.info("VoiClaude MCP Server stopped")


if __name__ == "__main__":
    main()  # Now synchronous