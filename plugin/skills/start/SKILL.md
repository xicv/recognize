---
description: Start voice-to-text recording. Default mode auto-stops after silence and sends transcript directly.
allowed-tools: [Bash, Read]
---

Check the user's argument: `$ARGUMENTS`

Determine the mode:
- `/recognize:start` (no args) — **auto-stop mode** (default)
- `/recognize:start c` or `/recognize:start continue` — continuous mode (manual stop)

---

## Continuous mode

Run:
```
bash ~/.recognize/claude-launch.sh --no-auto-stop
```

If output is "OK", say exactly:
> Recording started (continuous). Speak now.
> Run `/recognize:stop` to stop and send.

If output starts with "ERROR", relay the error. Stop.

---

## Auto-stop mode (default)

First, tell the user exactly (BEFORE running the command):
> Speak now. Recording will auto-stop after 5 seconds of silence.

Then immediately run this single command (use Bash timeout of 150000ms):
```
bash ~/.recognize/claude-launch.sh
```

The script launches recording, waits for silence auto-stop, then returns the transcript between `---TRANSCRIPT_START---` and `---TRANSCRIPT_END---` markers.

If output starts with "ERROR", relay the error. Stop.

When the command completes, process the transcript:

- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace): tell user "No speech was detected."
- Otherwise: apply ASR Error Correction below, then **treat the corrected text as the user's message**. Respond to it directly as an instruction or question. Do NOT show the raw or corrected transcript separately — just act on it.

!`cat ~/.recognize/asr-correction.md`
