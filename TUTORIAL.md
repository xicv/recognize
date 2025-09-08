# recognize Tutorial

A comprehensive guide to using the macOS CLI for real-time speech recognition with CoreML acceleration.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Installation](#installation)
3. [First Run](#first-run)
4. [Model Management](#model-management)
5. [Usage Modes](#usage-modes)
6. [Advanced Configuration](#advanced-configuration)
7. [Troubleshooting](#troubleshooting)
8. [Development](#development)

## Quick Start

### 🚀 5-Minute Setup

```bash
# 1. Install dependencies
make install-deps

# 2. Build the CLI
make build

# 3. Start transcribing (interactive setup)
make run
```

That's it! The CLI will guide you through model selection and start transcribing immediately.

## Installation

### Prerequisites

- macOS 10.15 or later
- Xcode Command Line Tools
- Homebrew (recommended)

### Option 1: Using Makefile (Recommended)

```bash
# Install everything automatically
make install-deps && make build
```

### Option 2: Manual Installation

```bash
# Install dependencies
brew install cmake sdl2

# Build manually
./build.sh
```

### Verify Installation

```bash
make test
# Should show: ✓ Basic functionality test passed
```

## First Run

### Interactive Setup (Recommended for Beginners)

```bash
make run
```

**What happens:**
1. Shows available models with sizes and descriptions
2. Prompts you to choose a model
3. Downloads automatically with progress bars
4. Downloads CoreML acceleration (macOS only)
5. Shows usage examples
6. Starts real-time transcription

**Example interaction:**
```
🤖 Available Whisper Models:

📱 English-only models (recommended for English speech):
  tiny.en - Tiny English model (39 MB) - Fastest processing, lower accuracy [⬇️  Available]
  base.en - Base English model (148 MB) - Good balance of speed and accuracy [⬇️  Available]
  ...

Which model would you like to use? base.en

📦 Model 'base.en' not found locally.
📄 Base English model (148 MB) - Good balance of speed and accuracy
📁 Size: 148 MB
🚀 CoreML acceleration: Available

Would you like to download it? [Y/n]: y

🚀 Starting download...
⬇️  Downloading: ggml-base.en.bin
[████████████████████████████████████████] 100%
✅ Download completed

🤖 Downloading CoreML acceleration model...
[████████████████████████████████████████] 100%
✅ CoreML model extracted successfully

🎉 Setup complete! Here's how to use your model:
...
[Start speaking]
```

### Quick Start with Known Model

```bash
# Use base.en model (most popular)
make run-model MODEL=base.en

# Use tiny model for fastest processing
make run-model MODEL=tiny.en

# Use large model for best accuracy
make run-model MODEL=large
```

## Model Management

### Available Models

Use this command to see all models:
```bash
make list-models
```

#### English-only Models (Recommended for English)
- `tiny.en` (39 MB) - Fastest, real-time on any device
- `base.en` (148 MB) - **Most popular** - good speed/accuracy balance
- `small.en` (488 MB) - Higher accuracy, still fast
- `medium.en` (1.5 GB) - Professional quality
- `large` (3.1 GB) - Best possible accuracy

#### Multilingual Models (99 Languages)
- `tiny` (39 MB) - 99 languages, fastest
- `base` (148 MB) - 99 languages, balanced
- `small` (488 MB) - 99 languages, higher accuracy
- `medium` (1.5 GB) - 99 languages, professional
- `large-v3` (3.1 GB) - 99 languages, best accuracy

### Model Selection Guide

**For Real-time Use:**
- `tiny.en` - Live demonstrations, low latency needed
- `base.en` - General use, good balance

**For High Accuracy:**
- `small.en` - Important meetings, interviews
- `medium.en` - Professional transcription
- `large` - Critical accuracy requirements

**For Other Languages:**
- Use multilingual versions (`base`, `small`, etc.)
- Add `-l <language>` for specific languages
- Add `--translate` to translate to English

### Automatic Downloads

Models download automatically when specified:
```bash
# Downloads base.en if not present
recognize -m base.en

# Downloads with CoreML acceleration
recognize -m base.en --coreml
```

**Download Features:**
- ✅ Progress bars with speed/ETA
- ✅ Automatic CoreML versions on macOS
- ✅ Smart caching (no re-downloads)
- ✅ Graceful fallback if CoreML fails

## Usage Modes

### 1. VAD Mode (Recommended)

**Voice Activity Detection** - Only transcribes when you speak.

```bash
make run-vad
# Uses base.en by default

make run-vad MODEL=tiny.en
# Use specific model
```

**Manual command:**
```bash
recognize -m base.en --step 0 --length 30000 -vth 0.6
```

**When to use:**
- Normal conversations
- Interviews, meetings
- Efficient processing (saves CPU)
- Natural speech patterns

### 2. Continuous Mode

Transcribes continuously every few milliseconds.

```bash
recognize -m base.en --step 500 --length 5000
```

**Parameters:**
- `--step 500` - Process every 500ms
- `--length 5000` - Use 5 seconds of audio context

**When to use:**
- Live presentations
- Streaming/broadcasting
- Dense speech with no pauses

### 3. File Output Mode

Save transcriptions to a file.

```bash
recognize -m base.en -f transcript.txt
```

**Features:**
- Real-time display + file saving
- Timestamps included
- Append mode (safe for long sessions)

### 4. Multilingual Mode

Transcribe other languages or translate to English.

```bash
# Spanish transcription
recognize -m base -l es

# Spanish → English translation
recognize -m base -l es --translate

# Auto-detect language
recognize -m base -l auto
```

### 5. Speaker Segmentation Mode

Detect when different people are speaking in conversations or meetings.

```bash
# Enable speaker segmentation (requires tdrz model)
recognize -m small.en-tdrz --tinydiarize

# Speaker segmentation with VAD for meetings
recognize -m small.en-tdrz --tinydiarize --step 0 --length 30000

# Save speaker-segmented transcription
recognize -m small.en-tdrz --tinydiarize -f meeting.txt
```

**What you'll see:**
```
[00:00:00.000 --> 00:00:03.800]  Hello everyone, welcome to the meeting. [SPEAKER_TURN]
[00:00:03.800 --> 00:00:07.200]  Thank you for having me. I'd like to discuss...
```

**Requirements:**
- Models with `tdrz` suffix (e.g., `small.en-tdrz`)
- Currently English-only
- Automatically marks speaker changes with `[SPEAKER_TURN]`

## Advanced Configuration

### Audio Device Selection

List available microphones:
```bash
recognize -m base.en
# Shows: "found 5 capture devices" with list
```

Use specific microphone:
```bash
recognize -m base.en -c 3
# Use device #3 (e.g., external mic)
```

### Performance Tuning

#### Thread Count
```bash
recognize -m base.en -t 8
# Use 8 CPU threads (match your CPU cores)
```

#### VAD Sensitivity
```bash
recognize -m base.en --step 0 -vth 0.4
# More sensitive (0.4) - detects quiet speech
recognize -m base.en --step 0 -vth 0.8
# Less sensitive (0.8) - ignores background noise
```

#### Audio Context
```bash
recognize -m base.en --length 15000
# Use 15 seconds of context (better accuracy)
recognize -m base.en --length 3000
# Use 3 seconds (faster processing)
```

### CoreML Control

```bash
# Force enable CoreML (default on macOS)
recognize -m base.en --coreml

# Disable CoreML (use CPU/Metal)
recognize -m base.en --no-coreml
```

### Save Audio Recordings

```bash
recognize -m base.en --save-audio
# Saves audio to timestamped WAV file
```

## Troubleshooting

### Build Issues

**SDL2 not found:**
```bash
brew install sdl2
make clean && make build
```

**CMake not found:**
```bash
brew install cmake
make build
```

**Permission denied:**
```bash
chmod +x recognize
```

### Runtime Issues

**No microphone detected:**
- Check System Preferences > Security & Privacy > Microphone
- Grant permission to Terminal/your shell

**Model download fails:**
- Check internet connection
- Try different model: `make run-model MODEL=tiny.en`
- Manual download: Check [Hugging Face](https://huggingface.co/ggerganov/whisper.cpp)

**CoreML fails:**
- Use `--no-coreml` flag
- Still gets Metal GPU acceleration
- Only affects encoder performance

**Poor transcription quality:**
- Try larger model: `make run-model MODEL=small.en`
- Adjust VAD threshold: `-vth 0.5`
- Check microphone quality/positioning
- Reduce background noise

**High CPU usage:**
- Use smaller model: `make run-model MODEL=tiny.en`
- Reduce threads: `-t 2`
- Increase step size: `--step 1000`

### Performance Optimization

**For maximum speed:**
```bash
make run-model MODEL=tiny.en
# + Use VAD mode
# + Enable CoreML
```

**For maximum accuracy:**
```bash
make run-model MODEL=large
# + Use longer context: --length 30000
# + Continuous mode for dense speech
```

**For battery efficiency:**
```bash
make run-vad MODEL=base.en
# VAD mode uses less CPU when silent
```

## Development

### Building from Source

```bash
# Full rebuild
make fresh

# Quick rebuild after changes
make rebuild

# Test changes
make test
```

### Makefile Commands

```bash
make help           # Show all available commands
make info           # Show system information
make check-deps     # Verify dependencies
make clean          # Remove build artifacts
```

### Development Shortcuts

```bash
make dev            # Clean + build + run (for development)
make quick          # Rebuild + run (after small changes)
```

### Project Structure

```
src/cli/
├── whisper-stream-coreml.cpp    # Main application (builds as 'recognize')
├── model_manager.cpp/.h         # Model download/management
├── CMakeLists.txt              # Build configuration
├── Makefile                    # Build automation
├── build.sh                    # Shell build script
├── README.md                   # Basic usage
├── TUTORIAL.md                 # This file
└── models/                     # Downloaded models (git-ignored)
```

## Examples Cookbook

### Meeting Transcription
```bash
# High accuracy, save to file
make run-model MODEL=small.en
recognize -m small.en -f meeting.txt --step 0 --length 30000
```

### Live Streaming
```bash
# Fast, continuous mode
make run-model MODEL=tiny.en
recognize -m tiny.en --step 250 --length 3000
```

### Multilingual Interview
```bash
# Auto-detect language, translate to English
recognize -m base -l auto --translate -f interview.txt
```

### Phone Call Transcription
```bash
# Optimize for voice quality
recognize -m base.en -vth 0.4 --step 0 --length 10000
```

### Podcast Recording
```bash
# Professional quality with timestamps
recognize -m medium.en -f podcast.txt --no-timestamps false
```

### Meeting with Multiple Speakers
```bash
# Detect speaker changes in meetings
recognize -m small.en-tdrz --tinydiarize -f meeting.txt --step 0 --length 30000
```

### Interview Transcription
```bash
# Two-person interview with speaker detection
recognize -m small.en-tdrz --tinydiarize --step 0 --length 15000 -vth 0.5
```

---

## 🎤 Ready to Start?

1. **First time**: `make install-deps && make run`
2. **Quick start**: `make run-model MODEL=base.en`
3. **Best quality**: `make run-model MODEL=large`
4. **Fastest**: `make run-model MODEL=tiny.en`
5. **Speaker segmentation**: `make run-model MODEL=small.en-tdrz --tinydiarize`

Enjoy real-time speech recognition with CoreML acceleration! 🚀