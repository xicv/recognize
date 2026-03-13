#!/bin/bash
# First-run setup for recognize Claude Code integration
set -e

RECOGNIZE_DIR="$HOME/.recognize"
COMMANDS_DIR="$HOME/.claude/commands"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Check recognize binary
if ! command -v recognize &>/dev/null; then
    echo "ERROR: 'recognize' binary not found in PATH."
    echo ""
    echo "Install it with:"
    echo "  curl -sSL https://raw.githubusercontent.com/anthropic-xi/recogniz.ing/main/src/cli/install.sh | sh"
    echo ""
    echo "Or with Homebrew:"
    echo "  brew tap recognizing/tap && brew install recognize"
    exit 1
fi

# Create directories
mkdir -p "$RECOGNIZE_DIR/models" "$RECOGNIZE_DIR/tmp"
mkdir -p "$COMMANDS_DIR"

# Install launcher script
cp "$SCRIPT_DIR/claude-launch.sh" "$RECOGNIZE_DIR/claude-launch.sh"
chmod +x "$RECOGNIZE_DIR/claude-launch.sh"

# Install short aliases to ~/.claude/commands/ for non-plugin users
# These work without the plugin installed, invoking the skills directly

cat > "$COMMANDS_DIR/r.md" << 'ALIAS'
---
description: Alias for /recognize:start — voice-to-text (auto-stops after silence)
allowed-tools: [Bash, Read]
---

Run the /recognize:start skill with arguments: $ARGUMENTS
ALIAS

cat > "$COMMANDS_DIR/rs.md" << 'ALIAS'
---
description: Alias for /recognize:stop — stop recording and send transcription
allowed-tools: [Bash, Read]
---

Run the /recognize:stop skill with arguments: $ARGUMENTS
ALIAS

cat > "$COMMANDS_DIR/rc.md" << 'ALIAS'
---
description: Alias for /recognize:start c — continuous voice-to-text recording
allowed-tools: [Bash, Read]
---

Run the /recognize:start skill with arguments: c
ALIAS

cat > "$COMMANDS_DIR/rp.md" << 'ALIAS'
---
description: "Alias for /recognize:ptt — push-to-talk voice-to-text (hold space to record)"
allowed-tools: [Bash, Read]
---

Run the /recognize:ptt skill with arguments: $ARGUMENTS
ALIAS

cat > "$COMMANDS_DIR/rh.md" << 'ALIAS'
---
description: Alias for /recognize:history — search past voice transcriptions
allowed-tools: [Bash, Read]
---

Run the /recognize:history skill with arguments: $ARGUMENTS
ALIAS

echo "recognize setup complete."
echo "  Binary: $(which recognize)"
echo "  Config: $RECOGNIZE_DIR/"
echo "  Aliases: $COMMANDS_DIR/{r,rs,rc,rp,rh}.md"
echo ""
echo "Type /r in Claude Code to start speaking."
