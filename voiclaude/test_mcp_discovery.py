#!/usr/bin/env python3
"""Test what Claude Code actually sees from our MCP server"""

import asyncio
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def test_what_claude_sees():
    """Test exactly what Claude Code discovers from voiclaude server."""
    print("🔍 Testing MCP Discovery (Claude Code Perspective)")
    print("=" * 60)
    
    try:
        server_params = StdioServerParameters(
            command="voiclaude",
            args=[],
            env={}
        )
        
        async with stdio_client(server_params) as (read, write):
            async with ClientSession(read, write) as session:
                # Initialize
                init_result = await session.initialize()
                print(f"✅ Server: {init_result.serverInfo.name}")
                print(f"📋 Capabilities: {init_result.capabilities}")
                
                # Test tools discovery (this works)
                print("\\n🔧 TOOLS Discovery:")
                tools = await session.list_tools()
                print(f"Found {len(tools.tools)} tools:")
                for tool in tools.tools:
                    print(f"   - {tool.name}: {tool.description[:40]}...")
                
                # Test prompts discovery (this is the issue!)
                print("\\n🎯 PROMPTS Discovery:")
                try:
                    prompts = await session.list_prompts()
                    print(f"Found {len(prompts.prompts) if hasattr(prompts, 'prompts') else len(prompts)} prompts:")
                    
                    prompt_list = prompts.prompts if hasattr(prompts, 'prompts') else prompts
                    for prompt in prompt_list:
                        server_name = init_result.serverInfo.name
                        expected_command = f"/mcp__{server_name}__{prompt.name}"
                        print(f"   - {prompt.name}: {prompt.description[:40]}...")
                        print(f"     Expected slash command: {expected_command}")
                        print(f"     Arguments: {len(prompt.arguments) if prompt.arguments else 0}")
                        
                        # Test individual prompt
                        try:
                            prompt_result = await session.get_prompt(prompt.name, {})
                            print(f"     ✅ Can get prompt: {type(prompt_result)}")
                        except Exception as e:
                            print(f"     ❌ Cannot get prompt: {e}")
                        
                except Exception as e:
                    print(f"❌ PROMPTS discovery failed: {e}")
                    import traceback
                    traceback.print_exc()
                
                return True
                
    except Exception as e:
        print(f"❌ Connection failed: {e}")
        return False

if __name__ == "__main__":
    asyncio.run(test_what_claude_sees())