#!/bin/bash
# VoiClaude Installation Script
# 
# This script installs VoiClaude MCP Server for Claude Code
# with all necessary dependencies and configuration

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_USER_ONLY=false
SKIP_CLAUDE_CONFIG=false

# Functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_usage() {
    echo "VoiClaude Installation Script"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --user-only          Install only for current user (no Claude Code config)"
    echo "  --skip-claude-config Skip automatic Claude Code configuration"
    echo "  --help              Show this help message"
    echo ""
    echo "Prerequisites:"
    echo "  - recognize CLI must be installed"
    echo "  - Python 3.8+ required"
    echo "  - Claude Code (for MCP integration)"
}

check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check Python version
    if ! command -v python3 &> /dev/null; then
        log_error "Python 3 is not installed. Please install Python 3.8+ first."
        exit 1
    fi
    
    python_version=$(python3 -c 'import sys; print(".".join(map(str, sys.version_info[:2])))')
    if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'; then
        log_error "Python 3.8+ required. Found version: $python_version"
        exit 1
    fi
    log_success "Python $python_version detected"
    
    # Check recognize CLI
    if ! command -v recognize &> /dev/null; then
        log_error "'recognize' CLI not found in PATH"
        log_info "Please install the recognize CLI first:"
        log_info "  1. Build from parent directory: cd .. && make install"
        log_info "  2. Or ensure it's in your PATH"
        exit 1
    fi
    log_success "recognize CLI found: $(which recognize)"
    
    # Check pip
    if ! command -v pip &> /dev/null && ! command -v pip3 &> /dev/null; then
        log_error "pip is not available. Please install pip first."
        exit 1
    fi
}

install_voiclaude() {
    log_info "Installing VoiClaude package..."
    
    cd "$SCRIPT_DIR"
    
    # Use pip or pip3
    PIP_CMD="pip3"
    if command -v pip &> /dev/null; then
        PIP_CMD="pip"
    fi
    
    # Install in editable mode
    if $PIP_CMD install -e .; then
        log_success "VoiClaude installed successfully"
    else
        log_error "Failed to install VoiClaude package"
        exit 1
    fi
    
    # Verify installation
    if python3 -c "import voiclaude; print('VoiClaude import successful')" 2>/dev/null; then
        log_success "Package verification passed"
    else
        log_warning "Package import failed, but installation may still work"
    fi
}

configure_claude_code() {
    if $SKIP_CLAUDE_CONFIG; then
        log_info "Skipping Claude Code configuration (--skip-claude-config)"
        return
    fi
    
    log_info "Configuring Claude Code MCP integration..."
    
    # Determine Claude Code config path
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        CLAUDE_CONFIG_DIR="$HOME/.config/claude-code"
    elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        # Windows (Git Bash/Cygwin)
        CLAUDE_CONFIG_DIR="$APPDATA/claude-code"
    else
        # Linux
        CLAUDE_CONFIG_DIR="$HOME/.config/claude-code"
    fi
    
    CLAUDE_CONFIG_FILE="$CLAUDE_CONFIG_DIR/mcp.json"
    
    # Create config directory if it doesn't exist
    if [ ! -d "$CLAUDE_CONFIG_DIR" ]; then
        log_info "Creating Claude Code config directory: $CLAUDE_CONFIG_DIR"
        mkdir -p "$CLAUDE_CONFIG_DIR"
    fi
    
    # Check if config file exists
    if [ -f "$CLAUDE_CONFIG_FILE" ]; then
        log_info "Existing Claude Code MCP config found"
        
        # Check if voiclaude is already configured
        if grep -q '"voiclaude"' "$CLAUDE_CONFIG_FILE" 2>/dev/null; then
            log_warning "VoiClaude already configured in Claude Code MCP settings"
            return
        fi
        
        # Backup existing config
        cp "$CLAUDE_CONFIG_FILE" "$CLAUDE_CONFIG_FILE.backup"
        log_info "Backed up existing config to mcp.json.backup"
        
        # TODO: Merge configurations (for now, show manual instructions)
        log_warning "Manual configuration required:"
        log_info "Add the following to your existing $CLAUDE_CONFIG_FILE:"
        cat << EOF

  "voiclaude": {
    "command": "voiclaude",
    "args": [],
    "env": {}
  }
EOF
    else
        # Create new config file
        log_info "Creating new Claude Code MCP configuration..."
        cat > "$CLAUDE_CONFIG_FILE" << 'EOF'
{
  "mcpServers": {
    "voiclaude": {
      "command": "voiclaude",
      "args": [],
      "env": {}
    }
  }
}
EOF
        log_success "Claude Code MCP configuration created"
    fi
    
    log_info "Configuration file location: $CLAUDE_CONFIG_FILE"
}

show_completion_message() {
    echo ""
    log_success "🎉 VoiClaude installation completed!"
    echo ""
    echo -e "${BLUE}Next Steps:${NC}"
    echo "1. Restart Claude Code to load the new MCP server"
    echo "2. Verify connection with: claude mcp list"
    echo "3. Test voice commands:"
    echo "   - /mcp__voiclaude__listen"
    echo "   - /mcp__voiclaude__conv mode=start"
    echo "   - /mcp__voiclaude__healthcheck"
    echo ""
    echo -e "${YELLOW}Configuration:${NC}"
    echo "- Set environment variables like VOICLAUDE_MODEL=base.en"
    echo "- Create ~/.voiclaude/config.json for persistent settings"
    echo "- Use 'make config-example' for configuration examples"
    echo ""
    echo -e "${BLUE}Documentation:${NC}"
    echo "- README.md: Comprehensive usage guide"
    echo "- Run: voiclaude --help"
    echo "- Debug: /mcp__voiclaude__debug_info"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --user-only)
            INSTALL_USER_ONLY=true
            shift
            ;;
        --skip-claude-config)
            SKIP_CLAUDE_CONFIG=true
            shift
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Main installation flow
echo -e "${BLUE}🎤 VoiClaude MCP Server Installer${NC}"
echo "================================="
echo ""

check_prerequisites
install_voiclaude

if ! $INSTALL_USER_ONLY; then
    configure_claude_code
fi

show_completion_message

exit 0