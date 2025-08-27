#!/usr/bin/env python3
"""
Simple launcher for VoiClaude MCP Server
"""

import sys
import asyncio
import os
from pathlib import Path

# Add current directory to Python path so we can import local modules
current_dir = Path(__file__).parent
sys.path.insert(0, str(current_dir))

def main():
    """Main entry point for console script."""
    try:
        from mcp_server import main as mcp_main
        
        print("🎤 Starting VoiClaude MCP Server...")
        print("📍 Voice recognition powered by recognize CLI")
        print("🔗 Model Context Protocol server for Claude Code")
        print("---")
        
        try:
            mcp_main()  # Now synchronous
        except KeyboardInterrupt:
            print("\n👋 VoiClaude server stopped")
        except Exception as e:
            print(f"❌ Error: {e}")
            sys.exit(1)
            
    except ImportError as e:
        print(f"❌ Import error: {e}")
        print("💡 Try running: pip install -e .")
        sys.exit(1)

if __name__ == "__main__":
    main()