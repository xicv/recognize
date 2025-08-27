#!/usr/bin/env python3
"""Test full MCP connection like Claude Code would do"""

import asyncio
import json
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def test_claude_connection():
    """Test connecting to voiclaude like Claude Code would."""
    print("🔗 Testing MCP connection like Claude Code...")
    
    try:
        # Create server parameters (like Claude Code would)
        server_params = StdioServerParameters(
            command="voiclaude",
            args=[],
            env={}
        )
        
        # Connect to the server
        async with stdio_client(server_params) as (read, write):
            async with ClientSession(read, write) as session:
                # Initialize the session
                await session.initialize()
                
                # List available tools (what Claude Code does)
                tools = await session.list_tools()
                print(f"✅ Connected! Found {len(tools.tools)} tools:")
                
                for tool in tools.tools:
                    print(f"   🔧 {tool.name}: {tool.description[:60]}...")
                
                # Test calling a tool
                print("\n🧪 Testing tool call...")
                result = await session.call_tool("healthcheck", {})
                print(f"✅ Tool call successful! Response type: {type(result)}")
                if hasattr(result, 'content') and result.content:
                    print(f"📋 Response preview: {str(result.content[0])[:100]}...")
                
                return True
                
    except Exception as e:
        print(f"❌ Connection failed: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    print("🎤 VoiClaude Full Connection Test")
    print("=" * 50)
    
    success = asyncio.run(test_claude_connection())
    
    if success:
        print("\n🎉 SUCCESS: Full connection test PASSED!")
        print("🤔 If Claude Code still shows 'Unknown slash command', try:")
        print("   1. Restart Claude Code completely")
        print("   2. Clear Claude Code cache/settings")
        print("   3. Check for Claude Code updates")
    else:
        print("\n💥 FAILED: Connection test failed")
        print("🔧 This explains why Claude Code can't use the commands")