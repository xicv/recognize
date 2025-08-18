#!/bin/bash

# Build script for whisper-stream-coreml
# Requires SDL2 and whisper.cpp dependencies

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building whisper-stream-coreml for macOS with CoreML support...${NC}"

# Check if we're on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}Error: This build script is for macOS only${NC}"
    exit 1
fi

# Check for required dependencies
echo -e "${YELLOW}Checking dependencies...${NC}"

# Check for SDL2
if ! pkg-config --exists sdl2; then
    echo -e "${RED}Error: SDL2 not found. Install with: brew install sdl2${NC}"
    exit 1
fi

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: CMake not found. Install with: brew install cmake${NC}"
    exit 1
fi

# Create build directory
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning existing build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo -e "${YELLOW}Configuring build with CMake...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
    -DWHISPER_COREML=ON \
    -DGGML_USE_METAL=ON \
    -DWHISPER_SDL2=ON

# Build
echo -e "${YELLOW}Building...${NC}"
make -j$(sysctl -n hw.ncpu)

# Check if build succeeded
if [ -f "whisper-stream-coreml" ]; then
    echo -e "${GREEN}Build successful!${NC}"
    echo -e "${GREEN}Executable: $(pwd)/whisper-stream-coreml${NC}"
    
    # Copy to parent directory for convenience
    cp whisper-stream-coreml ../
    echo -e "${GREEN}Copied to: $(dirname $(pwd))/whisper-stream-coreml${NC}"
    
    # Make it executable
    chmod +x ../whisper-stream-coreml
    
    echo -e "${YELLOW}Usage examples:${NC}"
    echo "./whisper-stream-coreml -m path/to/model.bin"
    echo "./whisper-stream-coreml -m path/to/model.bin --coreml"
    echo "./whisper-stream-coreml -m path/to/model.bin --no-coreml --step 500"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi