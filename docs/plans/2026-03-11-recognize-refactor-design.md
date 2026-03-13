# recognize.cpp Refactoring Design

**Goal:** Break the 2782-line monolithic `recognize.cpp` into focused modules following separation of concerns and SOLID principles, without changing any behavior.

**Approach:** Conservative — extract 4 new modules (8 files), leaving `recognize.cpp` as an ~800-line orchestrator.

## Modules

### 1. `text_processing.h/.cpp` (~200 lines)

Pure utility functions with no project dependencies.

**Functions extracted:**
- `trim_whitespace()`
- `filter_hallucinations()` + phantom patterns list
- `copy_to_clipboard_macos()`
- `is_claude_cli_available()`
- `invoke_claude_cli()`
- `refine_transcription()` + `ASR_REFINE_PROMPT`
- `count_words()`
- `split_into_chunks()`

**Depends on:** Nothing in our codebase (only stdlib).

### 2. `meeting_manager.h/.cpp` (~300 lines)

Meeting transcription orchestration.

**Extracted:**
- `MeetingSession` struct
- `DEFAULT_MEETING_PROMPT` constant
- `generate_meeting_filename()` + `generate_fallback_filename()`
- `process_meeting_transcription()`

**Depends on:** `text_processing.h` (for `is_claude_cli_available`, `invoke_claude_cli`, `count_words`, `split_into_chunks`).

### 3. `audio_processor.h/.cpp` (~300 lines)

Whisper inference pipeline and output formatting.

**Extracted:**
- `BilingualSegment` struct
- `normalize_audio()`
- `process_audio_segment()`
- `print_colored_tokens()`
- `SpeakerTracker` struct
- `print_bilingual_results()`

**Depends on:** `whisper.h`, `whisper_params.h`, `export_manager.h`, `text_processing.h`. Forward-declares `AutoCopySession`, `ExportSession`, `MeetingSession` (defined in recognize.cpp).

**Design decision:** `print_bilingual_results` writes to `AutoCopySession`, `ExportSession`, and `MeetingSession`. Rather than making audio_processor depend on all session types, the session structs stay in recognize.cpp and are forward-declared in audio_processor.h.

### 4. `cli_parser.h/.cpp` (~350 lines)

CLI argument parsing and help text.

**Extracted:**
- `whisper_params_parse()`
- `whisper_print_usage()`
- `handle_model_commands()`
- `handle_history_command()`
- Config subcommand handling (currently embedded in parser)

**Depends on:** `whisper_params.h`, `config_manager.h`, `model_manager.h`, `history_manager.h`.

### 5. `recognize.cpp` (~800 lines, reduced)

Session orchestrator and audio loops.

**Retains:**
- `main()` with PTT and standard audio loops
- `AutoCopySession`, `ExportSession` structs
- `should_auto_copy()`, `perform_auto_copy()`, `perform_export()`
- `finalize_session()`
- Signal handler (`signal_handler`, `suppress_stderr`, `restore_stderr`, `check_interrupt_with_confirmation`)
- Global atomics (`g_interrupt_received`, `g_is_recording`)

## Dependency Graph

```
whisper_params.h (unchanged)
        |
        +-- text_processing.h     (no project deps)
        |         |
        +-- meeting_manager.h     (depends on: text_processing)
        |
        +-- audio_processor.h     (depends on: whisper.h, whisper_params.h,
        |                          export_manager.h, text_processing.h)
        |
        +-- cli_parser.h          (depends on: whisper_params.h, config_manager.h,
        |                          model_manager.h, history_manager.h)
        |
        +-- recognize.cpp         (depends on: all above + ptt_manager, SDL)
```

No circular dependencies. `text_processing` is a leaf node.

## Migration Order

Each step produces a buildable, testable state:

1. `text_processing` (leaf node, no deps)
2. `meeting_manager` (depends only on text_processing)
3. `audio_processor` (depends on whisper + text_processing)
4. `cli_parser` (touches the most code in recognize.cpp)

## Testing Strategy

1. **Build verification** — `make rebuild` succeeds with zero new warnings
2. **Smoke tests** — `make test` (--help + history count)
3. **Subcommand tests** — history list/search, config list
4. **Text processing unit tests** — Standalone test binary for pure functions
5. **Header isolation** — Each .h compiles independently
6. **Integration** — Full CLI behavior unchanged

## Constraints

- No behavior changes — pure refactoring
- No formatting-only commits
- Follow existing `*_manager.h/.cpp` naming convention
- C++17, macOS 12.0+ deployment target
- Update CMakeLists.txt for new source files
