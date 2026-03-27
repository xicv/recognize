#!/bin/bash
# Launcher script for recognize in Claude Code sessions
# Voice input: base.en (148MB, ~5% WER, fast with CoreML)
# PTT mode: large-v3-turbo (1.5GB, ~2.5% WER, hold-to-talk, daemon with --ptt-loop)
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

# ─── PTT daemon mode: reuse warm daemon if available ────────────────────
if [ "$PTT_MODE" = "1" ]; then
  PTT_WARM=0

  # Check if PTT daemon is already running and warm
  if [ -f "$PIDFILE" ]; then
    RPID=$(cat "$PIDFILE" 2>/dev/null)
    if [ -n "$RPID" ] && kill -0 "$RPID" 2>/dev/null && \
       ps -p "$RPID" -o comm= 2>/dev/null | grep -q "recognize"; then
      if grep -q "PTT_READY" "$LOGFILE" 2>/dev/null; then
        PTT_WARM=1
      else
        echo "ERROR: session already active, run /recognize:stop first"
        [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
        exit 1
      fi
    fi
  fi

  if [ "$PTT_WARM" = "1" ]; then
    # ─── Warm start: daemon already loaded, instant ready ───
    # Count existing TRANSCRIPT_DONE markers to detect the next one
    PREV_DONE_COUNT=$(grep -c "TRANSCRIPT_DONE" "$LOGFILE" 2>/dev/null || echo 0)

    # Mark log position for preview filtering (only show new previews)
    LOG_OFFSET=$(wc -l < "$LOGFILE" 2>/dev/null | tr -d ' ')

    echo "PTT_READY"
    printf '\n╔══════════════════════════════════════════════════════╗\n'
    printf '║   ▶  READY — hold SPACE to speak, release to send   ║\n'
    printf '╚══════════════════════════════════════════════════════╝\n\n'

    # Wait for new TRANSCRIPT_DONE (poll log for count increase)
    LAST_PREVIEW=""
    while kill -0 "$RPID" 2>/dev/null; do
      CUR_DONE=$(grep -c "TRANSCRIPT_DONE" "$LOGFILE" 2>/dev/null || echo 0)
      if [ "$CUR_DONE" -gt "$PREV_DONE_COUNT" ]; then
        break
      fi

      # Stream preview lines (only from new log content)
      PREVIEW=$(tail -n +"$((LOG_OFFSET + 1))" "$LOGFILE" 2>/dev/null | grep '^\[PREVIEW' | tail -1 | sed 's/^\[PREVIEW[^]]*\]//')
      if [ -n "$PREVIEW" ] && [ "$PREVIEW" != "$LAST_PREVIEW" ]; then
        echo "[Listening...]$PREVIEW"
        LAST_PREVIEW="$PREVIEW"
      fi

      sleep 0.3
    done

    # Check if daemon died unexpectedly
    if ! kill -0 "$RPID" 2>/dev/null; then
      CUR_DONE=$(grep -c "TRANSCRIPT_DONE" "$LOGFILE" 2>/dev/null || echo 0)
      if [ "$CUR_DONE" -le "$PREV_DONE_COUNT" ]; then
        echo "ERROR: PTT daemon exited unexpectedly"
        rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
        [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
        exit 1
      fi
    fi

    sleep 0.1
    echo "---TRANSCRIPT_START---"
    cat "$TXTFILE" 2>/dev/null
    echo "---TRANSCRIPT_END---"
    cat "$TXTFILE" 2>/dev/null | pbcopy
    # Don't kill daemon or clean up files — keep warm for next /rp
    [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
    exit 0
  fi

  # ─── Cold start: launch new PTT daemon ───
  # Kill stale session from PID file (not all recognize processes)
  if [ -f "$PIDFILE" ]; then
    STALE_PID=$(cat "$PIDFILE" 2>/dev/null)
    if [ -n "$STALE_PID" ] && kill -0 "$STALE_PID" 2>/dev/null; then
      kill -INT "$STALE_PID" 2>/dev/null || true
      sleep 0.2
      kill -0 "$STALE_PID" 2>/dev/null && kill -9 "$STALE_PID" 2>/dev/null || true
    fi
  fi

  rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"

  # Launch with --ptt-loop for daemon mode (stays alive between transcriptions)
  RECOGNIZE_CMD="recognize --ptt-loop --no-export --no-timestamps --model large-v3-turbo"
  nohup $RECOGNIZE_CMD > "$TXTFILE" 2>"$LOGFILE" &
  RPID=$!
  echo "$RPID" > "$PIDFILE"

  # Liveness check
  ALIVE=0
  for i in $(seq 1 12); do
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
    rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
    [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
    exit 1
  fi

  # Wait for PTT_READY signal (model load + CoreML warmup)
  printf '\033[33m⏳ Loading model...\033[0m\n'
  READY=0
  for i in $(seq 1 200); do
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
  echo "Ready — hold space to speak, release to send."

  # Mark log position for preview filtering
  LOG_OFFSET=$(wc -l < "$LOGFILE" 2>/dev/null | tr -d ' ')

  # Wait for first TRANSCRIPT_DONE
  LAST_PREVIEW=""
  while kill -0 "$RPID" 2>/dev/null; do
    if grep -q "TRANSCRIPT_DONE" "$LOGFILE" 2>/dev/null; then
      break
    fi

    # Stream preview lines
    PREVIEW=$(tail -n +"$((LOG_OFFSET + 1))" "$LOGFILE" 2>/dev/null | grep '^\[PREVIEW' | tail -1 | sed 's/^\[PREVIEW[^]]*\]//')
    if [ -n "$PREVIEW" ] && [ "$PREVIEW" != "$LAST_PREVIEW" ]; then
      echo "[Listening...]$PREVIEW"
      LAST_PREVIEW="$PREVIEW"
    fi

    sleep 0.3
  done

  # Check for daemon death before transcript completed
  if ! kill -0 "$RPID" 2>/dev/null; then
    if ! grep -q "TRANSCRIPT_DONE" "$LOGFILE" 2>/dev/null; then
      echo "ERROR: recognize exited before transcription completed"
      rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
      [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
      exit 1
    fi
  fi

  sleep 0.1
  echo "---TRANSCRIPT_START---"
  cat "$TXTFILE" 2>/dev/null
  echo "---TRANSCRIPT_END---"
  cat "$TXTFILE" 2>/dev/null | pbcopy
  # Don't kill daemon or clean up files — keep warm for next /rp
  [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
  exit 0
fi

# ─── Non-PTT modes (unchanged) ─────────────────────────────────────────

# Check for active session (PID file exists and process alive)
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
  # Allow non-PTT launch if a PTT daemon is running (kill it first)
  if grep -q "PTT_READY" "$LOGFILE" 2>/dev/null; then
    RPID=$(cat "$PIDFILE")
    kill -INT "$RPID" 2>/dev/null || true
    sleep 0.3
    kill -0 "$RPID" 2>/dev/null && kill -9 "$RPID" 2>/dev/null || true
    rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
  else
    echo "ERROR: session already active, run /recognize:stop first"
    [ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
    exit 1
  fi
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
if [ "$MEETING_MODE" = "1" ]; then
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

sleep 0.3
echo "---TRANSCRIPT_START---"
cat "$TXTFILE" 2>/dev/null
echo "---TRANSCRIPT_END---"
cat "$TXTFILE" 2>/dev/null | pbcopy
rm -f "$PIDFILE" "$TXTFILE" "$LOGFILE"
[ -f "$PEON_CFG" ] && sed -i '' 's/"enabled": false/"enabled": true/' "$PEON_CFG"
