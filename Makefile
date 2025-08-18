# Makefile for whisper-stream-coreml
# A macOS CLI for real-time speech recognition with CoreML support

# Default target
.PHONY: all
all: build

# Variables
BUILD_DIR = build
TARGET = whisper-stream-coreml
CMAKE_FLAGS = -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
              -DWHISPER_COREML=ON \
              -DGGML_USE_METAL=ON \
              -DWHISPER_SDL2=ON

# Colors for output
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[1;33m
BLUE = \033[0;34m
NC = \033[0m # No Color

# Check dependencies
.PHONY: check-deps
check-deps:
	@echo "$(BLUE)Checking dependencies...$(NC)"
	@command -v cmake >/dev/null 2>&1 || { echo "$(RED)Error: CMake not found. Install with: brew install cmake$(NC)"; exit 1; }
	@pkg-config --exists sdl2 || { echo "$(RED)Error: SDL2 not found. Install with: brew install sdl2$(NC)"; exit 1; }
	@echo "$(GREEN)✓ All dependencies found$(NC)"

# Configure build
.PHONY: configure
configure: check-deps
	@echo "$(BLUE)Configuring build...$(NC)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS)
	@echo "$(GREEN)✓ Configuration complete$(NC)"

# Build the project
.PHONY: build
build: configure
	@echo "$(BLUE)Building $(TARGET)...$(NC)"
	@cd $(BUILD_DIR) && make -j$$(sysctl -n hw.ncpu)
	@if [ -f "$(BUILD_DIR)/$(TARGET)" ]; then \
		cp $(BUILD_DIR)/$(TARGET) . && \
		chmod +x $(TARGET) && \
		echo "$(GREEN)✓ Build successful: ./$(TARGET)$(NC)"; \
	else \
		echo "$(RED)✗ Build failed$(NC)"; \
		exit 1; \
	fi

# Quick rebuild (skip configure if already done)
.PHONY: rebuild
rebuild:
	@if [ -d "$(BUILD_DIR)" ]; then \
		echo "$(BLUE)Quick rebuild...$(NC)"; \
		cd $(BUILD_DIR) && make -j$$(sysctl -n hw.ncpu); \
		cp $(BUILD_DIR)/$(TARGET) . && chmod +x $(TARGET); \
		echo "$(GREEN)✓ Rebuild complete$(NC)"; \
	else \
		echo "$(YELLOW)No build directory found, running full build...$(NC)"; \
		$(MAKE) build; \
	fi

# Clean build artifacts
.PHONY: clean
clean:
	@echo "$(BLUE)Cleaning build artifacts...$(NC)"
	@rm -rf $(BUILD_DIR)
	@rm -f $(TARGET)
	@echo "$(GREEN)✓ Clean complete$(NC)"

# Development: clean + build
.PHONY: fresh
fresh: clean build

# Install dependencies (macOS)
.PHONY: install-deps
install-deps:
	@echo "$(BLUE)Installing dependencies...$(NC)"
	@if command -v brew >/dev/null 2>&1; then \
		brew install cmake sdl2; \
		echo "$(GREEN)✓ Dependencies installed$(NC)"; \
	else \
		echo "$(RED)Error: Homebrew not found. Install from https://brew.sh$(NC)"; \
		exit 1; \
	fi

# Run with default model
.PHONY: run
run: build
	@echo "$(BLUE)Starting transcription with interactive model selection...$(NC)"
	@./$(TARGET)

# Run with specific model
.PHONY: run-model
run-model: build
	@if [ -z "$(MODEL)" ]; then \
		echo "$(RED)Error: Please specify MODEL. Example: make run-model MODEL=base.en$(NC)"; \
		exit 1; \
	fi
	@echo "$(BLUE)Starting transcription with model: $(MODEL)$(NC)"
	@./$(TARGET) -m $(MODEL)

# Run with VAD mode (recommended)
.PHONY: run-vad
run-vad: build
	@MODEL=$${MODEL:-base.en}; \
	echo "$(BLUE)Starting VAD mode with model: $$MODEL$(NC)"; \
	./$(TARGET) -m $$MODEL --step 0 --length 30000

# List available models
.PHONY: list-models
list-models: build
	@./$(TARGET) --list-models

# Test build
.PHONY: test
test: build
	@echo "$(BLUE)Testing build...$(NC)"
	@./$(TARGET) --help >/dev/null && echo "$(GREEN)✓ Basic functionality test passed$(NC)" || { echo "$(RED)✗ Test failed$(NC)"; exit 1; }

# Show help
.PHONY: help
help:
	@echo "$(BLUE)whisper-stream-coreml Makefile$(NC)"
	@echo ""
	@echo "$(YELLOW)Build Commands:$(NC)"
	@echo "  make build        - Full build (configure + compile)"
	@echo "  make rebuild      - Quick rebuild (skip configure)"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make fresh        - Clean + build"
	@echo ""
	@echo "$(YELLOW)Dependencies:$(NC)"
	@echo "  make check-deps   - Check if dependencies are installed"
	@echo "  make install-deps - Install dependencies via Homebrew"
	@echo ""
	@echo "$(YELLOW)Run Commands:$(NC)"
	@echo "  make run          - Interactive model selection"
	@echo "  make run-model MODEL=base.en - Run with specific model"
	@echo "  make run-vad      - Run VAD mode (MODEL=base.en by default)"
	@echo "  make list-models  - Show available models"
	@echo ""
	@echo "$(YELLOW)Development:$(NC)"
	@echo "  make test         - Test basic functionality"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "$(YELLOW)Examples:$(NC)"
	@echo "  make install-deps && make build"
	@echo "  make run-model MODEL=tiny.en"
	@echo "  make run-vad MODEL=base.en"
	@echo "  make clean && make build"

# Show system info
.PHONY: info
info:
	@echo "$(BLUE)System Information:$(NC)"
	@echo "OS: $$(uname -s)"
	@echo "Architecture: $$(uname -m)"
	@echo "CPU Cores: $$(sysctl -n hw.ncpu)"
	@echo "Memory: $$(echo $$(($$(sysctl -n hw.memsize) / 1024 / 1024 / 1024))) GB"
	@command -v cmake >/dev/null 2>&1 && echo "CMake: $$(cmake --version | head -1)" || echo "CMake: Not installed"
	@pkg-config --exists sdl2 && echo "SDL2: $$(pkg-config --modversion sdl2)" || echo "SDL2: Not installed"

# Development shortcuts
.PHONY: dev
dev: fresh run

.PHONY: quick
quick: rebuild run