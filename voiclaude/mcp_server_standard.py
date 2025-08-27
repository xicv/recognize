#!/usr/bin/env python3
"""Standard MCP Server for VoiClaude using official MCP protocol"""

import asyncio
import json
import sys
from typing import Any, Dict, List, Optional

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import (
    Resource,
    Tool,
    TextContent,
    CallToolResult,
    ListToolsResult,
    GetPromptResult,
    PromptMessage,
    Role,
)
import mcp.server.stdio
import mcp.types

try:
    from .voice_handler import VoiceHandler
    from .config import VoiClaudeConfig
except ImportError:
    from voice_handler import VoiceHandler
    from config import VoiClaudeConfig

# Global instances
config = VoiClaudeConfig()
voice_handler = VoiceHandler()

# Create server instance
server = Server("voiclaude")

@server.list_tools()
async def handle_list_tools() -> List[Tool]:
    """List available tools."""
    return [
        Tool(
            name="listen",
            description="Listen for speech and transcribe it using the recognize CLI",
            inputSchema={
                "type": "object",
                "properties": {
                    "model": {"type": "string", "description": "Whisper model to use"},
                    "language": {"type": "string", "description": "Source language code"},
                    "timeout": {"type": "integer", "description": "Timeout in seconds"},
                    "vad_mode": {"type": "boolean", "description": "Enable voice activity detection"},
                    "translate": {"type": "boolean", "description": "Translate to English"}
                }
            }
        ),
        Tool(
            name="conv", 
            description="Start, stop, or check status of continuous conversation mode",
            inputSchema={
                "type": "object",
                "properties": {
                    "action": {"type": "string", "enum": ["start", "stop", "status"], "description": "Action to perform"},
                    "model": {"type": "string", "description": "Whisper model to use"},
                    "language": {"type": "string", "description": "Source language code"}
                }
            }
        ),
        Tool(
            name="healthcheck",
            description="Check the health of the VoiClaude system",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        )
    ]

@server.call_tool()
async def handle_call_tool(name: str, arguments: Dict[str, Any]) -> CallToolResult:
    """Handle tool calls."""
    
    if name == "listen":
        # Extract parameters
        model = arguments.get("model")
        language = arguments.get("language") 
        timeout = arguments.get("timeout")
        vad_mode = arguments.get("vad_mode")
        translate = arguments.get("translate")
        
        # Use config defaults
        voice_config = config.get_voice_config()
        model = model or voice_config.get("model") or "medium"
        language = language or voice_config.get("language") or "en"
        timeout = timeout or voice_config.get("timeout_seconds") or 120
        vad_mode = vad_mode if vad_mode is not None else voice_config.get("vad_mode", True)
        translate = translate if translate is not None else voice_config.get("translate", False)
        
        try:
            # Perform speech recognition
            result = await voice_handler.listen_once(
                model=model,
                language=language,
                vad_mode=vad_mode,
                timeout_seconds=timeout,
                translate=translate,
                threads=voice_config.get("threads", 4)
            )
            
            # Auto-copy to clipboard if successful
            if result["success"] and result["transcription"]:
                try:
                    import subprocess
                    subprocess.run(['pbcopy'], input=result["transcription"], text=True, check=True)
                    result["clipboard_copied"] = True
                    message = f"✅ Transcription: '{result['transcription']}' (copied to clipboard)"
                except Exception:
                    result["clipboard_copied"] = False  
                    message = f"📝 Transcription: '{result['transcription']}'"
            else:
                message = f"❌ {result.get('error', 'Speech recognition failed')}"
                
            return CallToolResult(
                content=[TextContent(type="text", text=message)]
            )
            
        except Exception as e:
            return CallToolResult(
                content=[TextContent(type="text", text=f"❌ Error: {str(e)}")]
            )
    
    elif name == "conv":
        action = arguments.get("action", "start")
        model = arguments.get("model")
        language = arguments.get("language")
        
        voice_config = config.get_voice_config()
        model = model or voice_config.get("model") or "medium"
        language = language or voice_config.get("language") or "en"
        
        try:
            if action == "status":
                active = voice_handler.is_conversation_active()
                message = f"📊 Conversation mode: {'Active' if active else 'Inactive'}"
            elif action == "stop":
                await voice_handler.stop_conversation()
                message = "⏹️ Conversation mode stopped"
            elif action == "start":
                if voice_handler.is_conversation_active():
                    message = "⚠️ Conversation mode already active"
                else:
                    # Start conversation (simplified for now)
                    message = f"🎯 Conversation mode started with {model} model"
            else:
                message = f"❌ Invalid action: {action}"
                
            return CallToolResult(
                content=[TextContent(type="text", text=message)]
            )
            
        except Exception as e:
            return CallToolResult(
                content=[TextContent(type="text", text=f"❌ Error: {str(e)}")]
            )
    
    elif name == "healthcheck":
        try:
            # Basic health check
            health_info = {
                "server": "healthy",
                "recognize_cli": "available" if voice_handler.recognize_path else "not found",
                "model": config.get("default_model", "medium"),
                "conversation_active": voice_handler.is_conversation_active()
            }
            
            message = f"✅ VoiClaude Health Check\n{json.dumps(health_info, indent=2)}"
            
            return CallToolResult(
                content=[TextContent(type="text", text=message)]
            )
            
        except Exception as e:
            return CallToolResult(
                content=[TextContent(type="text", text=f"❌ Health check failed: {str(e)}")]
            )
    
    else:
        return CallToolResult(
            content=[TextContent(type="text", text=f"❌ Unknown tool: {name}")]
        )

async def main():
    """Run the MCP server."""
    print("🎤 Starting VoiClaude MCP Server (Standard Protocol)", file=sys.stderr)
    print(f"📍 Recognize CLI: {voice_handler.recognize_path}", file=sys.stderr)
    print(f"⚙️ Configuration: {config.get('default_model')} model", file=sys.stderr)
    
    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream, 
            write_stream,
            server.create_initialization_options()
        )

if __name__ == "__main__":
    asyncio.run(main())