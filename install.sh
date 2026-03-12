#!/bin/bash
# One-command installer for recognize
# Downloads pre-built binary + sets up Claude Code integration
#
# Usage: curl -sSL https://raw.githubusercontent.com/anthropic-xi/recogniz.ing/main/src/cli/install.sh | sh

set -e

# Configuration
REPO="anthropic-xi/recogniz.ing"
INSTALL_DIR="/usr/local/bin"
RECOGNIZE_DIR="$HOME/.recognize"
BINARY_NAME="recognize"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}recognize installer${NC}"
echo ""

# macOS only
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}Error: recognize requires macOS (CoreML, Metal, Accelerate)${NC}"
    exit 1
fi

# Detect architecture
ARCH="$(uname -m)"
case "$ARCH" in
    arm64)  ARCH_LABEL="arm64" ;;
    x86_64) ARCH_LABEL="x86_64" ;;
    *)
        echo -e "${RED}Error: unsupported architecture: $ARCH${NC}"
        exit 1
        ;;
esac
echo -e "  Architecture: ${ARCH_LABEL}"

# Check for existing installation
if command -v recognize &>/dev/null; then
    EXISTING_VERSION=$(recognize --help 2>&1 | head -1 || echo "unknown")
    echo -e "${YELLOW}  Existing installation found. Will upgrade.${NC}"
fi

# Get latest release tag
echo -e "${BLUE}Fetching latest release...${NC}"
if command -v curl &>/dev/null; then
    LATEST_TAG=$(curl -sSL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')
elif command -v wget &>/dev/null; then
    LATEST_TAG=$(wget -qO- "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')
else
    echo -e "${RED}Error: curl or wget required${NC}"
    exit 1
fi

if [[ -z "$LATEST_TAG" ]]; then
    echo -e "${RED}Error: could not determine latest release${NC}"
    echo -e "${YELLOW}Check: https://github.com/${REPO}/releases${NC}"
    exit 1
fi
echo -e "  Version: ${LATEST_TAG}"

# Download binary
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${LATEST_TAG}/recognize-${LATEST_TAG}-${ARCH_LABEL}.tar.gz"
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

echo -e "${BLUE}Downloading recognize ${LATEST_TAG} (${ARCH_LABEL})...${NC}"
curl -sSL "$DOWNLOAD_URL" -o "$TEMP_DIR/recognize.tar.gz"

# Extract
cd "$TEMP_DIR"
tar xzf recognize.tar.gz

# Find the binary (may be in a subdirectory)
BINARY_PATH=$(find . -name "recognize" -type f -perm +111 | head -1)
if [[ -z "$BINARY_PATH" ]]; then
    echo -e "${RED}Error: binary not found in release archive${NC}"
    exit 1
fi

# Verify binary runs
if ! "$BINARY_PATH" --help &>/dev/null; then
    echo -e "${RED}Error: downloaded binary failed verification${NC}"
    exit 1
fi

# Install binary
echo -e "${BLUE}Installing to ${INSTALL_DIR}...${NC}"
if [[ -w "$INSTALL_DIR" ]]; then
    cp "$BINARY_PATH" "$INSTALL_DIR/$BINARY_NAME"
    chmod +x "$INSTALL_DIR/$BINARY_NAME"
else
    sudo cp "$BINARY_PATH" "$INSTALL_DIR/$BINARY_NAME"
    sudo chmod +x "$INSTALL_DIR/$BINARY_NAME"
fi
echo -e "${GREEN}✓ Binary installed${NC}"

# Create config directory
mkdir -p "$RECOGNIZE_DIR/models" "$RECOGNIZE_DIR/tmp"
echo -e "${GREEN}✓ Config directory created${NC}"

# Download launcher script from repo
LAUNCH_URL="https://raw.githubusercontent.com/${REPO}/main/src/cli/plugin/scripts/claude-launch.sh"
curl -sSL "$LAUNCH_URL" -o "$RECOGNIZE_DIR/claude-launch.sh"
chmod +x "$RECOGNIZE_DIR/claude-launch.sh"
echo -e "${GREEN}✓ Launcher script installed${NC}"

# Install Claude Code plugin if claude CLI exists
if command -v claude &>/dev/null; then
    echo -e "${BLUE}Installing Claude Code plugin...${NC}"
    # Try plugin install via marketplace
    claude plugin marketplace add "${REPO}" --path src/cli/plugin 2>/dev/null && \
    claude plugin install recognize-voice 2>/dev/null && \
    echo -e "${GREEN}✓ Claude Code plugin installed${NC}" || \
    echo -e "${YELLOW}⚠ Could not auto-install plugin. Install manually:${NC}"
    echo -e "  claude plugin marketplace add ${REPO}"
    echo -e "  claude plugin install recognize-voice"
else
    echo -e "${YELLOW}⚠ Claude Code CLI not found. Install plugin manually after installing Claude Code.${NC}"
fi

# Download default model
echo -e "${BLUE}Downloading default model (base.en, ~148MB)...${NC}"
"$INSTALL_DIR/$BINARY_NAME" -m base.en --help &>/dev/null 2>&1 || true
echo -e "${GREEN}✓ Default model ready${NC}"

echo ""
echo -e "${GREEN}Installation complete!${NC}"
echo ""
echo -e "  Binary:  ${INSTALL_DIR}/${BINARY_NAME}"
echo -e "  Config:  ${RECOGNIZE_DIR}/"
echo -e "  Models:  ${RECOGNIZE_DIR}/models/"
echo ""
echo -e "${BLUE}Quick start:${NC}"
echo -e "  recognize --help          # CLI usage"
echo -e "  In Claude Code: /r        # Speak, auto-stops, transcript → Claude"
echo ""
