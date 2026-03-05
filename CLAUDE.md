# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

macOS CLI application (`recognize`) for real-time speech recognition with CoreML acceleration, built on whisper.cpp. C++17, macOS 12.0+, Apple Silicon optimized.

### Use Cases

1. **Voice input for Claude Code** — `/r` starts recording, auto-stops after silence, transcript becomes Claude's input. No manual stop needed. (`/r c` for continuous, `/r m` for meetings)
2. **Meeting transcription** — Record meetings with speaker tracking, then AI-summarize into structured notes with action items (`recognize --meeting`)
3. **Real-time transcription** — Live speech-to-text to console, file, or clipboard with multi-language and bilingual support
4. **Subtitle generation** — Export to SRT/VTT for video captioning
5. **Pipe-friendly scripting** — Clean stdout output (no ANSI codes) for integration with other tools

## Build & Dev Commands

```bash
make install-deps          # First time: install SDL2, CMake via Homebrew
make build                 # Full build (configure + compile)
make rebuild               # Quick rebuild (skips cmake configure step)
make test                  # Smoke test (runs --help check)
make fresh                 # Clean + full build
make clean                 # Remove build artifacts
make install               # Install to /usr/local/bin/recognize

make dev                   # Clean + build + run (full cycle)
make quick                 # Rebuild + run (fast iteration)
make run-model MODEL=tiny.en  # Run with specific model (fast for testing)
make run-vad               # Run VAD mode (recommended for real-time)
make stop                  # Kill all running recognize processes
```

Config and model management: `make config-list`, `make config-set KEY=x VALUE=y`, `make list-models`, `make list-downloaded`.

## Architecture

### Repo Layout

This CLI lives at `src/cli/` within a larger monorepo (`recogniz.ing`). The whisper.cpp dependency is a git submodule at `../../fixtures/whisper.cpp` (relative to this directory).

### Core Components & Data Flow

```
Audio Input (SDL2)
    → recognize.cpp (main loop, signal handling, session management)
    → whisper.cpp (transcription engine via whisper_full())
    → Output: console, clipboard (auto-copy), file export, or meeting summary
```

**`recognize.cpp`** (~2000 lines) — Monolithic main file containing:
- CLI argument parsing (`whisper_params_parse`) with bounds-checked argument access
- Real-time audio capture loop with sliding window / VAD modes
- Silence timeout: uses `vad_simple()` on raw `pcmf32_new` audio to detect speech vs silence (not inference results). Only triggers after first speech. Sets `is_running = false` for graceful exit.
- Signal handler with graceful shutdown (Ctrl-C confirmation during recording, `isatty()` skip when no TTY)
- Pipe-friendly output mode: detects `!isatty(STDOUT_FILENO)` to suppress ANSI codes, route info to stderr, and use dual-buffer (finalized + current) text accumulation for clean output on exit
- Auto-copy session management (`AutoCopySession` struct)
- Export session management (`ExportSession` struct)
- Meeting session management (`MeetingSession` struct) with speaker tracking and multi-pass summarization
- Hallucination filtering (`filter_hallucinations()`) removes phantom phrases (silence markers, typing sounds, "Thank you for watching", URLs, etc.) and deduplicates
- Output modes: original, english, bilingual (bilingual creates a second whisper context)

**`model_manager.h/cpp`** — Model registry, auto-download, CoreML model extraction, interactive selection. Models stored in `~/.recognize/models/`.

**`config_manager.h/cpp`** — Multi-layer config with priority: CLI args > env vars (`WHISPER_*` prefix) > project config (`.whisper-config.json`) > user config (`~/.recognize/config.json`). Hand-rolled JSON parser (no external JSON library). Supports all settings including `silence_timeout`.

**`export_manager.h/cpp`** — Stateful export: add segments during recording, export on session end. Supports TXT, Markdown, JSON, CSV, SRT, VTT, XML. `TranscriptionSegment` includes `speaker_id` for speaker-labeled exports.

**`whisper_params.h`** — Single struct holding all runtime parameters. Every feature flag and setting lives here. Includes `initial_prompt`, `suppress_regex`, `vad_model_path`, `meeting_timeout`, `meeting_max_single_pass`, `silence_timeout`.

### Build System

CMake is the real build system; the top-level `Makefile` wraps it for convenience. Key CMake flags:
- `WHISPER_COREML=ON` + `WHISPER_COREML_ALLOW_FALLBACK=ON` — CoreML acceleration with fallback
- `GGML_METAL=ON` + `GGML_METAL_EMBED_LIBRARY=ON` — Metal GPU backend with embedded shaders
- `GGML_NATIVE=ON` + `GGML_ACCELERATE=ON` + `GGML_BLAS=ON` — Apple Silicon optimization
- `GGML_LTO=ON` — Link-time optimization
- `WHISPER_SDL2=ON` — Audio capture
- Compiles with `-O3 -march=native`, LTO enabled

The build copies the binary from `build/recognize` to `./recognize` in the source directory.

### Key Design Decisions

- **No external JSON library**: `config_manager.cpp` implements its own JSON parsing (~1000 lines). Be careful when modifying config serialization.
- **whisper.cpp examples as source files**: `common.cpp`, `common-whisper.cpp`, `common-sdl.cpp` are compiled directly from the whisper.cpp examples directory, not as a library.
- **CoreML auto-detection**: If CoreML is enabled but no CoreML model is found, it auto-disables to prevent crashes.
- **Async-safe signal handling**: Signal handler only sets atomic flag; confirmation dialog runs in main loop via `check_interrupt_with_confirmation()`. `g_interrupt_received` and `g_is_recording` are global `std::atomic<bool>`.
- **CoreML warm-up**: Runs a 1-second dummy inference at startup to trigger ANE compilation before recording starts.
- **Context-aware thread count**: When CoreML is active, uses 4 threads (decoder-only); without CoreML, scales up to 8 threads.
- **Meeting mode**: On session end, writes transcription to a temp file in `~/.recognize/tmp/` (not `/tmp/`) and pipes to `claude` CLI via stdin (avoiding shell injection). Supports multi-pass summarization for long meetings (>20k words). Falls back to saving raw markdown if Claude CLI isn't available. Output wraps meeting content in HTML comments (with `-->` escaped) within the markdown file.
- **Unified speaker tracking**: `SpeakerTracker` struct shared across export, meeting, and display paths — eliminates the old dual-counter desync. `MeetingSession` labels `[Speaker 1]` on the very first segment.
- **Meeting-optimized defaults**: When `--meeting` is active, auto-enables `tinydiarize`, sets `keep_ms=1000`, `step_ms=5000`, `length_ms=15000`, `beam_size=5`, `freq_thold=200`, `no_context=false`, `initial_prompt` for meeting transcription, and whisper parameters tuned for accuracy (`suppress_nst`, `no_speech_thold=0.4`, `entropy_thold=2.2`).
- **VAD model auto-download**: `--vad-model auto` downloads Silero VAD v5.1.2 (~864KB) to `~/.recognize/models/`.
- **Pipe-friendly output**: When stdout is not a TTY, all informational messages (model loading, auto-copy, export, meeting status) go to stderr. Streaming display uses dual-buffer (finalized + current group) to output only clean finalized text on exit. No ANSI codes in pipe mode.
- **Silence timeout**: `--silence-timeout N` auto-stops recording after N seconds of no speech. Uses `vad_simple()` on raw new audio samples (not inference results — the sliding window would always produce text from old audio). Only triggers after first speech detected (prevents premature exit during warmup). Uses `is_running = false` for graceful exit (same path as SDL quit). Automatically disabled in meeting mode. Checked in both VAD and non-VAD paths.
- **Hallucination filtering**: Removes known phantom phrases (silence markers, typing/keyboard sounds, "Thank you for watching", URLs, CJK equivalents). Case-insensitive matching. Also deduplicates consecutive identical sentences.

### Claude Code Voice Integration

Files in `~/.claude/commands/` and `~/.recognize/`:

| Command | Alias | Mode | Behavior |
|---------|-------|------|----------|
| `/recognize` | `/r` | Auto-stop | Single bash call: launch → wait for silence → return transcript → Claude responds |
| `/recognize c` | `/r c` | Continuous | Background launch, manual `/rs` to stop |
| `/recognize m` | `/r m` | Meeting | Background launch with large-v3-turbo, manual `/rs` to stop |
| `/recognize-stop` | `/rs` | — | Stop recording, ASR error correction, respond to transcript |

**Key files:**
- `~/.claude/commands/recognize.md` — Skill with mode detection, ASR error correction rules
- `~/.claude/commands/recognize-stop.md` — Stop skill with full ASR correction pipeline
- `~/.claude/commands/r.md` / `rs.md` — Aliases
- `~/.recognize/claude-launch.sh` — Launcher: process management, peon-ping mute/unmute, auto-stop wait loop
- `~/.recognize/claude-session.{pid,txt,log}` — Session files (PID, transcript, stderr log)

**Auto-stop flow** (single `/r` invocation, no `/rs` needed):
1. Skill shows "Speak now..." prompt
2. `claude-launch.sh` launches recognize with `--silence-timeout 5`, backgrounds it, verifies liveness
3. Script polls PID every 0.5s until recognize auto-exits (up to 100s safety net)
4. Reads transcript from session file, copies to clipboard, cleans up, re-enables peon-ping
5. Skill applies ASR error correction and treats result as user message

### CI/CD

GitHub Actions workflow (`.github/workflows/release.yml`) builds for both x86_64 and arm64 on tag push (`v*`), creates release packages with universal binary support.

## License

MIT
