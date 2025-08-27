"""Configuration for VoiClaude MCP Server

This module handles configuration loading and management for the VoiClaude server.
It integrates with the recognize CLI configuration system.
"""

import os
import json
import logging
from typing import Dict, Any, Optional
from pathlib import Path

logger = logging.getLogger(__name__)


class VoiClaudeConfig:
    """Configuration manager for VoiClaude MCP server."""
    
    def __init__(self):
        """Initialize configuration."""
        self.config = self._load_config()
        
    def _load_config(self) -> Dict[str, Any]:
        """Load configuration from multiple sources."""
        config = self._get_default_config()
        
        # Load from recognize config if available
        recognize_config = self._load_recognize_config()
        if recognize_config:
            config.update(recognize_config)
            
        # Load from environment variables
        env_config = self._load_env_config()
        config.update(env_config)
        
        # Load from voiclaude-specific config file
        voiclaude_config = self._load_voiclaude_config()
        config.update(voiclaude_config)
        
        return config
    
    def _get_default_config(self) -> Dict[str, Any]:
        """Get default configuration values."""
        return {
            # Voice settings
            "default_model": "medium",  # Changed to match your recognize config
            "vad_mode": True,
            "timeout_seconds": 120,  # Increased to 2 minutes for longer speech
            "length_ms": 60000,      # 60 seconds of audio processing
            "step_ms": 0,            # VAD mode (0 = continuous)
            "vad_threshold": 0.6,    # Voice activity detection threshold
            "language": "en",
            "translate": False,
            "threads": 4,
            
            # Conversation settings
            "conversation_timeout": 300,  # 5 minutes max conversation
            "auto_stop_silence": 10,      # Stop after 10 seconds of silence
            
            # Server settings
            "server_name": "voiclaude",
            "log_level": "INFO",
            
            # Feature flags
            "enable_conversation_mode": True,
            "enable_model_switching": True,
            "enable_language_detection": False,
        }
    
    def _load_recognize_config(self) -> Dict[str, Any]:
        """Load configuration from recognize CLI config files."""
        config = {}
        
        # Try user config
        user_config_path = Path.home() / ".recognize" / "config.json"
        if user_config_path.exists():
            try:
                with open(user_config_path, 'r') as f:
                    recognize_config = json.load(f)
                    
                # Map recognize config keys to voiclaude config
                config.update(self._map_recognize_config(recognize_config))
                logger.debug(f"Loaded recognize user config from {user_config_path}")
            except Exception as e:
                logger.warning(f"Failed to load recognize user config: {e}")
                
        # Try project config  
        project_config_paths = [
            ".whisper-config.json",
            "config.json",
            ".recognize-config.json"
        ]
        
        for path in project_config_paths:
            project_path = Path(path)
            if project_path.exists():
                try:
                    with open(project_path, 'r') as f:
                        recognize_config = json.load(f)
                        
                    config.update(self._map_recognize_config(recognize_config))
                    logger.debug(f"Loaded recognize project config from {project_path}")
                    break
                except Exception as e:
                    logger.warning(f"Failed to load recognize project config from {path}: {e}")
                    
        return config
    
    def _map_recognize_config(self, recognize_config: Dict[str, Any]) -> Dict[str, Any]:
        """Map recognize configuration keys to voiclaude configuration."""
        mapping = {
            "default_model": "default_model",
            "model": "default_model", 
            "threads": "threads",
            "language": "language",
            "lang": "language",
            "translate": "translate",
            "vad_threshold": "vad_threshold",
            "step_ms": "step_ms",
            "length_ms": "length_ms",
        }
        
        mapped_config = {}
        for recognize_key, voiclaude_key in mapping.items():
            if recognize_key in recognize_config:
                mapped_config[voiclaude_key] = recognize_config[recognize_key]
                
        return mapped_config
    
    def _load_env_config(self) -> Dict[str, Any]:
        """Load configuration from environment variables."""
        config = {}
        
        # Voiclaude-specific environment variables
        env_mapping = {
            "VOICLAUDE_MODEL": "default_model",
            "VOICLAUDE_LANGUAGE": "language", 
            "VOICLAUDE_THREADS": ("threads", int),
            "VOICLAUDE_TIMEOUT": ("timeout_seconds", int),
            "VOICLAUDE_VAD_MODE": ("vad_mode", lambda x: x.lower() in ('true', '1', 'yes')),
            "VOICLAUDE_TRANSLATE": ("translate", lambda x: x.lower() in ('true', '1', 'yes')),
            "VOICLAUDE_LOG_LEVEL": "log_level",
            "VOICLAUDE_CONVERSATION_TIMEOUT": ("conversation_timeout", int),
        }
        
        # Also support WHISPER_ prefixed variables for compatibility
        whisper_mapping = {
            "WHISPER_MODEL": "default_model",
            "WHISPER_LANGUAGE": "language",
            "WHISPER_THREADS": ("threads", int),
            "WHISPER_TRANSLATE": ("translate", lambda x: x.lower() in ('true', '1', 'yes')),
        }
        
        all_mappings = {**env_mapping, **whisper_mapping}
        
        for env_var, config_key in all_mappings.items():
            value = os.environ.get(env_var)
            if value is not None:
                try:
                    if isinstance(config_key, tuple):
                        key, converter = config_key
                        config[key] = converter(value)
                    else:
                        config[config_key] = value
                except (ValueError, TypeError) as e:
                    logger.warning(f"Invalid value for {env_var}: {value} ({e})")
                    
        return config
    
    def _load_voiclaude_config(self) -> Dict[str, Any]:
        """Load voiclaude-specific configuration file."""
        config = {}
        
        config_paths = [
            Path.home() / ".voiclaude" / "config.json",
            Path("voiclaude-config.json"),
            Path(".voiclaude.json")
        ]
        
        for path in config_paths:
            if path.exists():
                try:
                    with open(path, 'r') as f:
                        config.update(json.load(f))
                    logger.debug(f"Loaded voiclaude config from {path}")
                    break
                except Exception as e:
                    logger.warning(f"Failed to load voiclaude config from {path}: {e}")
                    
        return config
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get a configuration value."""
        return self.config.get(key, default)
    
    def get_voice_config(self) -> Dict[str, Any]:
        """Get voice-related configuration."""
        return {
            "model": self.get("default_model"),
            "vad_mode": self.get("vad_mode"),
            "language": self.get("language"),
            "translate": self.get("translate"),
            "threads": self.get("threads"),
            "timeout_seconds": self.get("timeout_seconds"),
        }
    
    def get_conversation_config(self) -> Dict[str, Any]:
        """Get conversation-related configuration."""
        return {
            "conversation_timeout": self.get("conversation_timeout"),
            "auto_stop_silence": self.get("auto_stop_silence"),
        }
    
    def is_feature_enabled(self, feature: str) -> bool:
        """Check if a feature is enabled."""
        return self.get(feature, False)
    
    def update_config(self, updates: Dict[str, Any]) -> None:
        """Update configuration values."""
        self.config.update(updates)
        
    def save_config(self, path: Optional[Path] = None) -> bool:
        """Save current configuration to file."""
        if path is None:
            path = Path.home() / ".voiclaude" / "config.json"
            
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            with open(path, 'w') as f:
                json.dump(self.config, f, indent=2)
            logger.info(f"Configuration saved to {path}")
            return True
        except Exception as e:
            logger.error(f"Failed to save configuration to {path}: {e}")
            return False
    
    def to_dict(self) -> Dict[str, Any]:
        """Get configuration as dictionary."""
        return self.config.copy()
    
    def __str__(self) -> str:
        """String representation of configuration."""
        return json.dumps(self.config, indent=2)