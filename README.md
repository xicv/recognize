# whisper-stream-coreml

A macOS CLI for real-time speech recognition with CoreML acceleration, based on whisper.cpp's stream example.

## Features

- Real-time speech transcription from microphone
- CoreML acceleration for improved performance on macOS
- Metal GPU backend support
- Voice Activity Detection (VAD) 
- Configurable audio parameters
- Multiple output formats (timestamped, plain text, file output)
- Comprehensive configuration system with JSON files and environment variables

## Requirements

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
make build         # Build the project
make clean         # Clean build artifacts
make test          # Test functionality
make run           # Interactive model selection
make list-models   # Show available models
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
make list-models
```

### VAD Mode (recommended)
```bash
./whisper-stream-coreml -m base.en --step 0 --length 30000 -vth 0.6
```

### Continuous Mode
```bash
./whisper-stream-coreml -m base.en --step 500 --length 5000
```

### With/Without CoreML
```bash
./whisper-stream-coreml -m base.en --coreml     # Enable CoreML (default)
./whisper-stream-coreml -m base.en --no-coreml  # Disable CoreML
```

## Command Line Options

### Basic Options
- `-h, --help` - Show help message
- `-m, --model` - Model name (e.g., base.en, tiny.en) or file path
- `-l, --language` - Source language (default: en)
- `-t, --threads` - Number of threads (default: 4)
- `--list-models` - List all available models for download

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

### Output Options
- `-f, --file` - Output transcription to file
- `-sa, --save-audio` - Save recorded audio to WAV file
- `--no-timestamps` - Disable timestamp output (auto in continuous mode)
- `-ps, --print-special` - Print special tokens

## Configuration Management

The CLI supports a comprehensive configuration system with multiple layers:

### Configuration Sources (in priority order)
1. **Command-line arguments** (highest priority)
2. **Environment variables** 
3. **Project config file** (`.whisper-config.json` or `config.json`)
4. **User config file** (`~/.whisper-stream-coreml/config.json`)

### Config Commands
```bash
# Show current configuration (including system paths)
./whisper-stream-coreml config list

# Set configuration values
./whisper-stream-coreml config set model base.en
./whisper-stream-coreml config set threads 8
./whisper-stream-coreml config set use_coreml true
./whisper-stream-coreml config set models_dir /custom/path/to/models

# Get configuration values
./whisper-stream-coreml config get model
./whisper-stream-coreml config get threads

# Remove configuration values
./whisper-stream-coreml config unset model

# Reset all configuration to defaults
./whisper-stream-coreml config reset
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
  "save_audio": false
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
- `output` / `output_file` - Output file path
- `format` / `output_format` - Output format (json, plain, timestamped)

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
./whisper-stream-coreml
# 1. Shows available models
# 2. Prompts for model selection
# 3. Downloads automatically with progress
# 4. Shows usage examples
```

### Real-time transcription with VAD
```bash
./whisper-stream-coreml -m base.en --step 0 --length 30000
```

### Continuous transcription every 500ms
```bash
./whisper-stream-coreml -m base.en --step 500 --length 5000
```

### Save transcription to file
```bash
./whisper-stream-coreml -m base.en -f transcript.txt
```

### Different language with translation
```bash
./whisper-stream-coreml -m base -l es --translate
```

### Fast processing with tiny model
```bash
./whisper-stream-coreml -m tiny.en --step 500
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