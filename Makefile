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

# Configuration management
.PHONY: config-list
config-list: build
	@./$(TARGET) config list

.PHONY: config-set
config-set: build
	@if [ -z "$(KEY)" ] || [ -z "$(VALUE)" ]; then \
		echo "$(RED)Error: Please specify KEY and VALUE. Example: make config-set KEY=model VALUE=base.en$(NC)"; \
		exit 1; \
	fi
	@echo "$(BLUE)Setting $(KEY) = $(VALUE)$(NC)"
	@./$(TARGET) config set $(KEY) $(VALUE)

.PHONY: config-get
config-get: build
	@if [ -z "$(KEY)" ]; then \
		echo "$(RED)Error: Please specify KEY. Example: make config-get KEY=model$(NC)"; \
		exit 1; \
	fi
	@./$(TARGET) config get $(KEY)

.PHONY: config-reset
config-reset: build
	@echo "$(YELLOW)Resetting configuration to defaults...$(NC)"
	@./$(TARGET) config reset

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
	@echo "$(YELLOW)Configuration:$(NC)"
	@echo "  make config-list  - Show current configuration"
	@echo "  make config-set KEY=value - Set configuration value"
	@echo "  make config-get KEY=key - Get configuration value"
	@echo "  make config-reset - Reset configuration to defaults"
	@echo ""
	@echo "$(YELLOW)Installation:$(NC)"
	@echo "  make install      - Install system-wide (/usr/local/bin)"
	@echo "  make install-user - Install for current user (~/bin)"
	@echo "  make uninstall    - Remove system installation"
	@echo "  make package      - Create distribution package"
	@echo ""
	@echo "$(YELLOW)Development:$(NC)"
	@echo "  make test         - Test basic functionality"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "$(YELLOW)Examples:$(NC)"
	@echo "  make install-deps && make build"
	@echo "  make run-model MODEL=tiny.en"
	@echo "  make run-vad MODEL=base.en"
	@echo "  make config-set KEY=model VALUE=base.en"
	@echo "  make config-get KEY=threads"
	@echo "  make config-list"
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

# Installation
PREFIX ?= /usr/local
INSTALL_DIR = $(PREFIX)/bin
MODELS_DIR = ~/.whisper-stream-coreml/models

.PHONY: install
install: build
	@echo "$(BLUE)Installing $(TARGET) system-wide...$(NC)"
	@sudo mkdir -p $(INSTALL_DIR)
	@sudo cp $(TARGET) $(INSTALL_DIR)/
	@sudo chmod +x $(INSTALL_DIR)/$(TARGET)
	@mkdir -p $(MODELS_DIR)
	@echo "$(GREEN)✓ Installed to $(INSTALL_DIR)/$(TARGET)$(NC)"
	@echo "$(GREEN)✓ Models directory: $(MODELS_DIR)$(NC)"
	@echo "$(YELLOW)Run: whisper-stream-coreml -h$(NC)"

.PHONY: uninstall
uninstall:
	@echo "$(BLUE)Uninstalling $(TARGET)...$(NC)"
	@sudo rm -f $(INSTALL_DIR)/$(TARGET)
	@echo "$(GREEN)✓ Uninstalled from $(INSTALL_DIR)/$(TARGET)$(NC)"
	@echo "$(YELLOW)Models directory preserved: $(MODELS_DIR)$(NC)"

.PHONY: install-user
install-user: build
	@echo "$(BLUE)Installing $(TARGET) for current user...$(NC)"
	@mkdir -p ~/bin
	@cp $(TARGET) ~/bin/
	@chmod +x ~/bin/$(TARGET)
	@mkdir -p $(MODELS_DIR)
	@echo "$(GREEN)✓ Installed to ~/bin/$(TARGET)$(NC)"
	@echo "$(GREEN)✓ Models directory: $(MODELS_DIR)$(NC)"
	@if echo $$PATH | grep -q "$$HOME/bin"; then \
		echo "$(GREEN)✓ ~/bin is in your PATH$(NC)"; \
	else \
		echo "$(YELLOW)⚠️  Add ~/bin to your PATH:$(NC)"; \
		echo "$(YELLOW)   echo 'export PATH=\"\$$HOME/bin:\$$PATH\"' >> ~/.zshrc$(NC)"; \
		echo "$(YELLOW)   source ~/.zshrc$(NC)"; \
	fi

.PHONY: package
package: build
	@echo "$(BLUE)Creating distribution package...$(NC)"
	@rm -rf dist
	@mkdir -p dist/whisper-stream-coreml
	@cp $(TARGET) dist/whisper-stream-coreml/
	@cp README.md TUTORIAL.md dist/whisper-stream-coreml/
	@printf '#!/bin/bash\nset -e\necho "Installing whisper-stream-coreml..."\nsudo mkdir -p /usr/local/bin\nsudo cp whisper-stream-coreml /usr/local/bin/\nsudo chmod +x /usr/local/bin/whisper-stream-coreml\nmkdir -p ~/.whisper-stream-coreml/models\necho "✓ Installation complete!"\necho "✓ Run: whisper-stream-coreml -h"\n' > dist/whisper-stream-coreml/install.sh
	@printf '#!/bin/bash\necho "Uninstalling whisper-stream-coreml..."\nsudo rm -f /usr/local/bin/whisper-stream-coreml\necho "✓ Uninstalled (models preserved in ~/.whisper-stream-coreml/)"\n' > dist/whisper-stream-coreml/uninstall.sh
	@chmod +x dist/whisper-stream-coreml/install.sh
	@chmod +x dist/whisper-stream-coreml/uninstall.sh
	@cd dist && tar czf whisper-stream-coreml.tar.gz whisper-stream-coreml/
	@echo "$(GREEN)✓ Package created: dist/whisper-stream-coreml.tar.gz$(NC)"
	@echo "$(YELLOW)To distribute:$(NC)"
	@echo "  tar xzf whisper-stream-coreml.tar.gz"
	@echo "  cd whisper-stream-coreml"
	@echo "  ./install.sh"

# Development shortcuts
.PHONY: dev
dev: fresh run

.PHONY: quick
quick: rebuild run