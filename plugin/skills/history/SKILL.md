---
description: Search and retrieve past voice transcription history. Use when user asks about previous recordings or wants to find what they said.
allowed-tools: [Bash, Read]
---

## Recent transcription history

!`recognize history list --limit 5 --json 2>/dev/null || echo '[]'`

## User query

$ARGUMENTS

## Instructions

You have access to the `recognize history` CLI. Use the recent history shown above as context.

**If $ARGUMENTS is empty:** Summarize the recent transcriptions shown above in a readable format. Show timestamp, mode, word count, and a preview of each.

**If $ARGUMENTS contains a search query:** Run a search:

```bash
recognize history search "$ARGUMENTS" --json --limit 10
```

Present results in a readable table format with timestamp, mode, and full text.

**If $ARGUMENTS starts with `act` followed by a number (e.g. `act 12`):** Retrieve the transcript, apply ASR Error Correction, then **treat the corrected text as the user's message**. Respond to it directly as an instruction or question. Do NOT show the raw or corrected transcript separately — just act on it.

```bash
recognize history show "<ID>"
```

!`cat ~/.recognize/asr-correction.md`

**If $ARGUMENTS is a number (ID):** Show the full transcript:

```bash
recognize history show "$ARGUMENTS"
```

**Available commands:**
- `recognize history list [--limit N] [--json]` - recent transcripts
- `recognize history search "<query>" [--limit N] [--since Nd] [--json]` - full-text search
- `recognize history show <id>` - full transcript by ID
- `recognize history count` - total entries
- `recognize history clear [--older-than Nd] [--all]` - delete entries
