#!/usr/bin/env python3
"""Diagnose what happens when Claude Code tries to connect to voiclaude"""

import asyncio
import json
import subprocess
import sys
import time
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def test_with_actual_command():
    """Test connecting using the actual voiclaude command like Claude Code does."""
    print("🔗 Testing connection using actual 'voiclaude' command...")
    print(f"📍 Command path: {subprocess.run(['which', 'voiclaude'], capture_output=True, text=True).stdout.strip()}")
    
    try:
        # This is exactly what Claude Code does
        server_params = StdioServerParameters(
            command="voiclaude",
            args=[],
            env={}
        )
        
        print("🚀 Attempting to connect...")
        
        # Set a timeout to avoid hanging
        async with stdio_client(server_params) as (read, write):
            print("✅ Connection established")
            
            async with ClientSession(read, write) as session:
                print("🔧 Initializing session...")
                
                # Initialize (this is critical)
                result = await session.initialize()
                print(f"✅ Initialization successful: {result}")
                
                # List tools (what Claude Code does to discover slash commands)
                print("📋 Listing tools...")
                tools_result = await session.list_tools()
                
                print(f"🎉 SUCCESS! Found {len(tools_result.tools)} tools:")
                for i, tool in enumerate(tools_result.tools, 1):
                    print(f"   {i}. {tool.name} - {tool.description[:60]}...")
                
                # Test a simple tool call
                print("\n🧪 Testing a tool call...")
                try:
                    call_result = await session.call_tool("healthcheck", {})
                    print("✅ Tool call successful!")
                    if call_result.content:
                        print(f"📝 Response: {call_result.content[0].text[:100]}...")
                except Exception as e:
                    print(f"⚠️ Tool call failed: {e}")
                
                return True
                
    except Exception as e:
        print(f"❌ Connection failed: {e}")
        print(f"Error type: {type(e)}")
        import traceback
        traceback.print_exc()
        return False

async def test_with_debug_output():
    """Test with debug output to see what's happening."""
    print("\n" + "="*60)
    print("🔍 Testing with detailed debug output...")
    
    # Start the server manually to see its output
    print("🚀 Starting voiclaude server manually...")
    
    try:
        proc = subprocess.Popen(
            ['voiclaude', '--log-level', 'DEBUG'], 
            stdout=subprocess.PIPE, 
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Give it a moment to start
        await asyncio.sleep(2)
        
        if proc.poll() is None:
            print("✅ Server is running")
            
            # Kill the process
            proc.terminate()
            await asyncio.sleep(1)
            
            stdout, stderr = proc.communicate()
            print(f"📤 Server stdout: {stdout[:500] if stdout else 'None'}")
            print(f"📤 Server stderr: {stderr[:500] if stderr else 'None'}")
            print(f"📤 Return code: {proc.returncode}")
            
        else:
            print(f"❌ Server exited immediately with code: {proc.returncode}")
            stdout, stderr = proc.communicate() 
            print(f"📤 stdout: {stdout}")
            print(f"📤 stderr: {stderr}")
            
    except Exception as e:
        print(f"❌ Failed to start server manually: {e}")
        return False

if __name__ == "__main__":
    print("🔍 VoiClaude Connection Diagnosis")
    print("=" * 50)
    
    # Test the connection
    success = asyncio.run(test_with_actual_command())
    
    if not success:
        print("\n🔍 Connection failed, trying debug mode...")
        asyncio.run(test_with_debug_output())
    else:
        print("\n🤔 Connection works fine! The issue might be with Claude Code itself.")
        print("💡 Try these solutions:")
        print("   1. Check Claude Code version compatibility")  
        print("   2. Try /mcp restart voiclaude")
        print("   3. Check if other MCP servers work")
        print("   4. Try creating a project-specific MCP config")