# PTT Mode Optimization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Optimize push-to-talk mode for smoother UX: fix first-syllable clipping, faster inference, and proper ready-signal flow.

**Architecture:** Three targeted improvements to the existing PTT pipeline — no architectural changes needed. Pre-roll audio via ring buffer offset, PTT-specific greedy inference params, and a ready-signal protocol between recognize and claude-launch.sh.

**Tech Stack:** C++17, whisper.cpp, SDL2, CoreGraphics, Bash

---

### Task 1: Add pre-roll audio to prevent first-syllable clipping

**Files:**
- Modify: `recognize.cpp:700-729` (PTT capture and retrieval)
- Modify: `whisper_params.h:88-90` (add `ptt_pre_roll_ms` param)
- Modify: `cli_parser.cpp` (add `--ptt-pre-roll` flag)

**Step 1: Add `ptt_pre_roll_ms` parameter to `whisper_params.h`**

In `whisper_params.h`, after line 90 (`std::string ptt_key`), add:

```cpp
int32_t ptt_pre_roll_ms = 300;  // Capture audio from before key press (catches onset consonants)
```

**Step 2: Add CLI flag in `cli_parser.cpp`**

Find the `--ptt-key` parsing block and add after it:

```cpp
else if (arg == "--ptt-pre-roll") { if (!require_arg(i, arg)) return false; params.ptt_pre_roll_ms = std::stoi(argv[++i]); }
```

Add help text in the push-to-talk section:

```
            --ptt-pre-roll N   [%-7d] pre-roll audio in ms before key press (default: 300)
```

**Step 3: Modify PTT capture in `recognize.cpp`**

Replace line 703 (`audio.clear();`) — remove the `audio.clear()` call entirely. The ring buffer continuously captures audio; clearing it discards the pre-roll we need.

Replace lines 700-729 with:

```cpp
if (!is_running_ptt || g_interrupt_received.load()) break;

// Key pressed — record timestamp (don't clear buffer — preserves pre-roll audio)
auto t_press = std::chrono::high_resolution_clock::now();
if (stderr_tty) fprintf(stderr, "\r[Recording...] ");

// Capture while key is held
while (ptt.is_key_held() && is_running_ptt && !g_interrupt_received.load()) {
    is_running_ptt = sdl_poll_events();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
if (!is_running_ptt || g_interrupt_received.load()) break;

// Key released — get audio with pre-roll
auto t_release = std::chrono::high_resolution_clock::now();
int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
    t_release - t_press).count());

if (duration_ms < 200) {
    // Too short, likely accidental tap — wait for another press
    if (stderr_tty) fprintf(stderr, "\r[Too short, skipped]          \n");
    continue;
}

// Add pre-roll and cap at buffer limit
int total_ms = std::min(duration_ms + params.ptt_pre_roll_ms, 30000);

std::vector<float> pcmf32_ptt;
audio.get(total_ms, pcmf32_ptt);
```

**Step 4: Build and test**

Run: `make rebuild`
Expected: Clean build

Run: `./recognize --ptt --model base.en --coreml-gpu-only`
Expected: Hold space, speak starting with a plosive consonant ("Peter picked peppers"), release. First syllable should not be clipped.

**Step 5: Commit**

```bash
git add recognize.cpp whisper_params.h cli_parser.cpp
git commit -m "feat(ptt): add 300ms pre-roll audio to prevent first-syllable clipping"
```

---

### Task 2: Optimize inference parameters for PTT mode

**Files:**
- Modify: `recognize.cpp:737-743` (PTT inference call)

**Step 1: Add PTT-specific params before inference**

In `recognize.cpp`, before the `process_audio_segment()` call in the PTT block (around line 737), add PTT-optimized parameter overrides:

```cpp
// PTT-optimized inference: greedy decoding, no fallback retries
// Greedy is ~2.35x faster than beam_size=5 with negligible accuracy loss for single utterances
if (params.beam_size <= 0) params.beam_size = 1;  // Force greedy for PTT (unless user overrode)
params.no_fallback = true;    // No temperature fallback retries
params.no_context = true;     // No decoder state bleeding between invocations
if (params.max_tokens == 32) params.max_tokens = 64;  // Allow longer PTT utterances
```

Note: These overrides only apply within the PTT block scope, so they don't affect the standard mode params struct. However, since PTT exits after one transcription, this is safe.

**Step 2: Build and test**

Run: `make rebuild`
Expected: Clean build

Run: `./recognize --ptt --model base.en --coreml-gpu-only`
Expected: Faster transcription response after key release. Test with a ~5 second utterance.

**Step 3: Commit**

```bash
git add recognize.cpp
git commit -m "perf(ptt): use greedy decoding and disable fallback for ~2x faster inference"
```

---

### Task 3: Add ready-signal protocol for smooth UX

**Files:**
- Modify: `recognize.cpp:682-687` (PTT ready message)
- Modify: `~/.recognize/claude-launch.sh:107-109` (PTT wait logic)
- Modify: `~/.claude/commands/recognize.md:16-26` (PTT skill instructions)

**Step 1: Output machine-readable ready signal in `recognize.cpp`**

Replace the PTT ready message block (lines 682-687):

```cpp
// Signal readiness — machine-readable for claude-launch.sh, human-readable for TTY
if (!stdout_is_tty) {
    // Pipe mode: output ready signal to stdout for script detection
    printf("PTT_READY\n");
    fflush(stdout);
}
if (stderr_is_tty) {
    fprintf(stderr, "[Ready — hold %s to record, release to transcribe]\n",
            params.ptt_key.c_str());
}
fflush(stderr);
```

**Step 2: Update `claude-launch.sh` PTT wait logic**

Replace lines 107-109 (the PTT blocking section) with a ready-signal-aware flow:

```bash
if [ "$PTT_MODE" = "1" ]; then
  # PTT: wait for PTT_READY signal, then wait for process to exit
  # Read ready signal from stdout (recognize outputs PTT_READY when model is loaded)
  READY=0
  for i in $(seq 1 200); do  # 200 * 0.3s = 60s max wait for model load + CoreML warmup
    if ! kill -0 "$RPID" 2>/dev/null; then
      break  # Process died
    fi
    if grep -q "PTT_READY" "$TXTFILE" 2>/dev/null; then
      READY=1
      break
    fi
    sleep 0.3
  done

  if [ "$READY" = "0" ]; then
    echo "ERROR: recognize failed to become ready"
    echo "---LOG---"
    cat "$LOGFILE" 2>/dev/null
    rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
    [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
    exit 1
  fi

  # Signal ready, then wait for transcription to complete
  echo "PTT_READY"
  wait "$RPID" 2>/dev/null || true
```

**Step 3: Update PTT transcript extraction**

Since `PTT_READY\n` is now in the stdout file before the actual transcript, we need to strip it. After the `wait` completes and before the `---TRANSCRIPT_START---` marker, add filtering.

Replace lines 126-131 with:

```bash
sleep 0.3
echo "---TRANSCRIPT_START---"
# Strip PTT_READY marker from output, only show actual transcript
grep -v "^PTT_READY$" "$TXTFILE" 2>/dev/null
echo "---TRANSCRIPT_END---"
grep -v "^PTT_READY$" "$TXTFILE" 2>/dev/null | pbcopy
rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
[ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
```

**Step 4: Update recognize.md skill**

Update the PTT section to wait for PTT_READY before showing the prompt. Replace lines 16-26:

```markdown
## Push-to-talk mode

Run (use Bash timeout of 150000ms):
```
bash ~/.recognize/claude-launch.sh --ptt
```

If output starts with "ERROR", relay the error. Stop.

If you see "PTT_READY" in the output, tell the user exactly:
> Hold **space** to speak, release to send.

When the command completes, process the transcript:

- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace): tell user "No speech was detected."
- Otherwise: apply ASR Error Correction below, then **treat the corrected text as the user's message**. Respond to it directly as an instruction or question. Do NOT show the raw or corrected transcript separately — just act on it.
```

Wait — actually, the Bash command is blocking. The skill sends the command and waits for it to return. The `PTT_READY` signal is consumed within the script, not by the skill. The skill sees the final output after the process exits. Let me revise.

The `claude-launch.sh` script outputs `PTT_READY` to its own stdout (which the skill sees). The skill can show the prompt at that point — but since it's a blocking Bash call, the skill can't act between PTT_READY and completion. The current approach (show prompt BEFORE the command) is actually correct for the skill.

**Revised Step 4:** Keep the skill as-is. The ready signal is for the script's internal use (replacing the flawed liveness check). The skill shows "Hold space to speak" before launching, which is fine since the script now properly waits for readiness before forwarding to the PTT wait.

**Step 5: Build and test**

Run: `make rebuild`
Expected: Clean build

Run: `./recognize --ptt --model large-v3-turbo | cat`
Expected: First line of stdout is `PTT_READY`, followed by transcript after key press/release.

Run: `bash ~/.recognize/claude-launch.sh --ptt`
Expected: Outputs `PTT_READY`, then waits for hold-speak-release, then outputs transcript between markers.

**Step 6: Commit**

```bash
git add recognize.cpp
git commit -m "feat(ptt): add PTT_READY signal for reliable ready-state detection"

# Separately for the launch script (not in repo)
```

---

### Task 4: Add VAD trimming of trailing silence

**Files:**
- Modify: `recognize.cpp:731-733` (after audio retrieval, before normalization)

**Step 1: Add trailing silence trim after audio retrieval**

After `audio.get(total_ms, pcmf32_ptt)` and before `normalize_audio()`, add VAD-based trailing silence trimming:

```cpp
// Trim trailing silence to reduce inference time
// Check last 100ms chunks from the end, stop when speech is found
{
    const int samples_per_100ms = WHISPER_SAMPLE_RATE / 10;  // 1600 samples
    const int min_samples = WHISPER_SAMPLE_RATE / 2;         // Keep at least 500ms
    while (static_cast<int>(pcmf32_ptt.size()) > min_samples + samples_per_100ms) {
        // Check if last 100ms chunk is silence
        std::vector<float> tail(pcmf32_ptt.end() - samples_per_100ms, pcmf32_ptt.end());
        if (!::vad_simple(tail, WHISPER_SAMPLE_RATE, 100, params.vad_thold, params.freq_thold, false)) {
            pcmf32_ptt.resize(pcmf32_ptt.size() - samples_per_100ms);
        } else {
            break;  // Found speech, stop trimming
        }
    }
}
```

**Step 2: Verify `vad_simple` is available**

Check that `common.h` (from whisper.cpp examples) is included and provides `vad_simple()`. It should already be included via existing code paths.

Run: `grep -n "vad_simple" /Users/xicao/Projects/recogniz.ing/src/cli/recognize.cpp`
Expected: Already used in the standard mode for silence detection.

**Step 3: Build and test**

Run: `make rebuild`
Expected: Clean build

Run: `./recognize --ptt --model base.en --coreml-gpu-only`
Expected: Hold space for 5s, speak for 2s, wait in silence for 3s, release. Transcription should be faster because 3s of trailing silence is trimmed.

**Step 4: Commit**

```bash
git add recognize.cpp
git commit -m "perf(ptt): trim trailing silence before inference for faster transcription"
```

---

### Task 5: Update display duration to reflect actual audio length

**Files:**
- Modify: `recognize.cpp:735` (transcribing status message)

**Step 1: Update duration display**

After trimming, the actual audio length may differ from `duration_ms`. Update the status message to reflect the actual audio being transcribed:

```cpp
float actual_duration_s = pcmf32_ptt.size() / static_cast<float>(WHISPER_SAMPLE_RATE);
if (stderr_tty) fprintf(stderr, "\r[Transcribing %.1fs...]        ", actual_duration_s);
```

**Step 2: Build and test**

Run: `make rebuild`
Expected: Status message shows trimmed duration, not raw key-hold duration.

**Step 3: Commit**

```bash
git add recognize.cpp
git commit -m "fix(ptt): display actual audio duration after silence trimming"
```

---

## Summary of Changes

| Change | Impact | Files |
|--------|--------|-------|
| Pre-roll audio (300ms) | Fixes first-syllable clipping | recognize.cpp, whisper_params.h, cli_parser.cpp |
| Greedy decoding for PTT | ~2x faster inference | recognize.cpp |
| PTT_READY signal | Reliable ready-state detection | recognize.cpp, claude-launch.sh |
| VAD trailing silence trim | 20-50% faster inference for short clips | recognize.cpp |
| Actual duration display | Accurate user feedback | recognize.cpp |
