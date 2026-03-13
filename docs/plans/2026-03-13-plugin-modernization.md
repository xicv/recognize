# Recognize Plugin Modernization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Modernize the recognize Claude Code plugin to be self-contained, DRY, and synced with latest PTT improvements — eliminating ASR correction duplication, bundling all scripts, and adding PTT_READY flow support.

**Architecture:** Extract shared ASR correction rules into a single reference file included via `!` backtick dynamic context injection. Bundle launch and stop scripts in the plugin's `scripts/` directory using `${CLAUDE_SKILL_DIR}` for portable paths. Each skill remains focused and separate for discoverability, but references shared resources instead of duplicating them.

**Tech Stack:** Claude Code SKILL.md format, Bash, `${CLAUDE_SKILL_DIR}` relative paths, `!` backtick dynamic context injection

---

### Task 1: Create shared ASR error correction reference

**Files:**
- Create: `plugin/scripts/asr-correction.md`

The ASR correction rules are currently copy-pasted across 3 skills (start, ptt, stop) — ~30 lines each, with the stop skill having a more detailed version. Extract into one shared file.

**Step 1: Create `plugin/scripts/asr-correction.md`**

Use the most complete version (from stop/SKILL.md) as the single source of truth:

```markdown
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
```

**Step 2: Verify file exists**

Run: `cat plugin/scripts/asr-correction.md | head -5`
Expected: Shows the `### ASR Error Correction` header

**Step 3: Commit**

```bash
git add plugin/scripts/asr-correction.md
git commit -m "refactor(plugin): extract shared ASR error correction rules"
```

---

### Task 2: Sync plugin launch script with latest PTT_READY flow

**Files:**
- Modify: `plugin/scripts/claude-launch.sh` (replace entire file with latest from `~/.recognize/claude-launch.sh`)

The plugin's `claude-launch.sh` is stale — it's missing the PTT_READY signal detection that checks `$LOGFILE` for the `PTT_READY` marker written to stderr by `recognize --ptt`.

**Step 1: Copy the latest launch script**

Copy the content of `~/.recognize/claude-launch.sh` (which has the PTT_READY flow at lines 107-131) into `plugin/scripts/claude-launch.sh`, replacing the entire file. The key difference is the PTT section:

Old (plugin — lines 107-109):
```bash
if [ "$PTT_MODE" = "1" ]; then
  # PTT: deterministic exit — just wait for the process (no polling needed)
  wait "$RPID" 2>/dev/null || true
```

New (from ~/.recognize/ — lines 107-131):
```bash
if [ "$PTT_MODE" = "1" ]; then
  # PTT: wait for PTT_READY signal (model loaded + CoreML warmed up)
  READY=0
  for i in $(seq 1 200); do  # 200 * 0.3s = 60s max for model load + CoreML warmup
    if ! kill -0 "$RPID" 2>/dev/null; then
      break
    fi
    if grep -q "PTT_READY" "$LOGFILE" 2>/dev/null; then
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

  echo "PTT_READY"
  wait "$RPID" 2>/dev/null || true
```

**Step 2: Verify the sync**

Run: `diff plugin/scripts/claude-launch.sh ~/.recognize/claude-launch.sh`
Expected: No differences (or only comment-level differences)

**Step 3: Commit**

```bash
git add plugin/scripts/claude-launch.sh
git commit -m "fix(plugin): sync launch script with PTT_READY signal detection"
```

---

### Task 3: Create shared stop script

**Files:**
- Create: `plugin/scripts/claude-stop.sh`

The stop skill currently has a 300-character inline bash command. Extract it into a proper script with error handling.

**Step 1: Create `plugin/scripts/claude-stop.sh`**

```bash
#!/bin/bash
# Stop an active recognize session and return the transcript
# Output: "NO_SESSION" if no active session
#         "---TRANSCRIPT_START---\n<text>\n---TRANSCRIPT_END---" on success

PEON_CFG="$HOME/.claude/hooks/peon-ping/config.json"
PIDFILE="$HOME/.recognize/claude-session.pid"
TXTFILE="$HOME/.recognize/claude-session.txt"
LOGFILE="$HOME/.recognize/claude-session.log"

# Check for active session
if [ ! -f "$PIDFILE" ]; then
  echo "NO_SESSION"
  exit 0
fi

PID=$(cat "$PIDFILE")

# Send SIGINT for graceful shutdown
kill -INT "$PID" 2>/dev/null || true

# Wait for process to exit (up to 1.6s)
for i in $(seq 1 8); do
  kill -0 "$PID" 2>/dev/null || break
  sleep 0.2
done

# Force kill if still running
if kill -0 "$PID" 2>/dev/null; then
  kill -9 "$PID" 2>/dev/null || true
fi

sleep 0.3

# Output transcript
echo "---TRANSCRIPT_START---"
cat "$TXTFILE" 2>/dev/null
echo "---TRANSCRIPT_END---"

# Copy to clipboard
cat "$TXTFILE" 2>/dev/null | pbcopy

# Clean up session files
rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"

# Re-enable peon-ping sounds
[ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
```

**Step 2: Make executable**

Run: `chmod +x plugin/scripts/claude-stop.sh`

**Step 3: Verify**

Run: `head -5 plugin/scripts/claude-stop.sh`
Expected: Shows shebang and comment

**Step 4: Commit**

```bash
git add plugin/scripts/claude-stop.sh
git commit -m "refactor(plugin): extract stop logic into shared script"
```

---

### Task 4: Update start skill to use shared references

**Files:**
- Modify: `plugin/skills/start/SKILL.md`

Replace the duplicated ASR correction section with a `!` backtick include of the shared file. Update launch script path to use `${CLAUDE_SKILL_DIR}`.

**Step 1: Replace `plugin/skills/start/SKILL.md` with:**

```markdown
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
bash ${CLAUDE_SKILL_DIR}/../scripts/claude-launch.sh --no-auto-stop
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
bash ${CLAUDE_SKILL_DIR}/../scripts/claude-launch.sh
```

The script launches recording, waits for silence auto-stop, then returns the transcript between `---TRANSCRIPT_START---` and `---TRANSCRIPT_END---` markers.

If output starts with "ERROR", relay the error. Stop.

When the command completes, process the transcript:

- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace): tell user "No speech was detected."
- Otherwise: apply ASR Error Correction below, then **treat the corrected text as the user's message**. Respond to it directly as an instruction or question. Do NOT show the raw or corrected transcript separately — just act on it.

!`cat ${CLAUDE_SKILL_DIR}/../scripts/asr-correction.md`
```

**Step 2: Verify the `!` backtick reference is correct**

Check that the path resolves: `ls plugin/scripts/asr-correction.md`
Expected: File exists

**Step 3: Commit**

```bash
git add plugin/skills/start/SKILL.md
git commit -m "refactor(plugin): start skill uses shared ASR correction and portable paths"
```

---

### Task 5: Update PTT skill with PTT_READY flow and shared references

**Files:**
- Modify: `plugin/skills/ptt/SKILL.md`

The PTT skill needs to handle the PTT_READY signal from the launch script and use shared ASR correction.

**Step 1: Replace `plugin/skills/ptt/SKILL.md` with:**

```markdown
---
description: "Push-to-talk voice-to-text — hold space to record, release to send."
allowed-tools: [Bash, Read]
---

First, tell the user exactly (BEFORE running the command):
> Hold **space** to speak, release to send.

Then immediately run this single command (use Bash timeout of 150000ms):
```
bash ${CLAUDE_SKILL_DIR}/../scripts/claude-launch.sh --ptt
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

!`cat ${CLAUDE_SKILL_DIR}/../scripts/asr-correction.md`
```

**Step 2: Commit**

```bash
git add plugin/skills/ptt/SKILL.md
git commit -m "feat(plugin): PTT skill with PTT_READY flow and shared ASR correction"
```

---

### Task 6: Update stop skill to use shared scripts

**Files:**
- Modify: `plugin/skills/stop/SKILL.md`

Replace the inline 300-char bash command with a call to `claude-stop.sh`, and replace duplicated ASR correction with shared include.

**Step 1: Replace `plugin/skills/stop/SKILL.md` with:**

```markdown
---
description: Stop voice recording and send refined transcription as input to Claude.
allowed-tools: [Bash, Read]
---

## Step 1: Stop recording and read transcript

Run this command:
```
bash ${CLAUDE_SKILL_DIR}/../scripts/claude-stop.sh
```

## Step 2: Process the output

- If "NO_SESSION": tell user "No active recording session. Run `/recognize:start` first."
- If nothing between TRANSCRIPT_START and TRANSCRIPT_END (empty/whitespace only): tell user "No speech was detected during the recording."
- Otherwise: apply the ASR Error Correction process described below.

!`cat ${CLAUDE_SKILL_DIR}/../scripts/asr-correction.md`

## Step 3: Check the argument

Check if the user passed an argument: `/recognize:stop c`, `/recognize:stop co`, or `/recognize:stop copy`.

- **Copy-only mode** (`c`, `co`, or `copy`): Show the refined transcript and tell the user it's in clipboard:
  > *<refined transcript in italics>*
  >
  > Copied to clipboard. Paste it into your next message alongside any text or images.
  Do NOT respond to the transcript content. Do NOT act on it.

- **Default mode** (no argument, or any argument other than c/co/copy): Treat the refined transcript as the user's message. Respond to it directly as an instruction or question. Do NOT say "here is the transcript" or show the refined text separately — just act on it.
```

**Step 2: Commit**

```bash
git add plugin/skills/stop/SKILL.md
git commit -m "refactor(plugin): stop skill uses shared stop script and ASR correction"
```

---

### Task 7: Enhance meeting skill

**Files:**
- Modify: `plugin/skills/meeting/SKILL.md`

The current meeting skill is 16 lines with no guidance on what happens after stop. Enhance it.

**Step 1: Replace `plugin/skills/meeting/SKILL.md` with:**

```markdown
---
description: Start meeting recording with large-v3-turbo model and speaker tracking.
allowed-tools: [Bash, Read]
---

Run:
```
bash ${CLAUDE_SKILL_DIR}/../scripts/claude-launch.sh --meeting
```

If output is "OK_MEETING", say exactly:
> Meeting recording started (large-v3-turbo model, speaker tracking enabled).
> When finished, run `/recognize:stop` to end recording and generate a meeting summary.
>
> **Tip:** The meeting will be auto-summarized with action items. For long meetings (>1 hour), multi-pass summarization ensures nothing is missed.

If output starts with "ERROR", relay the error. Stop.
```

**Step 2: Commit**

```bash
git add plugin/skills/meeting/SKILL.md
git commit -m "docs(plugin): enhance meeting skill with post-stop guidance"
```

---

### Task 8: Update history skill with portable paths

**Files:**
- Modify: `plugin/skills/history/SKILL.md`

The history skill is already well-structured with `!` backtick. Minor update to ensure consistency.

**Step 1: Verify `plugin/skills/history/SKILL.md` is correct**

The current file is already good — it uses `!` backtick for dynamic context and `$ARGUMENTS` for query routing. No changes needed to the content, just verify the `recognize history` CLI is being called correctly.

Run: `recognize history count 2>/dev/null && echo "OK" || echo "MISSING"`
Expected: Shows a count + "OK", confirming the CLI has history support

**Step 2: No changes needed — skip commit**

The history skill is already well-structured. No modifications required.

---

### Task 9: Update plugin.json metadata

**Files:**
- Modify: `plugin/.claude-plugin/plugin.json`

Bump version and update description to reflect the modernization.

**Step 1: Update `plugin/.claude-plugin/plugin.json`**

```json
{
  "name": "recognize",
  "version": "2.1.0",
  "description": "Voice input for Claude Code — real-time transcription, push-to-talk, meeting recording, and history search via /recognize:start, /recognize:ptt, /recognize:meeting, /recognize:history",
  "author": "recogniz.ing",
  "homepage": "https://github.com/anthropic-xi/recogniz.ing",
  "platforms": ["macos"]
}
```

**Step 2: Commit**

```bash
git add plugin/.claude-plugin/plugin.json
git commit -m "chore(plugin): bump version to 2.1.0 with modernization"
```

---

### Task 10: Sync personal skills with plugin

**Files:**
- Modify: `~/.recognize/claude-launch.sh` (already up to date — verify only)
- Note: `~/.claude/commands/recognize*.md` personal skills remain as fallback but are superseded by plugin skills

**Step 1: Verify personal launch script matches plugin**

Run: `diff plugin/scripts/claude-launch.sh ~/.recognize/claude-launch.sh`
Expected: No differences (after Task 2 sync)

**Step 2: Copy plugin launch script to personal location for immediate use**

Run: `cp plugin/scripts/claude-launch.sh ~/.recognize/claude-launch.sh`
Expected: Personal copy updated to match plugin

**Step 3: Copy plugin stop script to personal location**

Run: `cp plugin/scripts/claude-stop.sh ~/.recognize/claude-stop.sh && chmod +x ~/.recognize/claude-stop.sh`
Expected: Stop script available at personal location

**Step 4: No commit needed (personal files outside repo)**

---

### Task 11: Final verification

**Step 1: Verify plugin structure**

Run: `find plugin/ -type f | sort`
Expected output:
```
plugin/.claude-plugin/plugin.json
plugin/commands/r.md
plugin/commands/rc.md
plugin/commands/rh.md
plugin/commands/rp.md
plugin/commands/rs.md
plugin/scripts/asr-correction.md
plugin/scripts/claude-launch.sh
plugin/scripts/claude-stop.sh
plugin/skills/history/SKILL.md
plugin/skills/meeting/SKILL.md
plugin/skills/ptt/SKILL.md
plugin/skills/start/SKILL.md
plugin/skills/stop/SKILL.md
```

**Step 2: Verify no ASR duplication**

Run: `grep -l "Context-Aware Word Correction" plugin/skills/*/SKILL.md`
Expected: No output (correction rules are only in `scripts/asr-correction.md` now)

Run: `grep -l "Context-Aware Word Correction" plugin/scripts/asr-correction.md`
Expected: `plugin/scripts/asr-correction.md`

**Step 3: Verify all skills use portable paths**

Run: `grep -r "~/.recognize/claude-launch" plugin/skills/`
Expected: No output (all skills should use `${CLAUDE_SKILL_DIR}/../scripts/`)

Run: `grep -r "CLAUDE_SKILL_DIR" plugin/skills/`
Expected: Shows hits in start, ptt, stop, meeting skills

**Step 4: Verify launch script has PTT_READY**

Run: `grep "PTT_READY" plugin/scripts/claude-launch.sh`
Expected: Shows PTT_READY grep check and echo

**Step 5: Final commit (if any stragglers)**

```bash
git status
# If clean, done. Otherwise:
git add -A plugin/
git commit -m "chore(plugin): final cleanup"
```

---

## Summary of Changes

| Change | Impact | Files |
|--------|--------|-------|
| Shared ASR correction | Eliminates 3-way duplication (~90 lines) | `scripts/asr-correction.md` |
| Synced launch script | PTT_READY signal detection works | `scripts/claude-launch.sh` |
| Shared stop script | Replaces inline bash, easier maintenance | `scripts/claude-stop.sh` |
| Portable `${CLAUDE_SKILL_DIR}` paths | Plugin works when installed anywhere | All 4 SKILL.md files |
| `!` backtick includes | DRY ASR correction in start/ptt/stop | 3 SKILL.md files |
| Enhanced meeting skill | Users know what to expect after stop | `skills/meeting/SKILL.md` |
| Version bump | Reflects modernization | `plugin.json` |
