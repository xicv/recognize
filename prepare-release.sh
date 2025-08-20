#!/bin/bash
# Script to prepare a release for recognize CLI

set -e

VERSION=${1:-"1.0.0"}
RELEASE_DIR="release-$VERSION"

echo "🏗️  Preparing release $VERSION..."

# Clean and build
make clean
make build

# Create release directory
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

# Copy files
cp recognize "$RELEASE_DIR/"
cp README.md "$RELEASE_DIR/"
cp TUTORIAL.md "$RELEASE_DIR/"
cp INSTALL.md "$RELEASE_DIR/"
cp recognize.rb "$RELEASE_DIR/"

# Create installation script
cat > "$RELEASE_DIR/install.sh" << 'EOF'
#!/bin/bash
set -e

echo "🚀 Installing recognize..."

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo "❌ Error: This installer is for macOS only"
    exit 1
fi

# Install to /usr/local/bin
sudo mkdir -p /usr/local/bin
sudo cp recognize /usr/local/bin/
sudo chmod +x /usr/local/bin/recognize

# Create models directory
mkdir -p ~/.recognize/models

echo "✅ Installation complete!"
echo "📋 Run: recognize --help"
echo "📁 Models directory: ~/.recognize/models"
EOF

chmod +x "$RELEASE_DIR/install.sh"

# Create tarball
tar czf "recognize-$VERSION.tar.gz" "$RELEASE_DIR"

# Calculate SHA256
SHA256=$(shasum -a 256 "recognize-$VERSION.tar.gz" | cut -d' ' -f1)

echo ""
echo "✅ Release prepared:"
echo "📦 File: recognize-$VERSION.tar.gz"
echo "🔐 SHA256: $SHA256"
echo ""
echo "📋 Next steps:"
echo "1. Upload recognize-$VERSION.tar.gz to GitHub releases"
echo "2. Create a release tag: v$VERSION"
echo "3. Use the SHA256 hash in your Homebrew formula"
echo ""
echo "🍺 Homebrew formula template:"
echo "url \"https://github.com/YOUR_USERNAME/recognize/releases/download/v$VERSION/recognize-$VERSION.tar.gz\""
echo "sha256 \"$SHA256\""