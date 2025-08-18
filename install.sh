#!/bin/bash
# Standalone installer for whisper-stream-coreml
# Downloads and installs the CLI system-wide

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
INSTALL_DIR="/usr/local/bin"
MODELS_DIR="$HOME/.whisper-stream-coreml/models"
BINARY_NAME="whisper-stream-coreml"
TEMP_DIR="/tmp/whisper-stream-coreml-install"

echo -e "${BLUE}whisper-stream-coreml Installer${NC}"
echo -e "${BLUE}================================${NC}"
echo ""

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}Error: This installer is for macOS only${NC}"
    exit 1
fi

# Check dependencies
echo -e "${BLUE}Checking dependencies...${NC}"

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    echo -e "${RED}Error: Homebrew not found${NC}"
    echo -e "${YELLOW}Install Homebrew from: https://brew.sh${NC}"
    exit 1
fi

# Check/install cmake
if ! command -v cmake &> /dev/null; then
    echo -e "${YELLOW}Installing CMake...${NC}"
    brew install cmake
fi

# Check/install SDL2
if ! pkg-config --exists sdl2; then
    echo -e "${YELLOW}Installing SDL2...${NC}"
    brew install sdl2
fi

echo -e "${GREEN}✓ Dependencies verified${NC}"

# Create temporary directory
rm -rf "$TEMP_DIR"
mkdir -p "$TEMP_DIR"
cd "$TEMP_DIR"

# Download or use local source
if [[ -n "$1" && -f "$1" ]]; then
    # Use local source directory
    echo -e "${BLUE}Using local source: $1${NC}"
    cp -r "$1"/* .
else
    # This would be used if distributing via GitHub releases
    echo -e "${BLUE}Building from local source...${NC}"
    # For now, we'll assume we're running from the source directory
    if [[ ! -f "whisper-stream-coreml.cpp" ]]; then
        echo -e "${RED}Error: Please run this installer from the source directory${NC}"
        echo -e "${YELLOW}Or provide source path: ./install.sh /path/to/source${NC}"
        exit 1
    fi
fi

# Build the application
echo -e "${BLUE}Building whisper-stream-coreml...${NC}"
if command -v make &> /dev/null && [[ -f "Makefile" ]]; then
    make build
elif [[ -f "build.sh" ]]; then
    ./build.sh
else
    echo -e "${RED}Error: No build system found${NC}"
    exit 1
fi

# Verify build
if [[ ! -f "$BINARY_NAME" ]]; then
    echo -e "${RED}Error: Build failed - binary not found${NC}"
    exit 1
fi

# Test the binary
echo -e "${BLUE}Testing binary...${NC}"
if ! ./"$BINARY_NAME" --help &> /dev/null; then
    echo -e "${RED}Error: Binary test failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build successful${NC}"

# Install system-wide
echo -e "${BLUE}Installing system-wide...${NC}"

# Create install directory
sudo mkdir -p "$INSTALL_DIR"

# Copy binary
sudo cp "$BINARY_NAME" "$INSTALL_DIR/"
sudo chmod +x "$INSTALL_DIR/$BINARY_NAME"

# Create models directory
mkdir -p "$MODELS_DIR"

# Cleanup
cd /
rm -rf "$TEMP_DIR"

echo -e "${GREEN}✓ Installation complete!${NC}"
echo ""
echo -e "${YELLOW}Installed to:${NC} $INSTALL_DIR/$BINARY_NAME"
echo -e "${YELLOW}Models directory:${NC} $MODELS_DIR"
echo ""
echo -e "${BLUE}Quick Start:${NC}"
echo -e "  ${BINARY_NAME}                    # Interactive model selection"
echo -e "  ${BINARY_NAME} -m base.en         # Use specific model"
echo -e "  ${BINARY_NAME} --list-models      # Show available models"
echo -e "  ${BINARY_NAME} -h                 # Show all options"
echo ""
echo -e "${GREEN}🎤 Ready to start transcribing!${NC}"