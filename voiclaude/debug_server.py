#!/usr/bin/env python3
"""Debug MCP server startup issues"""

import sys
import json
import asyncio
from mcp_server import mcp, main

async def test_server():
    """Test server functionality directly."""
    print("🔧 Testing MCP server functionality...")
    
    # Test tool listing
    try:
        tools = await mcp.list_tools()
        print(f"✅ Found {len(tools)} tools:")
        for tool in tools:
            print(f"   - {tool.name}")
    except Exception as e:
        print(f"❌ Error listing tools: {e}")
        return False
    
    # Test calling a simple tool
    try:
        from mcp.types import CallToolRequest
        request = CallToolRequest(
            method="tools/call",
            params={
                "name": "healthcheck",
                "arguments": {}
            }
        )
        
        result = await mcp.call_tool(request.params.name, request.params.arguments or {})
        print(f"✅ Healthcheck tool result: {result.content[0].text[:100]}...")
        return True
        
    except Exception as e:
        print(f"❌ Error calling healthcheck tool: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    print("🎤 VoiClaude MCP Server Debug Test")
    print("=" * 40)
    
    # Run async test
    success = asyncio.run(test_server())
    
    if success:
        print("\n✅ Server functionality test PASSED")
        print("🔥 Issue might be with Claude Code connection, not server code")
    else:
        print("\n❌ Server functionality test FAILED") 
        print("🐛 Found issues in server implementation")
    
    print(f"\n📍 Server executable path: {sys.executable}")
    print(f"📍 VoiClaude command path: voiclaude")
    print(f"📍 Current working directory: {sys.path[0]}")