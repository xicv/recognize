# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a macOS CLI application called `recognize` that provides real-time speech recognition with CoreML acceleration. It's built on top of whisper.cpp and includes custom model management and configuration systems.

## Key Development Commands

### Building and Testing
- `make build` - Full build (configure + compile)
- `make rebuild` - Quick rebuild (skip configure if already done)
- `make clean` - Remove build artifacts  
- `make test` - Test basic functionality (runs --help check)
- `make fresh` - Clean + build
- `make info` - Show system information (OS, architecture, CPU cores, memory, dependencies)
- `make help` - Show all available Makefile commands

### Dependencies
- `make install-deps` - Install dependencies via Homebrew (SDL2, CMake)
- `make check-deps` - Check if dependencies are installed

### Running and Development
- `make run` - Interactive model selection and execution
- `make run-model MODEL=base.en` - Run with specific model
- `make run-vad` - Run VAD mode (recommended for real-time)
- `make list-models` - Show available models for download
- `make list-downloaded` - Show downloaded models with details
- `make show-storage` - Show storage usage summary  
- `make cleanup-models` - Remove orphaned model files

### Export Examples
- `make run-export-txt` - Transcribe with text export
- `make run-export-md` - Transcribe with Markdown export  
- `make run-export-json` - Transcribe with JSON export

### Configuration Management
- `make config-list` - Show current configuration
- `make config-set KEY=value VALUE=value` - Set configuration
- `make config-get KEY=key` - Get configuration value
- `make config-reset` - Reset to defaults

### Installation and Packaging
- `make install` - Install system-wide to /usr/local/bin (requires sudo)
- `make install-user` - Install for current user to ~/bin (no sudo required)
- `make uninstall` - Remove system installation
- `make package` - Create distribution package (tar.gz)

## Architecture Overview

### Core Components

1. **Main Application** (`recognize.cpp` → builds as `recognize`)
   - Entry point and command-line parsing
   - Real-time audio processing loop
   - Integration with whisper.cpp for transcription
   - Auto-copy functionality with session management and safety limits
   - Export system integration with comprehensive format support

2. **Model Manager** (`model_manager.h/cpp`)
   - Automatic model discovery and download
   - CoreML model management
   - Interactive model selection interface
   - Model registry with metadata (size, URLs, descriptions)
   - Storage usage tracking and cleanup utilities

3. **Configuration Manager** (`config_manager.h/cpp`)
   - Multi-layer configuration system:
     - Command-line arguments (highest priority)
     - Environment variables (`WHISPER_*` prefix)
     - Project config file (`.whisper-config.json` or `config.json`)
     - User config file (`~/.recognize/config.json`)
   - JSON-based configuration persistence
   - Config validation and merging

4. **Export Manager** (`export_manager.h/cpp`)
   - Multi-format transcription export (TXT, Markdown, JSON, CSV, SRT, VTT, XML)
   - Session metadata tracking
   - Flexible output options with timestamps and confidence scores
   - Auto-filename generation and custom file paths

### Key Features
- **CoreML Acceleration**: Enabled by default on macOS for performance
- **Voice Activity Detection (VAD)**: Use `--step 0` for efficient processing
- **Real-time Streaming**: Configurable step and length parameters
- **Multi-format Output**: Timestamped, plain text, file output
- **Interactive Setup**: Guided model selection and download
- **Auto-copy Functionality**: Automatic clipboard copy of transcription when session ends
- **Export System**: Export transcriptions to multiple formats (TXT, Markdown, JSON, CSV, SRT, VTT, XML)

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
- `tinydiarize/speaker_segmentation` - Enable speaker segmentation (requires tdrz model, default: false)
- `auto_copy_enabled` - Enable/disable automatic clipboard copy
- `auto_copy_max_duration_hours` - Max session duration before skipping auto-copy (default: 2)
- `auto_copy_max_size_bytes` - Max transcription size before skipping auto-copy (default: 1MB)
- `export/enabled` - Enable/disable automatic export (default: false)
- `export/format` - Default export format: txt, md, json, csv, srt, vtt, xml (default: txt)
- `export/include_metadata` - Include session metadata in exports (default: true)
- `export/include_timestamps` - Include timestamps in exports (default: true)
- `export/include_confidence` - Include confidence scores in exports (default: false)

### Command Name
The application builds as `recognize` (short, memorable command name) and should be referenced as such in documentation and examples, not as `./recognize` since it's designed to be installed system-wide.

### Model Management Features
The application includes comprehensive model management capabilities:
- **List models**: `recognize --list-models` (all available) or `recognize --list-downloaded` (downloaded only)
- **Storage usage**: `recognize --show-storage` (detailed disk usage breakdown)
- **Delete models**: `recognize --delete-model <name>` (single model) or `recognize --delete-all-models` (all models)
- **Cleanup**: `recognize --cleanup` (remove orphaned files)
- Models are automatically downloaded when first requested and stored in `~/.recognize/models/`

### Export System Features
The application includes a comprehensive export system for transcription data:
- **Multiple formats**: TXT, Markdown, JSON, CSV, SRT, VTT, XML
- **Session metadata**: Model info, timestamps, configuration settings
- **Flexible output**: Auto-generated filenames or custom file paths
- **Content options**: Include/exclude timestamps, confidence scores, metadata
- **Professional formats**: Industry-standard subtitle formats (SRT, VTT) and structured data (JSON, CSV)

#### Export Usage Examples
```bash
# Basic export to text file
recognize --export --export-format txt

# Export to Markdown with custom filename
recognize --export --export-format md --export-file transcript.md

# Export JSON with confidence scores
recognize --export --export-format json --export-include-confidence

# Export SRT subtitle file without metadata
recognize --export --export-format srt --export-no-metadata

# Export CSV with all options
recognize --export --export-format csv --export-include-confidence
```

#### Export Formats
- **TXT**: Plain text with optional timestamps and metadata header
- **Markdown**: Formatted document with tables and styling
- **JSON**: Structured data with segments, metadata, and confidence scores
- **CSV**: Spreadsheet-compatible format with segment data
- **SRT**: Standard subtitle format for video players
- **VTT**: WebVTT subtitle format for web players
- **XML**: Structured markup with full metadata and segment details

### Development Notes
- The application integrates whisper.cpp as a submodule located at `../../fixtures/whisper.cpp`
- CoreML support is compiled in by default (`WHISPER_COREML=1`)
- Metal GPU backend is enabled (`GGML_USE_METAL=1`)
- Audio capture uses SDL2 with configurable device selection
- Configuration files use JSON format with optional value types
- Auto-copy uses macOS `pbcopy` command for clipboard integration
- Auto-copy session tracking with unique session IDs and safety limits
- Multi-language support with bilingual output modes (original, english, bilingual)
- Comprehensive signal handling for graceful shutdown during recording

### Code Structure
- `recognize.cpp` - Main application entry point and audio processing loop
- `model_manager.h/cpp` - Model discovery, download, and management
- `config_manager.h/cpp` - Multi-layer configuration system
- `export_manager.h/cpp` - Multi-format transcription export system
- `whisper_params.h` - Parameter structures and validation
- Uses whisper.cpp examples: `common.cpp`, `common-whisper.cpp`, `common-sdl.cpp`

### Performance Recommendations
- Use CoreML acceleration (enabled by default)
- VAD mode (`--step 0`) for real-time processing
- `base.en` model for good balance of speed/accuracy
- Match thread count to CPU cores for optimal performance

### Development Shortcuts
- `make dev` - Clean, build, and run (equivalent to `make fresh run`)
- `make quick` - Rebuild and run (equivalent to `make rebuild run`)

### Development Workflow
1. **Initial setup**: `make install-deps && make build`
2. **Code changes**: `make rebuild` to skip configure step
3. **Testing**: `make test` for basic functionality check
4. **Debug builds**: Edit CMakeLists.txt to change `CMAKE_BUILD_TYPE` to `Debug`
5. **Model testing**: Use `make run-model MODEL=tiny.en` for faster iteration
6. **Configuration testing**: Use `make config-set` and `make config-get` to test config system

### Environment Variables
All configuration options support environment variables with `WHISPER_` prefix:
- `WHISPER_MODEL` - Default model name
- `WHISPER_THREADS` - Number of processing threads
- `WHISPER_COREML` - Enable/disable CoreML acceleration
- `WHISPER_STEP_MS` - Audio processing step size
- `WHISPER_VAD_THRESHOLD` - Voice activity detection threshold
- `WHISPER_TINYDIARIZE` - Enable speaker segmentation (requires tdrz model)
- `WHISPER_AUTO_COPY` - Enable automatic clipboard copy
- `WHISPER_AUTO_COPY_MAX_DURATION` - Max session duration (hours)
- `WHISPER_AUTO_COPY_MAX_SIZE` - Max transcription size (bytes)
- `WHISPER_EXPORT_ENABLED` - Enable automatic export
- `WHISPER_EXPORT_FORMAT` - Default export format
- `WHISPER_LANGUAGE` - Source language code
- `WHISPER_OUTPUT_MODE` - Output mode (original, english, bilingual)