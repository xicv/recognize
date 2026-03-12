# recognize

Talk to your Mac and it listens. A fast, local speech recognition CLI for macOS, powered by [whisper.cpp](https://github.com/ggerganov/whisper.cpp) with CoreML and Metal acceleration on Apple Silicon.

No cloud. No API keys. No latency. Just speak.

## Voice Mode for Claude Code

The flagship feature: **talk to Claude Code instead of typing**. Say `/r` in Claude Code, speak your request, and the transcript becomes Claude's input automatically.

```
You: /r
> Speak now... (recording)
> (you speak for a few seconds, then pause)
> (auto-stops after 5s of silence)
Claude: I'll help you with that. Let me...
```

One command. No manual stop needed. recognize listens, auto-stops when you're done talking, and feeds the transcript straight to Claude.

| Command | Alias | What it does |
|---------|-------|--------------|
| `/recognize` | `/r` | Speak, auto-stop on silence, Claude responds |
| `/recognize c` | `/rc` | Continuous recording until you manually stop |
| `/recognize m` | `/r m` | Meeting mode with AI-powered summary |
| `/recognize p` | `/rp` | Push-to-talk (hold space to speak) |
| `/recognize-stop` | `/rs` | Stop a continuous/meeting session |
| `/recognize-history` | `/rh` | Search past transcriptions |

**Setup:** Install the `recognize-voice` plugin, or copy command files to `~/.claude/commands/` and the launcher to `~/.recognize/claude-launch.sh`. See [Installation](#installation) below.

## What You Can Do

### Real-time transcription
Speak into your mic and see text appear in real-time. Works with 99+ languages.

```bash
recognize -m base.en                              # English, good speed/accuracy
recognize -m medium --output-mode bilingual -l zh  # Chinese with English translation
recognize -m base.en --step 0 --length 30000       # VAD mode (recommended)
```

### Meeting transcription with AI summary
Record a meeting, get structured notes with action items, speaker labels, and decisions - organized by Claude.

```bash
recognize --meeting                        # Start recording, Ctrl-C when done
recognize --meeting --name "team-standup"   # Named output file
recognize --meeting --tinydiarize -m small.en-tdrz  # With speaker tracking
```

When you stop recording, the transcript is sent to Claude CLI which produces a structured summary with meeting type classification, action items, key decisions, and follow-up drafts. Long meetings (>20k words) use multi-pass summarization. Falls back to raw transcript if Claude CLI isn't available.

### Subtitle generation
Generate professional subtitles for video content.

```bash
recognize -m base.en --export --export-format srt   # SRT for video players
recognize -m base.en --export --export-format vtt   # WebVTT for web
```

### Scripting and piping
Clean stdout output with no ANSI codes when piped - ideal for building speech-powered workflows.

```bash
recognize --no-export --no-timestamps 2>/dev/null | my-processor
recognize --no-export --no-timestamps > transcript.txt 2>/dev/null &
```

### Auto-copy to clipboard
Transcription is automatically copied to your clipboard when a session ends.

```bash
recognize -m base.en --auto-copy
```

## Installation

**One-command install** (downloads pre-built binary + Claude Code plugin):

```bash
curl -sSL https://raw.githubusercontent.com/anthropic-xi/recogniz.ing/main/src/cli/install.sh | sh
```

**Or with Homebrew:**

```bash
brew tap recognizing/tap && brew install recognize
```

**Or build from source:**

```bash
make install-deps && make build && make install
```

**Requirements:** macOS 12.0+. Models download automatically on first use.

## Available Models

| Model | Size | Speed | Languages | Best for |
|-------|------|-------|-----------|----------|
| `tiny.en` | 39 MB | Fastest | English | Quick drafts, voice commands |
| `base.en` | 148 MB | Fast | English | Daily use, good accuracy |
| `small.en` | 488 MB | Medium | English | Higher accuracy |
| `medium.en` | 1.5 GB | Slower | English | High accuracy |
| `large-v3` | 3.1 GB | Slowest | 99 languages | Maximum accuracy |
| `large-v3-turbo` | 1.5 GB | Fast | 99 languages | Best accuracy/speed ratio |

For multilingual or bilingual output, use models without the `.en` suffix (`base`, `medium`, `large-v3`).

```bash
make list-models       # See all available models
make list-downloaded   # See what you have installed
```

## Build Commands

```bash
make build             # Full build (configure + compile)
make rebuild           # Quick rebuild (skips cmake configure)
make fresh             # Clean + full build
make clean             # Remove build artifacts
make test              # Smoke test (--help check)
make install           # Install to /usr/local/bin
make install-user      # Install to ~/bin
```

## Configuration

Settings are layered: **CLI args > env vars > project config > user config**.

```bash
# Set defaults
recognize config set model base.en
recognize config set auto_copy_enabled true
recognize config set silence_timeout 5

# Or use environment variables
export WHISPER_MODEL=base.en
export WHISPER_SILENCE_TIMEOUT=5

# View current config
recognize config list
```

Config files live at `~/.recognize/config.json` (user) and `.whisper-config.json` (project). See the full [configuration reference](#configuration-reference) below.

## Multi-Language Support

Three output modes for seamless translation:

- **`original`** - Transcribe in the spoken language (default)
- **`english`** - Translate everything to English
- **`bilingual`** - Show both original and English side by side

```bash
recognize -m medium --output-mode bilingual -l zh   # Chinese + English
recognize -m medium --output-mode english -l ja      # Japanese -> English
recognize -m medium --output-mode original -l es     # Spanish as-is
```

Bilingual mode runs two inference passes (~2x processing time). Use `medium`, `large-v3`, or `large-v3-turbo` for best translation quality.

## Export Formats

Export transcriptions to TXT, Markdown, JSON, CSV, SRT, VTT, or XML.

```bash
recognize -m base.en --export --export-format md --export-file notes.md
recognize -m base.en --export --export-format json --export-include-confidence
recognize -m base.en --export --export-format srt --export-file subtitles.srt
```

Configure default export behavior:
```bash
recognize config set export_enabled true
recognize config set export_format json
```

## Performance Tips

1. **CoreML** is enabled by default - best performance on Apple Silicon
2. **VAD mode** (`--step 0`) is the most efficient for real-time use
3. **Thread count** auto-tunes: 4 with CoreML (decoder-only), up to 8 without
4. **`large-v3-turbo`** gets near large-v3 accuracy at ~40% faster speed
5. **`tiny.en`** for the fastest possible processing when accuracy is less critical

## Speaker Segmentation

Track who's speaking with automatic `[Speaker N]` labels:

```bash
recognize -m small.en-tdrz --tinydiarize
recognize -m small.en-tdrz --tinydiarize --step 0 --length 30000  # VAD mode
```

Currently English-only, requires models with `tdrz` suffix.

---

## Command Line Reference

### Basic Options
| Flag | Description | Default |
|------|-------------|---------|
| `-m, --model` | Model name or file path | (interactive) |
| `-l, --language` | Source language | `en` |
| `-t, --threads` | Processing threads | 4 |
| `-v, --version` | Show version | |
| `-h, --help` | Show help | |

### Audio Options
| Flag | Description | Default |
|------|-------------|---------|
| `-c, --capture` | Audio capture device ID | -1 (default) |
| `--step` | Audio step size in ms (0 for VAD) | 3000 |
| `--length` | Audio length in ms | 10000 |
| `--keep` | Audio kept from previous step in ms | 200 |

### Processing Options
| Flag | Description | Default |
|------|-------------|---------|
| `-tr, --translate` | Translate to English | off |
| `-vth, --vad-thold` | VAD threshold | 0.6 |
| `-fth, --freq-thold` | High-pass frequency cutoff | 100.0 |
| `-bs, --beam-size` | Beam search size | -1 |
| `-mt, --max-tokens` | Max tokens per chunk | 32 |
| `-fa, --flash-attn` | Flash attention | on |

### Output Options
| Flag | Description | Default |
|------|-------------|---------|
| `-f, --file` | Output to file | |
| `-om, --output-mode` | `original`, `english`, or `bilingual` | `original` |
| `-sa, --save-audio` | Save recorded audio to WAV | off |
| `--no-timestamps` | Disable timestamps | off |
| `-ps, --print-special` | Print special tokens | off |

### CoreML Options
| Flag | Description | Default |
|------|-------------|---------|
| `--coreml` | Enable CoreML acceleration | on |
| `--no-coreml` | Disable CoreML | |
| `-cm, --coreml-model` | Specific CoreML model path | |

### Accuracy Options
| Flag | Description | Default |
|------|-------------|---------|
| `--initial-prompt` | Whisper conditioning prompt | |
| `--suppress-regex` | Regex pattern to suppress | |
| `--vad-model` | Silero VAD model path or `auto` | |
| `--silence-timeout N` | Auto-stop after N seconds of silence | 0 (off) |

### Export Options
| Flag | Description | Default |
|------|-------------|---------|
| `--export` | Enable export on session end | off |
| `--export-format` | `txt`, `md`, `json`, `csv`, `srt`, `vtt`, `xml` | `txt` |
| `--export-file` | Output file path | auto |
| `--export-no-metadata` | Exclude metadata | off |
| `--export-no-timestamps` | Exclude timestamps | off |
| `--export-include-confidence` | Include confidence scores | off |

### Auto-Copy Options
| Flag | Description | Default |
|------|-------------|---------|
| `--auto-copy` | Copy transcript to clipboard on exit | off |
| `--auto-copy-max-duration N` | Max session hours before skip | 2 |
| `--auto-copy-max-size N` | Max transcript bytes before skip | 1MB |

### Meeting Options
| Flag | Description | Default |
|------|-------------|---------|
| `--meeting` | Enable meeting mode with AI summary | off |
| `--no-meeting` | Disable meeting mode | |
| `--prompt` | Custom prompt text or file | |
| `--name` | Output filename prefix | |
| `--meeting-timeout N` | Claude CLI timeout in seconds | 120 |
| `--meeting-max-single-pass N` | Words before multi-pass | 20000 |

### Speaker Segmentation Options
| Flag | Description | Default |
|------|-------------|---------|
| `-tdrz, --tinydiarize` | Enable speaker segmentation | off |

### Push-to-Talk Options
| Flag | Description | Default |
|------|-------------|---------|
| `--ptt` | Enable push-to-talk mode | off |
| `--ptt-key KEY` | PTT key: `space`, `right_option`, `right_ctrl`, `fn`, `f13` | `space` |

### Model Management
| Flag | Description |
|------|-------------|
| `--list-models` | List available models |
| `--list-downloaded` | Show downloaded models with sizes |
| `--show-storage` | Storage usage breakdown |
| `--delete-model MODEL` | Delete a specific model |
| `--delete-all-models` | Delete all models |
| `--cleanup` | Remove orphaned files |

### History
| Command | Description |
|---------|-------------|
| `history list [--limit N]` | Recent transcriptions |
| `history search "<query>"` | Full-text search |
| `history show <id>` | Full transcript by ID |
| `history count` | Total entries |
| `history clear [--older-than Nd]` | Delete entries |

## Configuration Reference

### Config Commands
```bash
recognize config list                    # Show all settings
recognize config set model base.en       # Set a value
recognize config get model               # Get a value
recognize config unset model             # Remove a value
recognize config reset                   # Reset to defaults
```

### Environment Variables

All settings support `WHISPER_` prefixed env vars:

```bash
WHISPER_MODEL=base.en
WHISPER_LANGUAGE=en
WHISPER_THREADS=8
WHISPER_COREML=true
WHISPER_SILENCE_TIMEOUT=5
WHISPER_AUTO_COPY=true
WHISPER_MEETING=true
WHISPER_MEETING_NAME=standup
```

### Config File (`~/.recognize/config.json`)

```json
{
  "default_model": "base.en",
  "threads": 8,
  "use_coreml": true,
  "language": "en",
  "vad_threshold": 0.6,
  "step_ms": 3000,
  "length_ms": 10000,
  "auto_copy_enabled": true,
  "silence_timeout": 0,
  "meeting_mode": false,
  "meeting_timeout": 120,
  "meeting_max_single_pass": 20000
}
```

## Troubleshooting

**Build fails:** Ensure SDL2 (`brew install sdl2`) and CMake (`brew install cmake`) are installed. Try `make fresh` for a clean build.

**No audio:** Check microphone permissions in System Settings > Privacy & Security > Microphone. Try `-c` to select a different audio device.

**Poor accuracy:** Use a larger model (`base.en` -> `small.en`), try VAD mode (`--step 0`), or adjust VAD threshold (`-vth`).

**CoreML issues:** Runs fine without CoreML (auto-fallback). Force disable with `--no-coreml` if needed.

## Documentation

- **[TUTORIAL.md](TUTORIAL.md)** - Comprehensive usage guide
- `make help` - All Makefile targets
- `recognize --help` - Full CLI options

## License

MIT License - see [LICENSE](LICENSE) for details.
