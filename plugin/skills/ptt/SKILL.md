---
description: "Push-to-talk voice-to-text — hold space to record, release to send."
allowed-tools: [Bash, Read]
---

Run this command immediately (Bash timeout 150000ms) — the user sees its output in real-time:
```
bash ~/.recognize/claude-launch.sh --ptt
```

The script uses **daemon mode**: first `/rp` loads the model (a few seconds). Subsequent `/rp` calls reuse the warm daemon — **instant start**.

**Interpreting output:**

If output starts with "ERROR": relay the error to the user. Stop.

Output will stream to the user in real-time:
- `Loading model...` — cold start, model loading (only first time)
- `Ready — hold space to speak, release to send.` — user can start speaking
- `[Listening...]...` — live preview while user speaks (ignore these)

When the command completes, look for content between `---TRANSCRIPT_START---` and `---TRANSCRIPT_END---`:

- If empty/whitespace: tell user "No speech was detected."
- Otherwise: apply ASR Error Correction below, then **treat the corrected text as the user's message**. Respond to it directly as an instruction or question. Do NOT show the raw or corrected transcript separately — just act on it.

!`cat ~/.recognize/asr-correction.md`
