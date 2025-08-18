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
- Whisper model file (download from https://huggingface.co/ggerganov/whisper.cpp)

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

### Basic Usage
```bash
./whisper-stream-coreml -m path/to/ggml-base.en.bin
```

### With CoreML (default)
```bash
./whisper-stream-coreml -m path/to/model.bin --coreml
```

### Without CoreML
```bash
./whisper-stream-coreml -m path/to/model.bin --no-coreml
```

### VAD Mode (recommended)
```bash
./whisper-stream-coreml -m path/to/model.bin --step 0 --length 30000 -vth 0.6
```

### Continuous Mode
```bash
./whisper-stream-coreml -m path/to/model.bin --step 500 --length 5000
```

## Command Line Options

### Basic Options
- `-h, --help` - Show help message
- `-m, --model` - Path to whisper model file (required)
- `-l, --language` - Source language (default: en)
- `-t, --threads` - Number of threads (default: 4)

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

### Real-time transcription with VAD
```bash
./whisper-stream-coreml -m models/ggml-base.en.bin --step 0 --length 30000
```

### Continuous transcription every 500ms
```bash
./whisper-stream-coreml -m models/ggml-base.en.bin --step 500 --length 5000
```

### Save transcription to file
```bash
./whisper-stream-coreml -m models/ggml-base.en.bin -f transcript.txt
```

### Different language with translation
```bash
./whisper-stream-coreml -m models/ggml-base.bin -l es --translate
```

## Models

Download pre-trained models:
```bash
# English-only models (faster)
curl -L https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin -o models/ggml-base.en.bin
curl -L https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin -o models/ggml-small.en.bin

# Multilingual models
curl -L https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin -o models/ggml-base.bin
curl -L https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin -o models/ggml-small.bin
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