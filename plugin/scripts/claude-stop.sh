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
