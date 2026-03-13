#!/bin/bash
# Launcher script for recognize in Claude Code sessions
# Voice input: base.en (148MB, ~5% WER, fast with CoreML)
# PTT mode: large-v3-turbo (1.5GB, ~2.5% WER, hold-to-talk, single-shot)
# Meeting mode: large-v3-turbo (1.5GB, ~2.5% WER, high accuracy)
# Usage: bash claude-launch.sh [--meeting] [--no-auto-stop] [--ptt]

set -e

PEON_CFG="$HOME/.claude/hooks/peon-ping/config.json"
PIDFILE="$HOME/.recognize/claude-session.pid"
TXTFILE="$HOME/.recognize/claude-session.txt"
LOGFILE="$HOME/.recognize/claude-session.log"
MEETING_MODE=0
NO_AUTO_STOP=0
PTT_MODE=0

# Parse arguments
for arg in "$@"; do
  case "$arg" in
    --meeting|-m|meeting) MEETING_MODE=1 ;;
    --no-auto-stop) NO_AUTO_STOP=1 ;;
    --ptt|-p|ptt) PTT_MODE=1 ;;
  esac
done

# Mute peon-ping sounds (non-blocking)
[ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": true/"enabled": false/' "$PEON_CFG"

# Check recognize is installed
command -v recognize &>/dev/null || { echo "ERROR: recognize not installed"; exit 1; }

# Check for active session (PID file exists and process alive)
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
  echo "ERROR: session already active, run /recognize:stop first"
  exit 1
fi

# Kill stale recognize processes only if they exist
if pgrep -q recognize 2>/dev/null; then
  pkill -INT recognize 2>/dev/null || true
  sleep 0.2
  pkill -9 recognize 2>/dev/null || true
fi

# Clean up stale files from previous sessions
rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"

# Build recognize command based on mode
if [ "$MEETING_MODE" = "1" ]; then
  # Meeting mode: large-v3-turbo for accuracy, meeting features enabled
  RECOGNIZE_CMD="recognize --no-export --no-timestamps --meeting --model large-v3-turbo"
elif [ "$PTT_MODE" = "1" ]; then
  # PTT mode: large-v3-turbo for accuracy, single-shot (hold key → speak → release → done)
  RECOGNIZE_CMD="recognize --ptt --no-export --no-timestamps --model large-v3-turbo"
else
  # Voice input: base.en — best speed/accuracy balance (5% WER, <1s startup with CoreML)
  # --coreml-gpu-only skips ANE compilation for instant startup
  RECOGNIZE_CMD="recognize --no-export --no-timestamps --model base.en --output-mode original --coreml-gpu-only"
  if [ "$NO_AUTO_STOP" = "0" ]; then
    RECOGNIZE_CMD="$RECOGNIZE_CMD --silence-timeout 5"
  fi
fi

# Launch recognize in background
nohup $RECOGNIZE_CMD > "$TXTFILE" 2>"$LOGFILE" &
RPID=$!
echo "$RPID" > "$PIDFILE"

# Liveness check — large models need longer (CoreML warm-up)
if [ "$MEETING_MODE" = "1" ] || [ "$PTT_MODE" = "1" ]; then
  MAX_CHECKS=12  # up to 3.6s for large model
else
  MAX_CHECKS=2   # up to 0.6s for base.en
fi

ALIVE=0
for i in $(seq 1 $MAX_CHECKS); do
  sleep 0.3
  if kill -0 "$RPID" 2>/dev/null; then
    ALIVE=1
    break
  fi
done

if [ "$ALIVE" = "0" ]; then
  echo "ERROR: failed to start"
  echo "---LOG---"
  cat "$LOGFILE" 2>/dev/null
  exit 1
fi

if [ "$MEETING_MODE" = "1" ]; then
  echo "OK_MEETING"
  exit 0
fi

if [ "$NO_AUTO_STOP" = "1" ]; then
  echo "OK"
  exit 0
fi

# ─── Blocking modes: wait for recognize to exit, return transcript ───

# PTT mode: recognize auto-exits after single transcription, just wait
# Auto-stop mode: recognize auto-exits after silence timeout
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
else
  # Auto-stop: poll with safety net (100s max)
  echo "OK_WAITING"
  for i in $(seq 1 200); do
    kill -0 "$RPID" 2>/dev/null || break
    sleep 0.5
  done

  # Force stop if still running
  if kill -0 "$RPID" 2>/dev/null; then
    kill -INT "$RPID" 2>/dev/null || true
    sleep 0.5
    kill -0 "$RPID" 2>/dev/null && kill -9 "$RPID" 2>/dev/null
  fi
fi

sleep 0.3
echo "---TRANSCRIPT_START---"
cat "$TXTFILE" 2>/dev/null
echo "---TRANSCRIPT_END---"
cat "$TXTFILE" 2>/dev/null | pbcopy
rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
[ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
