# recognize Tutorial

A comprehensive guide to using the macOS CLI for real-time speech recognition with CoreML acceleration.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Installation](#installation)
3. [First Run](#first-run)
4. [Model Management](#model-management)
5. [Usage Modes](#usage-modes)
6. [Meeting Organization](#meeting-organization)
7. [Advanced Configuration](#advanced-configuration)
8. [Troubleshooting](#troubleshooting)
9. [Development](#development)

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

## Meeting Organization

### AI-Powered Meeting Transcription

Transform raw meeting transcriptions into professional, structured summaries using Claude CLI integration.

### Quick Start with Meetings

```bash
# Basic meeting organization (most common use case)
recognize --meeting

# Custom filename for your meeting
recognize --meeting --name project-review

# Save to specific directory
recognize --meeting --name ~/Documents/meetings/team-meeting.md
```

### Prerequisites for Meeting Organization

**Required:**
- Base CLI installation (see [Installation](#installation))
- Microphone access for transcription

**Optional (Recommended):**
- Claude CLI for AI organization: `https://claude.ai/code`
- Speaker segmentation model: `small.en-tdrz` for multi-speaker meetings

### Meeting Organization Workflow

1. **Recording Phase** 🎤
   ```bash
   recognize --meeting --name standup-meeting
   ```
   - Transcribes meeting in real-time
   - Works with all existing features (VAD, speaker segmentation, translation)
   - Captures clean transcription data for processing

2. **Processing Phase** 🤖
   - When you press `Ctrl-C` to end recording
   - Automatically sends transcription to Claude CLI
   - Uses comprehensive meeting organization prompt
   - Falls back to raw transcription if Claude unavailable

3. **Output Phase** 📋
   - Generates professional meeting summary in markdown
   - Includes action items, decisions, and metadata
   - Saves to specified filename or auto-generated format

### Meeting Organization Examples

#### Basic Daily Standup
```bash
recognize --meeting --name daily-standup
```
**Output file:** `daily-standup.md`
**Perfect for:** Quick team updates, task tracking

#### Project Review Meeting
```bash
recognize --meeting --name project-review --output-mode english -m base.en
```
**Features:**
- English translation for multilingual teams
- Professional output format
- Custom filename for easy reference

#### Multi-Speaker Team Meeting
```bash
recognize --meeting --name team-meeting --tinydiarize -m small.en-tdrz --step 0
```
**Features:**
- Speaker detection and labeling
- Voice Activity Detection for efficiency
- High-quality transcription

#### Client Meeting with Translation
```bash
recognize --meeting --name client-call --output-mode bilingual -m medium -l auto
```
**Features:**
- Bilingual output (original + English)
- Auto-language detection
- Professional quality model

#### Board Meeting (High Quality)
```bash
recognize --meeting --name quarterly-review --tinydiarize -m medium.en
```
**Features:**
- Maximum accuracy for important meetings
- Speaker segmentation for multiple participants
- Comprehensive documentation

### Advanced Meeting Features

#### Custom Prompts
Create specialized meeting formats:
```bash
# Create custom prompt file
cat > retrospective-prompt.txt << 'EOF'
You are a sprint retrospective facilitator. Please organize this meeting transcription into:
1. What went well during the sprint
2. What didn't go well
3. Action items for next sprint
4. Team appreciation notes
Focus on team dynamics and process improvements.
EOF

# Use custom prompt
recognize --meeting --prompt retrospective-prompt.txt --name sprint-retro
```

#### Integration with Other Features

**Meeting + Export:**
```bash
recognize --meeting --name all-hands --export --export-format json
```
- Gets AI-organized meeting summary
- Also exports raw transcription data
- Perfect for documentation and analysis

**Meeting + Auto-Copy:**
```bash
recognize --meeting --name standup --auto-copy
```
- AI-organized meeting summary saved to file
- Raw transcription automatically copied to clipboard
- Quick sharing with team members

**Meeting + Configuration:**
```bash
# Set meeting mode as default
recognize config set meeting_mode true
recognize config set meeting_name meeting-%Y-%m-%d.md

# Now every session is a meeting
recognize -m base.en  # Automatically creates meeting-2025-10-01.md
```

### Meeting Output Structure

The AI-organized meeting summaries include:

#### 📊 Meeting Metadata
- **Meeting Title**: AI-generated descriptive title
- **Date & Time**: Extracted from session
- **Duration**: Calculated from transcription length
- **Attendees**: Inferred from speaker patterns
- **Meeting Type**: Detected (stand-up, planning, review, etc.)

#### 🎯 Executive Summary
- **Main Objective**: Primary meeting goal
- **Key Outcomes**: 3-5 major results
- **Critical Decisions**: Important decisions made
- **Next Meeting**: Follow-up scheduling

#### 📝 Detailed Discussion
Organized by topics with:
- **Discussion Points**: Key arguments and ideas
- **Decisions Made**: Specific outcomes
- **Action Items**: Tasks with owners and deadlines
- **Owner & Deadline**: Assignment tracking

#### ✅ Action Items Tracker
| Task | Owner | Deadline | Status | Priority |
|------|-------|----------|--------|----------|
| [Specific task] | [Person] | [Date] | [Not Started/In Progress/Done] | [High/Medium/Low] |

#### 🔍 Additional Analysis
- **Key Decisions Log**: Decisions with rationale and impact
- **Open Issues**: Problems raised and solutions proposed
- **Follow-up Requirements**: Immediate, short-term, and long-term actions
- **Quality Notes**: Meeting effectiveness and improvement suggestions

### Troubleshooting Meeting Organization

**Claude CLI Not Found:**
```bash
# Check if Claude CLI is available
which claude

# If not found, install from https://claude.ai/code
# Meeting mode still works - saves raw transcription instead
```

**Poor Meeting Organization Quality:**
- Ensure clear audio recording
- Use appropriate microphone placement
- Consider higher-quality model (`medium.en` vs `base.en`)
- Enable speaker segmentation for multi-speaker meetings

**Large Meeting Files:**
- Meeting mode works with long sessions
- Claude CLI handles large transcriptions
- Consider splitting very long meetings (>2 hours)

**Custom Prompt Not Working:**
- Verify prompt file path exists
- Check prompt file formatting
- Test with shorter custom prompts first

### Meeting Organization Best Practices

#### Before the Meeting
1. **Test Setup**: Run a quick test recording
2. **Choose Model**: `base.en` for most cases, `medium.en` for important meetings
3. **Speaker Segmentation**: Use `--tinydiarize` for multi-speaker meetings
4. **File Naming**: Use descriptive names with dates

#### During the Meeting
1. **Microphone Placement**: Central location for group discussions
2. **Speaker Identification**: Ask participants to introduce themselves
3. **Pause Between Topics**: Helps AI identify discussion segments
4. **Clear Speech**: Encourage participants to speak clearly

#### After the Meeting
1. **Review Output**: Check AI-organized summary for accuracy
2. **Action Items**: Verify assigned tasks and deadlines
3. **File Management**: Store meeting summaries in organized directory
4. **Share with Team**: Distribute organized summary promptly

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

### AI-Organized Meetings
```bash
# Basic daily standup with AI organization
recognize --meeting --name daily-standup

# Project review with speaker segmentation
recognize --meeting --name project-review --tinydiarize -m small.en-tdrz

# Client meeting with bilingual output
recognize --meeting --name client-call --output-mode bilingual -m medium -l auto

# Sprint retrospective with custom prompt
cat > retrospective.txt << 'EOF'
Focus on: 1) What went well, 2) What didn't go well, 3) Action items, 4) Team appreciation
EOF
recognize --meeting --prompt retrospective.txt --name sprint-retro

# Board meeting with maximum accuracy
recognize --meeting --name quarterly-review --tinydiarize -m medium.en
```

---

## 🎤 Ready to Start?

1. **First time**: `make install-deps && make run`
2. **Quick start**: `make run-model MODEL=base.en`
3. **Best quality**: `make run-model MODEL=large`
4. **Fastest**: `make run-model MODEL=tiny.en`
5. **Speaker segmentation**: `make run-model MODEL=small.en-tdrz --tinydiarize`
6. **AI-organized meeting**: `recognize --meeting --name team-meeting`

Enjoy real-time speech recognition with CoreML acceleration! 🚀