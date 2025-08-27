#!/usr/bin/env python3
"""Test configuration to verify settings"""

from config import VoiClaudeConfig
import json

def test_config():
    print("🔧 Testing VoiClaude configuration...")
    
    config = VoiClaudeConfig()
    print(f"\n📊 Raw config:")
    print(json.dumps(config.to_dict(), indent=2))
    
    print(f"\n🎤 Voice config:")
    voice_config = config.get_voice_config()
    print(json.dumps(voice_config, indent=2))
    
    print(f"\n📋 Key defaults:")
    print(f"  Model: {config.get('default_model')}")
    print(f"  Timeout: {config.get('timeout_seconds')}")
    print(f"  VAD Mode: {config.get('vad_mode')}")

if __name__ == "__main__":
    test_config()