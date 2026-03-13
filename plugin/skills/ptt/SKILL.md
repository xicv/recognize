---
description: "Push-to-talk voice-to-text — hold space to record, release to send."
allowed-tools: [Bash, Read]
---

First, tell the user exactly (BEFORE running the command):
> Hold **space** to speak, release to send.

Then immediately run this single command (use Bash timeout of 150000ms):
```
bash ~/.recognize/claude-launch.sh --ptt
```

The script launches PTT recording, waits for the user to hold space, speak, release, transcribes, then returns the transcript between `---TRANSCRIPT_START---` and `---TRANSCRIPT_END---` markers.

If output starts with "ERROR", relay the error. Stop.

When the command completes, process the transcript:

- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace): tell user "No speech was detected."
- Otherwise: apply ASR Error Correction below, then **treat the corrected text as the user's message**. Respond to it directly as an instruction or question. Do NOT show the raw or corrected transcript separately — just act on it.

### ASR Error Correction

Speech-to-text is lossy. Actively reconstruct the speaker's intent:

1. **Context-Aware Word Correction** (MOST IMPORTANT): For every suspicious word, ask "What word that SOUNDS LIKE this makes sense in context?" The speaker is a developer using Claude Code — technical terms, CLI commands, file paths, and programming concepts are highly likely. Trust semantic coherence over literal transcription.
2. **Word Boundary Errors**: ASR may split/merge words incorrectly — reconstruct based on meaning. Watch for compound technical terms broken apart.
3. **Noise/Artifact Removal**: Remove environmental sounds transcribed as text, repeated phrases from echo, hallucinated phrases generated during silence.
4. **Disfluency Cleanup**: Remove filler words, false starts, stuttering. When speaker self-corrects, keep ONLY the corrected version.
5. **Sentence Reconstruction**: Fix punctuation, capitalization, sentence boundaries. Preserve the speaker's natural voice — do NOT over-formalize.
6. **Preservation** (CRITICAL): Do NOT add content the speaker didn't express. Do NOT remove substantive meaning. When uncertain, prefer the interpretation that makes more sense given the conversation context.
