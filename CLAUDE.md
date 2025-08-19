# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a macOS CLI application called `whisper-stream-coreml` that provides real-time speech recognition with CoreML acceleration. It's built on top of whisper.cpp and includes custom model management and configuration systems.

## Key Development Commands

### Building and Testing
- `make build` - Full build (configure + compile)
- `make rebuild` - Quick rebuild (skip configure if already done)
- `make clean` - Remove build artifacts  
- `make test` - Test basic functionality (runs --help check)
- `make fresh` - Clean + build

### Dependencies
- `make install-deps` - Install dependencies via Homebrew (SDL2, CMake)
- `make check-deps` - Check if dependencies are installed

### Running and Development
- `make run` - Interactive model selection and execution
- `make run-model MODEL=base.en` - Run with specific model
- `make run-vad` - Run VAD mode (recommended for real-time)
- `make list-models` - Show available models for download

### Configuration Management
- `make config-list` - Show current configuration
- `make config-set KEY=value VALUE=value` - Set configuration
- `make config-get KEY=key` - Get configuration value
- `make config-reset` - Reset to defaults

## Architecture Overview

### Core Components

1. **Main Application** (`whisper-stream-coreml.cpp`)
   - Entry point and command-line parsing
   - Real-time audio processing loop
   - Integration with whisper.cpp for transcription
   - Auto-copy functionality with session management and safety limits

2. **Model Manager** (`model_manager.h/cpp`)
   - Automatic model discovery and download
   - CoreML model management
   - Interactive model selection interface
   - Model registry with metadata (size, URLs, descriptions)

3. **Configuration Manager** (`config_manager.h/cpp`)
   - Multi-layer configuration system:
     - Command-line arguments (highest priority)
     - Environment variables (`WHISPER_*` prefix)
     - Project config file (`.whisper-config.json`)
     - User config file (`~/.whisper-stream-coreml/config.json`)
   - JSON-based configuration persistence
   - Config validation and merging

### Key Features
- **CoreML Acceleration**: Enabled by default on macOS for performance
- **Voice Activity Detection (VAD)**: Use `--step 0` for efficient processing
- **Real-time Streaming**: Configurable step and length parameters
- **Multi-format Output**: Timestamped, plain text, file output
- **Interactive Setup**: Guided model selection and download
- **Auto-copy Functionality**: Automatic clipboard copy of transcription when session ends

### Build System
- **CMake**: Primary build system with CoreML and Metal support
- **Makefile**: High-level automation and development tasks
- **Dependencies**: SDL2 for audio, whisper.cpp as git submodule
- **Platform**: macOS 10.15+ with Apple Silicon optimizations

### Model System
Models are automatically downloaded when needed. The system supports:
- English-only models (tiny.en, base.en, small.en, medium.en, large)
- Multilingual models (tiny, base, small, medium, large-v3)
- CoreML-optimized versions for improved macOS performance

### Configuration Keys
Common configuration options include:
- `model/default_model` - Default model to use
- `coreml/use_coreml` - Enable/disable CoreML acceleration
- `step/step_ms` - Audio processing step size
- `vad/vad_threshold` - Voice activity detection threshold
- `threads` - Number of processing threads
- `language` - Source language code
- `auto_copy_enabled` - Enable/disable automatic clipboard copy
- `auto_copy_max_duration_hours` - Max session duration before skipping auto-copy (default: 2)
- `auto_copy_max_size_bytes` - Max transcription size before skipping auto-copy (default: 1MB)

### Development Notes
- The application integrates whisper.cpp as a submodule located at `../../fixtures/whisper.cpp`
- CoreML support is compiled in by default (`WHISPER_COREML=1`)
- Metal GPU backend is enabled (`GGML_USE_METAL=1`)
- Audio capture uses SDL2 with configurable device selection
- Configuration files use JSON format with optional value types
- Auto-copy uses macOS `pbcopy` command for clipboard integration
- Auto-copy session tracking with unique session IDs and safety limits

### Performance Recommendations
- Use CoreML acceleration (enabled by default)
- VAD mode (`--step 0`) for real-time processing
- `base.en` model for good balance of speed/accuracy
- Match thread count to CPU cores for optimal performance