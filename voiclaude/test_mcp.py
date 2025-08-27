#!/usr/bin/env python3
"""Simple MCP server test to debug tool registration"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP

# Configure logging
logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger(__name__)

# Create the MCP server
mcp = FastMCP(
    name="voiclaude-test",
    instructions="Test MCP server for VoiClaude debugging"
)

@mcp.tool()
async def test_tool() -> Dict[str, Any]:
    """Simple test tool to verify MCP registration works."""
    logger.info("Test tool called!")
    return {
        "success": True,
        "message": "Test tool working correctly!"
    }

@mcp.tool()
async def listen() -> Dict[str, Any]:
    """Simplified listen tool for testing."""
    logger.info("Listen tool called!")
    return {
        "success": True, 
        "transcription": "Test transcription",
        "message": "Listen tool working!"
    }

def main():
    """Main entry point for the test MCP server."""
    logger.info("Starting test MCP server")
    
    # Print registered tools for debugging
    logger.info(f"Registered tools: {list(mcp.list_tools())}")
    
    # Run the MCP server
    mcp.run(transport="stdio")

if __name__ == "__main__":
    main()