# recognize

A macOS CLI for real-time speech recognition with CoreML acceleration, based on whisper.cpp's stream example.

## Features

- **Real-time speech transcription** from microphone with low latency
- **CoreML acceleration** for optimal performance on Apple Silicon Macs
- **Metal GPU backend** support for enhanced processing
- **Voice Activity Detection (VAD)** for efficient real-time processing
- **Comprehensive model management** with automatic downloads and storage optimization
- **Multi-format export system** supporting TXT, Markdown, JSON, CSV, SRT, VTT, XML
- **Auto-copy functionality** with automatic clipboard integration
- **Multi-language speech transcription** with bilingual output support (original + English translation)
- **Advanced configuration system** with JSON files, environment variables, and CLI options
- **Professional subtitle generation** in SRT and VTT formats
- **Session metadata tracking** with detailed performance metrics

## Requirements

### CLI Tool
- macOS 10.15 or later
- SDL2 library (`brew install sdl2`)
- CMake (`brew install cmake`)
- Models are downloaded automatically when needed

## Building

### Quick Start (Recommended)
```bash
make install-deps && make build
```

### Alternative Methods
```bash
# Using build script
./build.sh

# Manual build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DWHISPER_COREML=ON -DGGML_USE_METAL=ON
make -j$(nproc)
```

### Available Make Targets
```bash
make help          # Show all available commands

# Build Commands
make build         # Full build (configure + compile)
make rebuild       # Quick rebuild (skip configure)
make clean         # Remove build artifacts
make fresh         # Clean + build

# Dependencies
make check-deps    # Check if dependencies are installed
make install-deps  # Install dependencies via Homebrew

# Run Commands
make run           # Interactive model selection
make run-model MODEL=base.en  # Run with specific model
make run-vad       # Run VAD mode (recommended)
make list-models   # Show available models

# Model Management
make list-downloaded    # Show downloaded models with details
make show-storage       # Show storage usage summary
make cleanup-models     # Remove orphaned model files

# Export Examples  
make run-export-txt     # Transcribe with text export
make run-export-md      # Transcribe with Markdown export
make run-export-json    # Transcribe with JSON export

# Configuration
make config-list       # Show current configuration
make config-set KEY=value VALUE=value  # Set configuration
make config-get KEY=key  # Get configuration
make config-reset       # Reset to defaults

# Installation
make install            # Install system-wide (/usr/local/bin)
make install-user       # Install for current user (~/bin)
make uninstall          # Remove system installation
make package            # Create distribution package

# Development
make test               # Test basic functionality
make stop               # Stop all running dev apps

```


## Usage

### Quick Start (Interactive)
```bash
make run
# The CLI will guide you through model selection and download
```

### Direct Model Usage
```bash
make run-model MODEL=base.en
# Downloads base.en model automatically if not present
```

### List Available Models
```bash
make list-models                    # Show all available models for download
make list-downloaded                # Show downloaded models with details
make show-storage                   # Show storage usage and cleanup suggestions
```

### Model Management
```bash
# Delete specific model
recognize --delete-model base.en

# Delete all downloaded models
recognize --delete-all-models

# Cleanup orphaned files
recognize --cleanup
```

### VAD Mode (recommended)
```bash
recognize -m base.en --step 0 --length 30000 -vth 0.6
```

### Continuous Mode
```bash
recognize -m base.en --step 500 --length 5000
```

### With/Without CoreML
```bash
recognize -m base.en --coreml     # Enable CoreML (default)
recognize -m base.en --no-coreml  # Disable CoreML
```

### Export Transcriptions
```bash
# Export to text file (auto-generated filename)
recognize -m base.en --export --export-format txt

# Export to Markdown with custom filename
recognize -m base.en --export --export-format md --export-file meeting.md

# Export to JSON with confidence scores
recognize -m base.en --export --export-format json --export-include-confidence

# Export to SRT subtitle file
recognize -m base.en --export --export-format srt

# Export with all metadata and timestamps
recognize -m base.en --export --export-format json

# Export without metadata (clean output)
recognize -m base.en --export --export-format txt --export-no-metadata --export-no-timestamps
```

### Supported Export Formats
- **TXT**: Plain text with optional timestamps and metadata
- **Markdown**: Formatted document with tables and styling
- **JSON**: Structured data with segments, metadata, and confidence scores
- **CSV**: Spreadsheet-compatible format with segment timing
- **SRT**: Standard subtitle format for video players
- **VTT**: WebVTT subtitle format for web players
- **XML**: Structured markup with complete session details

## Command Line Options

### Basic Options
- `-h, --help` - Show help message
- `-m, --model` - Model name (e.g., base.en, tiny.en) or file path
- `-l, --language` - Source language (default: en)
- `-t, --threads` - Number of threads (default: 4)
- `--list-models` - List all available models for download

### Model Management Options
- `--list-downloaded` - Show downloaded models with sizes and paths
- `--show-storage` - Show detailed storage usage breakdown
- `--delete-model MODEL` - Delete a specific model
- `--delete-all-models` - Delete all downloaded models
- `--cleanup` - Remove orphaned model files

### Export Options
- `--export` - Enable transcription export when session ends
- `--export-format FORMAT` - Export format: txt, md, json, csv, srt, vtt, xml
- `--export-file FILE` - Export to specific file (default: auto-generated)
- `--export-auto-filename` - Generate automatic filename with timestamp
- `--export-no-metadata` - Exclude session metadata from export
- `--export-no-timestamps` - Exclude timestamps from export
- `--export-include-confidence` - Include confidence scores in export

### Auto-Copy Options
- `--auto-copy` - Automatically copy transcription to clipboard when session ends
- `--auto-copy-max-duration N` - Max session duration in hours before skipping auto-copy
- `--auto-copy-max-size N` - Max transcription size in bytes before skipping auto-copy

### Audio Options
- `-c, --capture` - Audio capture device ID (default: -1 for default)
- `--step` - Audio step size in ms (default: 3000, 0 for VAD mode)
- `--length` - Audio length in ms (default: 10000)
- `--keep` - Audio to keep from previous step in ms (default: 200)

### Processing Options
- `-tr, --translate` - Translate to English
- `-vth, --vad-thold` - VAD threshold (default: 0.6)
- `-fth, --freq-thold` - High-pass frequency cutoff (default: 100.0)
- `-bs, --beam-size` - Beam search size (default: -1)
- `-mt, --max-tokens` - Max tokens per chunk (default: 32)

### CoreML Options
- `--coreml` - Enable CoreML acceleration (default: enabled)
- `--no-coreml` - Disable CoreML acceleration
- `-cm, --coreml-model` - Specific CoreML model path

### Speaker Segmentation Options
- `-tdrz, --tinydiarize` - Enable speaker segmentation (requires tdrz model)
- Speaker segmentation detects when different people are speaking and marks speaker turns
- Requires models with `tdrz` suffix (e.g., `ggml-small.en-tdrz.bin`)
- Currently supports English-only with small.en models
- Output includes `[SPEAKER_TURN]` markers when speakers change

### Output Options
- `-f, --file` - Output transcription to file
- `-om, --output-mode` - Output mode: original, english, bilingual (default: original)
- `-sa, --save-audio` - Save recorded audio to WAV file
- `--no-timestamps` - Disable timestamp output (auto in continuous mode)
- `-ps, --print-special` - Print special tokens

## Configuration Management

The CLI supports a comprehensive configuration system with multiple layers:

### Configuration Sources (in priority order)
1. **Command-line arguments** (highest priority)
2. **Environment variables** 
3. **Project config file** (`.whisper-config.json` or `config.json`)
4. **User config file** (`~/.recognize/config.json`)

### Config Commands
```bash
# Show current configuration (including system paths)
recognize config list

# Set configuration values
recognize config set model base.en
recognize config set threads 8
recognize config set use_coreml true
recognize config set models_dir /custom/path/to/models

# Get configuration values
recognize config get model
recognize config get threads

# Remove configuration values
recognize config unset model

# Reset all configuration to defaults
recognize config reset
```

### Makefile Shortcuts
```bash
# Configuration management via Makefile
make config-list
make config-set KEY=model VALUE=base.en
make config-get KEY=threads
make config-reset
```

### Environment Variables
All configuration options can be set via environment variables with the `WHISPER_` prefix:

```bash
export WHISPER_MODEL=base.en
export WHISPER_MODELS_DIR=/custom/path/to/models
export WHISPER_THREADS=8
export WHISPER_COREML=true
export WHISPER_VAD_THRESHOLD=0.7
export WHISPER_STEP_MS=3000
export WHISPER_LANGUAGE=en
export WHISPER_TINYDIARIZE=true
export WHISPER_AUTO_COPY=true
export WHISPER_AUTO_COPY_MAX_DURATION=2
export WHISPER_AUTO_COPY_MAX_SIZE=1048576
```

### Configuration File Format
Configuration files use JSON format:

```json
{
  "default_model": "base.en",
  "models_directory": "/custom/path/to/models",
  "threads": 8,
  "use_coreml": true,
  "vad_threshold": 0.6,
  "step_ms": 3000,
  "length_ms": 10000,
  "language": "en",
  "translate": false,
  "save_audio": false,
  "tinydiarize": false,
  "auto_copy_enabled": true,
  "auto_copy_max_duration_hours": 2,
  "auto_copy_max_size_bytes": 1048576
}
```

### Available Configuration Keys
- `model` / `default_model` - Default model to use
- `models_dir` / `models_directory` - Directory to store models
- `coreml` / `use_coreml` - Enable/disable CoreML acceleration
- `coreml_model` - Specific CoreML model path
- `capture` / `capture_device` - Audio capture device ID
- `step` / `step_ms` - Audio step size in milliseconds
- `length` / `length_ms` - Audio length in milliseconds  
- `keep` / `keep_ms` - Audio to keep from previous step
- `vad` / `vad_threshold` - Voice activity detection threshold
- `freq` / `freq_threshold` - High-pass frequency cutoff
- `threads` - Number of processing threads
- `tokens` / `max_tokens` - Maximum tokens per chunk
- `beam` / `beam_size` - Beam search size
- `language` / `lang` - Source language
- `translate` - Translate to English
- `timestamps` / `no_timestamps` - Disable timestamps
- `special` / `print_special` - Print special tokens
- `colors` / `print_colors` - Print colors based on token confidence
- `save_audio` - Save recorded audio
- `tinydiarize` / `speaker_segmentation` - Enable speaker segmentation (requires tdrz model)
- `output` / `output_file` - Output file path
- `format` / `output_format` - Output format (json, plain, timestamped)
- `mode` / `output_mode` - Output mode: original, english, bilingual

### Auto-Copy Configuration
- `auto_copy` / `auto_copy_enabled` - Enable/disable automatic clipboard copy when session ends
- `auto_copy_max_duration` / `auto_copy_max_duration_hours` - Maximum session duration (hours) before skipping auto-copy (default: 2)
- `auto_copy_max_size` / `auto_copy_max_size_bytes` - Maximum transcription size (bytes) before skipping auto-copy (default: 1MB)

### Export Configuration
- `export_enabled` - Enable/disable automatic export when session ends (default: false)
- `export_format` - Default export format: txt, md, json, csv, srt, vtt, xml (default: txt)
- `export_auto_filename` - Generate automatic filename with timestamp (default: true)
- `export_include_metadata` - Include session metadata in exports (default: true)
- `export_include_timestamps` - Include timestamps in exports (default: true)
- `export_include_confidence` - Include confidence scores in exports (default: false)

## Multi-Language Speech Transcription

The CLI supports multi-language speech transcription with three output modes for seamless translation workflows:

### Output Modes

- **`original`** - Transcribe in the original spoken language only (default)
- **`english`** - Translate everything to English only
- **`bilingual`** - Show both original language and English translation side by side

### Usage Examples

```bash
# Bilingual Chinese-English transcription
recognize -m medium --output-mode bilingual -l zh

# Japanese to English translation only
recognize -m medium --output-mode english -l ja

# Spanish transcription in original language
recognize -m medium --output-mode original -l es

# Set bilingual as default
recognize config set output_mode bilingual
recognize config set language zh
recognize -m medium  # Uses configured defaults
```

### Output Format Examples

**Bilingual Mode (with timestamps):**
```
[00:01.000 --> 00:02.500]  zh: 你好世界
[00:01.000 --> 00:02.500]  en: Hello World
[00:02.500 --> 00:04.000]  zh: 这是一个测试
[00:02.500 --> 00:04.000]  en: This is a test
```

**Bilingual Mode (plain text):**
```
zh: 你好世界
en: Hello World
zh: 这是一个测试
en: This is a test
```

**English-only Mode:**
```
[00:01.000 --> 00:02.500]  en: Hello World
[00:02.500 --> 00:04.000]  en: This is a test
```

### Requirements for Multi-Language Features

- **Multilingual models required**: Use models without `.en` suffix (e.g., `base`, `medium`, `large-v3`)
- **Source language specification**: Use `-l` or `--language` with appropriate language code (e.g., `zh`, `es`, `fr`, `ja`)
- **Two-pass processing**: Bilingual mode performs both transcription and translation for optimal accuracy

### Supported Languages

All Whisper-supported languages work with the multi-language features:
- Chinese (`zh`), Japanese (`ja`), Korean (`ko`)
- Spanish (`es`), French (`fr`), German (`de`), Italian (`it`)
- Russian (`ru`), Arabic (`ar`), Hindi (`hi`)
- And 90+ more languages

### Performance Considerations

- **Bilingual mode**: Approximately 2x processing time (runs two inference passes)
- **English/Original modes**: Standard processing time (single inference pass)
- **Model recommendations**: `medium` or `large-v3` for best translation quality

## Performance Tips

1. **Use CoreML**: Enabled by default for best performance on Apple Silicon
2. **VAD Mode**: Use `--step 0` for efficient processing with voice detection
3. **Model Selection**: 
   - `base.en` for English-only, good balance of speed/accuracy
   - `tiny.en` for fastest processing
   - `small.en` for better accuracy than tiny
4. **Thread Count**: Use `-t` to match your CPU cores for optimal performance

## Examples

### Interactive Setup (Recommended for First Use)
```bash
recognize
# 1. Shows available models
# 2. Prompts for model selection
# 3. Downloads automatically with progress
# 4. Shows usage examples
```

### Real-time transcription with VAD
```bash
recognize -m base.en --step 0 --length 30000
```

### Continuous transcription every 500ms
```bash
recognize -m base.en --step 500 --length 5000
```

### Save transcription to file
```bash
recognize -m base.en -f transcript.txt
```

### Multi-language transcription with bilingual output
```bash
# Chinese with English translation (side by side)
recognize -m base --output-mode bilingual -l zh

# Spanish to English translation only
recognize -m base --output-mode english -l es

# Traditional translate flag (compatibility)
recognize -m base -l es --translate
```

### Fast processing with tiny model
```bash
recognize -m tiny.en --step 500
```

### Auto-copy transcription results
```bash
# Enable auto-copy with default settings (2 hours max, 1MB max)
recognize -m base.en --auto-copy

# Enable auto-copy with custom limits
recognize -m base.en --auto-copy --auto-copy-max-duration 1 --auto-copy-max-size 500000

# Configure via environment variables
export WHISPER_AUTO_COPY=true
export WHISPER_AUTO_COPY_MAX_DURATION=3
recognize -m base.en

# Configure via config file
recognize config set auto_copy_enabled true
recognize config set auto_copy_max_duration_hours 1
recognize -m base.en
```

### Model Management Examples
```bash
# List downloaded models with details
recognize --list-downloaded

# Show storage usage and get cleanup suggestions
recognize --show-storage

# Delete specific model to free space
recognize --delete-model medium.en

# Clean up orphaned files
recognize --cleanup

# Delete all models (nuclear option)
recognize --delete-all-models
```

### Speaker Segmentation Examples
```bash
# Enable speaker segmentation with tdrz model
recognize -m small.en-tdrz --tinydiarize

# Speaker segmentation with VAD mode for meetings
recognize -m small.en-tdrz --tinydiarize --step 0 --length 30000

# Save speaker-segmented transcription to file
recognize -m small.en-tdrz --tinydiarize -f meeting_transcript.txt

# Configure speaker segmentation as default
recognize config set tinydiarize true
recognize config set model small.en-tdrz
```

### Export Examples
```bash
# Export meeting transcript to Markdown
recognize -m base.en --export --export-format md --export-file meeting_notes.md

# Export with confidence scores for analysis
recognize -m base.en --export --export-format json --export-include-confidence

# Generate SRT subtitles for video
recognize -m base.en --export --export-format srt --export-file video_subtitles.srt

# Quick text export with auto-naming
recognize -m base.en --export --export-format txt

# Clean CSV export for data processing
recognize -m base.en --export --export-format csv --export-no-metadata

# Configure default export settings
recognize config set export_enabled true
recognize config set export_format json
recognize config set export_include_confidence true
recognize -m base.en  # Will automatically export to JSON with confidence scores
```

## Available Models

The CLI automatically downloads models when needed. Available models:

### English-only (Recommended for English speech)
- `tiny.en` (39 MB) - Fastest processing, lower accuracy
- `base.en` (148 MB) - Good balance of speed and accuracy
- `small.en` (488 MB) - Higher accuracy than base
- `medium.en` (1.5 GB) - Very high accuracy, slower
- `large` (3.1 GB) - Highest accuracy, slowest

### Multilingual (99 languages)
- `tiny` (39 MB) - Fastest, 99 languages, lower accuracy
- `base` (148 MB) - Good balance, 99 languages
- `small` (488 MB) - Higher accuracy, 99 languages
- `medium` (1.5 GB) - Very high accuracy, 99 languages
- `large-v3` (3.1 GB) - Highest accuracy, 99 languages

View all available models:
```bash
make list-models
```

## Documentation

- **[TUTORIAL.md](TUTORIAL.md)** - Comprehensive usage guide with examples
- **[README.md](README.md)** - This file (quick reference)
- Run `make help` - Show all Makefile commands


## Troubleshooting

### Build Issues
- Ensure SDL2 is installed: `brew install sdl2`
- Verify CMake version: `cmake --version`
- Clean build: `rm -rf build && ./build.sh`

### Runtime Issues
- Check microphone permissions in System Preferences > Security & Privacy
- Verify model file exists and is not corrupted
- Try different audio devices with `-c` flag
- Adjust VAD threshold with `-vth` if speech detection is poor

### Performance Issues
- Enable CoreML with `--coreml` (should be default)
- Use smaller model (tiny.en vs base.en)
- Adjust thread count with `-t`
- Try VAD mode with `--step 0`