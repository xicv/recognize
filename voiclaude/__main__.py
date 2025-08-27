#!/usr/bin/env python3
"""
VoiClaude MCP Server Entry Point

This module allows the package to be executed directly:
  python -m voiclaude
  python voiclaude/
"""

from .mcp_server import main

if __name__ == "__main__":
    main()  # main() is synchronous