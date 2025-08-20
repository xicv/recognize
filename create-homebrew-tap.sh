#!/bin/bash
# Script to set up a Homebrew tap for recognize

set -e

GITHUB_USERNAME=${1:-"YOUR_USERNAME"}
TAP_NAME="homebrew-recognize"

if [ "$GITHUB_USERNAME" = "YOUR_USERNAME" ]; then
    echo "❌ Please provide your GitHub username:"
    echo "Usage: $0 <github_username>"
    exit 1
fi

echo "🍺 Setting up Homebrew tap for $GITHUB_USERNAME..."

# Create tap directory structure
TAP_DIR="../$TAP_NAME"
mkdir -p "$TAP_DIR/Formula"

# Copy the formula
cp Formula/recognize.rb "$TAP_DIR/Formula/"

# Update the formula with actual GitHub username
sed -i '' "s/YOUR_USERNAME/$GITHUB_USERNAME/g" "$TAP_DIR/Formula/recognize.rb"

# Create README for the tap
cat > "$TAP_DIR/README.md" << EOF
# Homebrew Tap for recognize

A Homebrew tap for the \`recognize\` CLI tool - real-time speech recognition with CoreML acceleration for macOS.

## Installation

\`\`\`bash
# Add the tap
brew tap $GITHUB_USERNAME/recognize

# Install recognize
brew install recognize
\`\`\`

## Usage

\`\`\`bash
# Interactive setup
recognize

# Quick start with specific model
recognize -m base.en

# List available models
recognize --list-models

# Show downloaded models
recognize --list-downloaded
\`\`\`

## About

recognize is a macOS CLI for real-time speech recognition featuring:

- 🚀 CoreML acceleration for optimal performance
- 🎯 Voice Activity Detection (VAD) 
- 📱 Multiple Whisper models (tiny to large)
- 🌍 Support for 99+ languages
- 💾 Smart model management
- 📋 Auto-copy to clipboard
- ⚙️ Comprehensive configuration system

## Requirements

- macOS 11.0+ (Big Sur or later)
- Apple Silicon or Intel Mac
- Microphone access permissions

## Documentation

- [README](https://github.com/$GITHUB_USERNAME/recognize/blob/main/README.md)
- [Tutorial](https://github.com/$GITHUB_USERNAME/recognize/blob/main/TUTORIAL.md)
- [Installation Guide](https://github.com/$GITHUB_USERNAME/recognize/blob/main/INSTALL.md)

## Model Management

\`\`\`bash
recognize --show-storage        # Check disk usage
recognize --delete-model base.en  # Remove specific model
recognize --cleanup             # Clean orphaned files
\`\`\`

Models are stored in \`~/.recognize/models/\` and range from 39MB (tiny) to 3.1GB (large).
EOF

# Create .gitignore
cat > "$TAP_DIR/.gitignore" << EOF
# macOS
.DS_Store
.DS_Store?
._*
.Spotlight-V100
.Trashes
ehthumbs.db
Thumbs.db
EOF

# Initialize git repo
cd "$TAP_DIR"
git init
git add .
git commit -m "Initial commit: Add recognize formula"

echo ""
echo "✅ Homebrew tap created in $TAP_DIR"
echo ""
echo "📋 Next steps:"
echo "1. cd $TAP_DIR"
echo "2. Create GitHub repository: https://github.com/$GITHUB_USERNAME/$TAP_NAME"
echo "3. git remote add origin https://github.com/$GITHUB_USERNAME/$TAP_NAME.git"
echo "4. git push -u origin main"
echo "5. Update Formula/recognize.rb with correct URLs and SHA256"
echo ""
echo "🍺 Once published, users can install with:"
echo "   brew tap $GITHUB_USERNAME/recognize"
echo "   brew install recognize"
EOF