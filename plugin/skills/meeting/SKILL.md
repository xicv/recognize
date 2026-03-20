---
description: Start meeting recording with large-v3-turbo model and speaker tracking.
allowed-tools: [Bash, Read]
---

Run:
```
bash ~/.recognize/claude-launch.sh --meeting
```

If output is "OK_MEETING", say exactly:
> Meeting recording started (large-v3-turbo model, speaker tracking enabled).
> When finished, run `/recognize:stop` to end recording and generate a meeting summary.
>
> **Tip:** The meeting will be auto-summarized with action items. For long meetings (>1 hour), multi-pass summarization ensures nothing is missed.

If output starts with "ERROR", relay the error. Stop.
