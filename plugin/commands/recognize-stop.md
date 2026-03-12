---
description: Stop voice recording and send refined transcription as input to Claude.
allowed-tools: [Bash, Read]
---

## Step 1: Stop recording, copy to clipboard, read transcript, and re-enable peon-ping

Run this command:

```
if [ ! -f ~/.recognize/claude-session.pid ]; then echo "NO_SESSION"; exit 0; fi && PID=$(cat ~/.recognize/claude-session.pid) && kill -INT $PID 2>/dev/null; for i in $(seq 1 8); do kill -0 $PID 2>/dev/null || break; sleep 0.2; done; kill -0 $PID 2>/dev/null && kill -9 $PID 2>/dev/null; sleep 0.3; echo "---TRANSCRIPT_START---"; cat ~/.recognize/claude-session.txt 2>/dev/null; echo "---TRANSCRIPT_END---"; cat ~/.recognize/claude-session.txt 2>/dev/null | pbcopy; rm -f ~/.recognize/claude-session.pid ~/.recognize/claude-session.txt ~/.recognize/claude-session.log; PEON_CFG=~/.claude/hooks/peon-ping/config.json; if [ -f "$PEON_CFG" ]; then sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"; fi
```

## Step 2: Process the output

- If "NO_SESSION": tell user "No active recording session. Run /recognize first."
- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace only): tell user "No speech was detected during the recording."
- Otherwise: apply the ASR Error Correction process described below.

### ASR Error Correction

Speech-to-text is lossy. You MUST actively reconstruct the speaker's intent, not pass through raw ASR output. Apply these corrections in order:

#### 1. Context-Aware Word Correction (MOST IMPORTANT)
ASR frequently substitutes phonetically similar but semantically wrong words. For EVERY suspicious or out-of-place word, ask: "What word that SOUNDS LIKE this would make sense in context?"
- Use the **surrounding sentence meaning** and **topic domain** to infer the intended word
- Consider the conversation history — the speaker is a developer using Claude Code, so technical terms, CLI commands, file paths, and programming concepts are highly likely
- When a word doesn't fit the semantic context, find the phonetically closest word that does
- Trust semantic coherence over literal transcription — if a phrase makes no sense as transcribed, it's almost certainly a misrecognition

#### 2. Phonetic and Accent Awareness
Different speakers produce systematic sound substitutions that ASR misinterprets. Watch for:
- **Consonant confusions**: sounds that are close in articulation often get swapped (voiced/unvoiced pairs, similar place of articulation)
- **Vowel shifts**: stressed/unstressed vowels, reduced vowels transcribed as different words
- **Word boundary errors**: ASR may split one word into two, or merge two words into one — reconstruct based on meaning
- **Compound words and technical terms**: multi-part names, CLI flags, and hyphenated terms are often broken apart or misheard as common words

#### 3. Noise and Artifact Removal
- Remove environmental sounds transcribed as text (background noise, music descriptions, animal sounds, etc.)
- Remove ASR artifacts like repeated phrases from audio overlap or echo
- Remove hallucinated phrases that Whisper generates during silence (common with small models)

#### 4. Speech Disfluency Cleanup
- Remove filler words and hesitation markers
- Remove false starts and stuttered words
- When the speaker self-corrects mid-sentence, keep ONLY the corrected version
- Remove repeated words/phrases from natural speech rhythm

#### 5. Sentence Reconstruction
- Reconstruct fragmented or run-on speech into clear, grammatical sentences
- Fix punctuation, capitalization, and sentence boundaries
- Preserve the speaker's natural voice and register — do NOT over-formalize into written prose

#### 6. Preservation Rules (CRITICAL)
- Do NOT add ideas, opinions, or content the speaker did not express
- Do NOT remove substantive content or change the meaning
- Do NOT expand abbreviations or acronyms the speaker used intentionally
- Do NOT over-correct casual speech into formal writing — keep it natural
- When uncertain between two interpretations, prefer the one that makes more sense given the conversation context

## Step 3: Check the argument

Check if the user passed an argument: `/recognize-stop c`, `/recognize-stop co`, or `/recognize-stop copy`.

- **Copy-only mode** (`c`, `co`, or `copy`): Show the refined transcript and tell the user it's in clipboard:
  > *<refined transcript in italics>*
  >
  > Copied to clipboard. Paste it into your next message alongside any text or images.
  Do NOT respond to the transcript content. Do NOT act on it.

- **Default mode** (no argument, or any argument other than c/co/copy): Treat the refined transcript as the user's message. Respond to it directly as an instruction or question. Do NOT say "here is the transcript" or show the refined text separately — just act on it.
