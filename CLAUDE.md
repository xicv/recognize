# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

macOS CLI application (`recognize`) for real-time speech recognition with CoreML acceleration, built on whisper.cpp. C++17, macOS 10.15+, Apple Silicon optimized.

## Build & Dev Commands

```bash
make install-deps          # First time: install SDL2, CMake via Homebrew
make build                 # Full build (configure + compile)
make rebuild               # Quick rebuild (skips cmake configure step)
make test                  # Smoke test (runs --help check)
make fresh                 # Clean + full build
make clean                 # Remove build artifacts

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

**`recognize.cpp`** (~1900 lines) — Monolithic main file containing:
- CLI argument parsing (`whisper_params_parse`) with bounds-checked argument access
- Real-time audio capture loop with sliding window / VAD modes
- Signal handler with graceful shutdown (Ctrl-C confirmation during recording)
- Auto-copy session management (`AutoCopySession` struct)
- Export session management (`ExportSession` struct)
- Meeting session management (`MeetingSession` struct) with speaker tracking and multi-pass summarization
- Hallucination filtering (`filter_hallucinations()`) removes phantom phrases and deduplicates
- Output modes: original, english, bilingual (bilingual creates a second whisper context)

**`model_manager.h/cpp`** — Model registry, auto-download, CoreML model extraction, interactive selection. Models stored in `~/.recognize/models/`.

**`config_manager.h/cpp`** — Multi-layer config with priority: CLI args > env vars (`WHISPER_*` prefix) > project config (`.whisper-config.json`) > user config (`~/.recognize/config.json`). Hand-rolled JSON parser (no external JSON library). Supports meeting settings: `meeting_mode`, `meeting_prompt`, `meeting_name`, `meeting_initial_prompt`, `meeting_timeout`, `meeting_max_single_pass`.

**`export_manager.h/cpp`** — Stateful export: add segments during recording, export on session end. Supports TXT, Markdown, JSON, CSV, SRT, VTT, XML. `TranscriptionSegment` includes `speaker_id` for speaker-labeled exports.

**`whisper_params.h`** — Single struct holding all runtime parameters. Every feature flag and setting lives here. Includes `initial_prompt`, `suppress_regex`, `vad_model_path`, `meeting_timeout`, `meeting_max_single_pass`.

### Build System

CMake is the real build system; the top-level `Makefile` wraps it for convenience. Key CMake flags:
- `WHISPER_COREML=ON` — CoreML acceleration (default on)
- `GGML_USE_METAL=ON` — Metal GPU backend
- `WHISPER_SDL2=ON` — Audio capture
- Compiles with `-O3 -march=native`

The build copies the binary from `build/recognize` to `./recognize` in the source directory.

### Key Design Decisions

- **No external JSON library**: `config_manager.cpp` implements its own JSON parsing (~1000 lines). Be careful when modifying config serialization.
- **whisper.cpp examples as source files**: `common.cpp`, `common-whisper.cpp`, `common-sdl.cpp` are compiled directly from the whisper.cpp examples directory, not as a library.
- **CoreML auto-detection**: If CoreML is enabled but no CoreML model is found, it auto-disables to prevent crashes.
- **Global atomics for signal handling**: `g_interrupt_received` and `g_is_recording` are global `std::atomic<bool>` used for graceful Ctrl-C handling.
- **Meeting mode**: On session end, writes transcription to a temp file and pipes to `claude` CLI via stdin (avoiding shell injection). Supports multi-pass summarization for long meetings (>20k words). Falls back to saving raw markdown if Claude CLI isn't available. Output wraps meeting content in HTML comments within the markdown file.
- **Speaker tracking**: `MeetingSession` tracks speaker turns from whisper.cpp's `speaker_turn_next` API, labeling `[Speaker 1]`, `[Speaker 2]`, etc. Speaker IDs flow through to export formats.
- **Meeting-optimized defaults**: When `--meeting` is active, automatically sets `keep_ms=2000`, `step_ms=5000`, `length_ms=15000`, `no_context=false`, `initial_prompt` for meeting transcription, and whisper parameters tuned for accuracy (`suppress_nst`, higher `no_speech_thold`, `entropy_thold`).

### CI/CD

GitHub Actions workflow (`.github/workflows/release.yml`) builds for both x86_64 and arm64 on tag push (`v*`), creates release packages with universal binary support.

## License

MIT
