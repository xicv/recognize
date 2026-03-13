---
description: Start meeting recording with large-v3-turbo model and speaker tracking.
allowed-tools: [Bash, Read]
---

Run:
```
bash ~/.recognize/claude-launch.sh --meeting
```

If output is "OK_MEETING", say exactly:
> Meeting recording started (large-v3-turbo model, meeting mode enabled).
> When finished, run `/recognize:stop` to end and generate the meeting summary.

If output starts with "ERROR", relay the error. Stop.
