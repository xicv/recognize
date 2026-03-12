#!/bin/bash
# First-run setup for recognize Claude Code integration
set -e

RECOGNIZE_DIR="$HOME/.recognize"
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

# Install launcher script
cp "$SCRIPT_DIR/claude-launch.sh" "$RECOGNIZE_DIR/claude-launch.sh"
chmod +x "$RECOGNIZE_DIR/claude-launch.sh"

echo "recognize setup complete."
echo "  Binary: $(which recognize)"
echo "  Config: $RECOGNIZE_DIR/"
echo ""
echo "Type /r in Claude Code to start speaking."
