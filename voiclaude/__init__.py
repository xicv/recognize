"""VoiClaude - Voice MCP Server for Claude Code

A Model Context Protocol (MCP) server that provides local voice interaction
capabilities for Claude Code using the recognize CLI for speech recognition.

Features:
- Local speech recognition with CoreML acceleration
- One-shot transcription with /listen command
- Continuous conversation mode with /conv command  
- Integration with recognize CLI configuration
- No cloud services required
"""

__version__ = "0.1.0"
__author__ = "VoiClaude Team"
__description__ = "Voice MCP Server for Claude Code using recognize CLI"

from .voice_handler import VoiceHandler
from .config import VoiClaudeConfig
from .mcp_server import mcp

__all__ = ["VoiceHandler", "VoiClaudeConfig", "mcp"]