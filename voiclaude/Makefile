# Makefile for VoiClaude MCP Server

.PHONY: help install install-dev test run clean uninstall check-deps

# Colors
BLUE = \033[0;34m
GREEN = \033[0;32m
YELLOW = \033[1;33m
RED = \033[0;31m
NC = \033[0m

# Default target
help:
	@echo "$(BLUE)VoiClaude MCP Server$(NC)"
	@echo ""
	@echo "$(YELLOW)Available commands:$(NC)"
	@echo "  make install      - Install VoiClaude for production"
	@echo "  make install-dev  - Install VoiClaude in development mode" 
	@echo "  make test         - Test the installation"
	@echo "  make run          - Run the MCP server directly"
	@echo "  make check-deps   - Check dependencies"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make uninstall    - Remove installation"
	@echo ""
	@echo "$(YELLOW)Prerequisites:$(NC)"
	@echo "  - recognize CLI must be installed and in PATH"
	@echo "  - Python 3.8+ required"
	@echo "  - Claude Code for MCP integration"

# Check if recognize CLI is available
check-deps:
	@echo "$(BLUE)Checking dependencies...$(NC)"
	@command -v recognize >/dev/null 2>&1 || { echo "$(RED)Error: 'recognize' CLI not found. Please install it first.$(NC)"; exit 1; }
	@python3 -c "import sys; assert sys.version_info >= (3, 8)" 2>/dev/null || { echo "$(RED)Error: Python 3.8+ required$(NC)"; exit 1; }
	@echo "$(GREEN)✓ recognize CLI found: $$(which recognize)$(NC)"
	@echo "$(GREEN)✓ Python version: $$(python3 --version)$(NC)"

# Install for production
install: check-deps
	@echo "$(BLUE)Installing VoiClaude...$(NC)"
	@pip install -e .
	@echo "$(GREEN)✓ VoiClaude installed successfully$(NC)"
	@echo "$(YELLOW)Next steps:$(NC)"
	@echo "  1. Configure Claude Code MCP settings"  
	@echo "  2. Restart Claude Code"
	@echo "  3. Use /mcp__voiclaude__listen and /mcp__voiclaude__conv commands"

# Install in development mode with extra dependencies
install-dev: check-deps
	@echo "$(BLUE)Installing VoiClaude in development mode...$(NC)"
	@pip install -e ".[dev]" || pip install -e .
	@echo "$(GREEN)✓ VoiClaude development installation complete$(NC)"

# Test the installation
test: check-deps
	@echo "$(BLUE)Testing VoiClaude installation...$(NC)"
	@python3 -c "import voiclaude; print('✓ Package import successful')" || { echo "$(RED)✗ Package import failed$(NC)"; exit 1; }
	@voiclaude --help >/dev/null 2>&1 && echo "$(GREEN)✓ CLI command available$(NC)" || echo "$(YELLOW)⚠ CLI command not in PATH (this is OK)$(NC)"
	@python3 -m voiclaude --help >/dev/null 2>&1 && echo "$(GREEN)✓ Module execution works$(NC)" || echo "$(RED)✗ Module execution failed$(NC)"
	@echo "$(GREEN)✓ Installation test complete$(NC)"

# Run the server directly (for testing)
run: check-deps
	@echo "$(BLUE)Starting VoiClaude MCP Server (test mode)...$(NC)"
	@echo "$(YELLOW)Press Ctrl+C to stop$(NC)"
	@python3 run.py

# Run with debug logging
run-debug: check-deps
	@echo "$(BLUE)Starting VoiClaude MCP Server (debug mode)...$(NC)"
	@VOICLAUDE_LOG_LEVEL=DEBUG python3 run.py

# Clean build artifacts
clean:
	@echo "$(BLUE)Cleaning build artifacts...$(NC)"
	@rm -rf build/ dist/ *.egg-info/
	@find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	@find . -type f -name "*.pyc" -delete 2>/dev/null || true
	@echo "$(GREEN)✓ Cleanup complete$(NC)"

# Uninstall
uninstall:
	@echo "$(BLUE)Uninstalling VoiClaude...$(NC)"
	@pip uninstall voiclaude -y 2>/dev/null || echo "$(YELLOW)VoiClaude not found in pip$(NC)"
	@echo "$(GREEN)✓ Uninstall complete$(NC)"

# Show configuration example
config-example:
	@echo "$(BLUE)Claude Code MCP Configuration Example:$(NC)"
	@echo ""
	@echo "$(YELLOW)File: ~/.config/claude-code/mcp.json$(NC)"
	@echo '{'
	@echo '  "mcpServers": {'
	@echo '    "voiclaude": {'
	@echo '      "command": "voiclaude",'
	@echo '      "args": [],'
	@echo '      "env": {}'
	@echo '    }'
	@echo '  }'
	@echo '}'
	@echo ""
	@echo "$(YELLOW)Environment Variables:$(NC)"
	@echo "export VOICLAUDE_MODEL=base.en"
	@echo "export VOICLAUDE_LANGUAGE=en"
	@echo "export VOICLAUDE_VAD_MODE=true"

# Show usage examples
usage:
	@echo "$(BLUE)VoiClaude Usage Examples:$(NC)"
	@echo ""
	@echo "$(YELLOW)In Claude Code:$(NC)"
	@echo "/mcp__voiclaude__listen"
	@echo "/mcp__voiclaude__listen model=base.en language=en timeout=30"
	@echo "/mcp__voiclaude__conv"
	@echo "/mcp__voiclaude__conv action=start model=small.en"
	@echo "/mcp__voiclaude__conv action=stop"
	@echo "/mcp__voiclaude__conv action=status"

# Package for distribution
package: clean
	@echo "$(BLUE)Creating distribution package...$(NC)"
	@python3 setup.py sdist bdist_wheel
	@echo "$(GREEN)✓ Package created in dist/$(NC)"

# Fallback for recognize CLI commands (redirect to parent directory)
.PHONY: run-vad
run-vad:
	@echo "$(YELLOW)Note: run-vad is a recognize CLI command$(NC)"
	@echo "$(BLUE)Running from parent directory:$(NC)"
	@cd .. && make run-vad