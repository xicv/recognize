#!/usr/bin/env python3
"""Test MCP prompt discovery exactly like Claude Code does"""

import asyncio
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def test_prompt_discovery():
    """Test prompt discovery like Claude Code."""
    print("🔍 Testing MCP Prompt Discovery (Claude Code Style)")
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
                print(f"✅ Server initialized: {init_result.serverInfo.name}")
                
                # Get server capabilities
                print(f"📋 Server capabilities:")
                caps = init_result.capabilities
                if hasattr(caps, 'prompts'):
                    print(f"   - Prompts: {caps.prompts}")
                if hasattr(caps, 'tools'):
                    print(f"   - Tools: {caps.tools}")
                
                # List prompts (this is what Claude Code uses for slash commands!)
                print("\\n🎯 Discovering prompts...")
                prompts = await session.list_prompts()
                
                print(f"Found {len(prompts.prompts) if hasattr(prompts, 'prompts') else len(prompts)} prompts:")
                
                prompt_list = prompts.prompts if hasattr(prompts, 'prompts') else prompts
                
                for prompt in prompt_list:
                    # This is what Claude Code uses to create slash commands
                    server_name = init_result.serverInfo.name  # "voiclaude"
                    prompt_name = prompt.name  # "listen", "conv", etc.
                    
                    # Apply Claude Code's normalization rules
                    normalized_server = server_name.lower().replace(' ', '_').replace('-', '_')
                    normalized_prompt = prompt_name.lower().replace(' ', '_').replace('-', '_')
                    
                    expected_slash_command = f"/mcp__{normalized_server}__{normalized_prompt}"
                    
                    print(f"   📝 Prompt: {prompt.name}")
                    print(f"      Description: {prompt.description[:50]}...")
                    print(f"      🎯 Expected slash command: {expected_slash_command}")
                    print(f"      Arguments: {len(prompt.arguments) if prompt.arguments else 0}")
                    print()
                
                # Test getting a specific prompt
                print("🧪 Testing prompt retrieval...")
                try:
                    test_prompt = await session.get_prompt("listen", {})
                    print("✅ Prompt retrieval successful!")
                    print(f"📝 Prompt result: {test_prompt}")
                except Exception as e:
                    print(f"❌ Prompt retrieval failed: {e}")
                
                return True
                
    except Exception as e:
        print(f"❌ Prompt discovery failed: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = asyncio.run(test_prompt_discovery())
    
    if success:
        print("\\n🎉 Prompt discovery test PASSED!")
        print("🤔 If Claude Code still doesn't work, the issue might be:")
        print("   1. Claude Code version compatibility")
        print("   2. Claude Code internal caching issue")
        print("   3. MCP protocol version mismatch")
    else:
        print("\\n💥 Prompt discovery test FAILED!")
        print("🔧 This explains why Claude Code can't create slash commands")