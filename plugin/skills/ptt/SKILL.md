---
description: "Push-to-talk voice-to-text — hold space to record, release to send."
allowed-tools: [Bash, Read]
---

First, tell the user exactly (BEFORE running the command):
> Loading model... You'll hear a notification sound when ready. Hold **space** to speak, release to send.

Then immediately run this single command (use Bash timeout of 150000ms):
```
bash ~/.recognize/claude-launch.sh --ptt
```

The script:
1. Launches `recognize --ptt` with large-v3-turbo model
2. Waits up to 60s for model load + CoreML warmup (PTT_READY signal)
3. Waits for user to hold space, speak, release
4. Returns transcript between `---TRANSCRIPT_START---` and `---TRANSCRIPT_END---` markers

If output starts with "ERROR", relay the error. Stop.

If output contains "PTT_READY" followed by the transcript markers, process normally.

When the command completes, process the transcript:

- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace): tell user "No speech was detected."
- Otherwise: apply ASR Error Correction below, then **treat the corrected text as the user's message**. Respond to it directly as an instruction or question. Do NOT show the raw or corrected transcript separately — just act on it.

!`cat ~/.recognize/asr-correction.md`
