# whisper-stream-coreml

A macOS CLI for real-time speech recognition with CoreML acceleration, based on whisper.cpp's stream example.

## Features

- Real-time speech transcription from microphone
- CoreML acceleration for improved performance on macOS
- Metal GPU backend support
- Voice Activity Detection (VAD) 
- Configurable audio parameters
- Multiple output formats (timestamped, plain text, file output)

## Requirements

- macOS 10.15 or later
- SDL2 library (`brew install sdl2`)
- CMake (`brew install cmake`)
- Models are downloaded automatically when needed

## Building

```bash
./build.sh
```

This will:
1. Check for required dependencies
2. Configure with CMake including CoreML and Metal support
3. Build the executable
4. Copy to the current directory

## Usage

### Quick Start (Interactive)
```bash
./whisper-stream-coreml
# The CLI will guide you through model selection and download
```

### Direct Model Usage
```bash
./whisper-stream-coreml -m base.en
# Downloads base.en model automatically if not present
```

### List Available Models
```bash
./whisper-stream-coreml --list-models
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
./whisper-stream-coreml --list-models
```

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