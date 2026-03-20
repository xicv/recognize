---
description: Stop voice recording and send refined transcription as input to Claude.
allowed-tools: [Bash, Read]
---

## Step 1: Stop recording and read transcript

Run this command:
```
bash ~/.recognize/claude-stop.sh
```

## Step 2: Process the output

- If "NO_SESSION": tell user "No active recording session. Run `/recognize:start` first."
- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace only): tell user "No speech was detected during the recording."
- Otherwise: apply the ASR Error Correction process described below.

!`cat ~/.recognize/asr-correction.md`

## Step 3: Check the argument

Check if the user passed an argument: `/recognize:stop c`, `/recognize:stop co`, or `/recognize:stop copy`.

- **Copy-only mode** (`c`, `co`, or `copy`): Show the refined transcript and tell the user it's in clipboard:
  > *<refined transcript in italics>*
  >
  > Copied to clipboard. Paste it into your next message alongside any text or images.
  Do NOT respond to the transcript content. Do NOT act on it.

- **Default mode** (no argument, or any argument other than c/co/copy): Treat the refined transcript as the user's message. Respond to it directly as an instruction or question. Do NOT say "here is the transcript" or show the refined text separately — just act on it.
