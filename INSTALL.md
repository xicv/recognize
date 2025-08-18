# Installation Guide for whisper-stream-coreml

Multiple ways to install whisper-stream-coreml system-wide on macOS.

## 🚀 Quick Installation Methods

### Method 1: Makefile Installation (Recommended)

```bash
# Build and install system-wide
make install

# Or install for current user only (no sudo required)
make install-user
```

### Method 2: Standalone Installer

```bash
# Run the installer script
./install.sh
```

### Method 3: Package Distribution

```bash
# Create a distribution package
make package

# This creates: dist/whisper-stream-coreml.tar.gz
# Share this file with others for easy installation
```

## 📦 Installation Details

### System-wide Installation (`make install`)

**Installs to:** `/usr/local/bin/whisper-stream-coreml`
**Models directory:** `~/.whisper-stream-coreml/models`
**Accessible from:** Any terminal, any directory

```bash
# After installation, use from anywhere:
whisper-stream-coreml -m base.en
whisper-stream-coreml --list-models
whisper-stream-coreml -h
```

### User Installation (`make install-user`)

**Installs to:** `~/bin/whisper-stream-coreml`
**Models directory:** `~/.whisper-stream-coreml/models`
**Requires:** Adding `~/bin` to your PATH

```bash
# Add to PATH (if not already)
echo 'export PATH="$HOME/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

## 🗂️ Global Models Directory

When installed system-wide, models are stored in:
```
~/.whisper-stream-coreml/models/
```

**Benefits:**
- ✅ Shared across all users
- ✅ Survives reinstallation
- ✅ Centralized location
- ✅ Easy to manage/backup

**Usage:**
```bash
# Models download automatically
whisper-stream-coreml -m base.en
# Downloads to ~/.whisper-stream-coreml/models/ggml-base.en.bin

# Check downloaded models
ls -lh ~/.whisper-stream-coreml/models/

# Total models size
du -sh ~/.whisper-stream-coreml/models/
```

## 🧹 Uninstallation

### Remove System Installation
```bash
make uninstall
# Removes: /usr/local/bin/whisper-stream-coreml
# Preserves: ~/.whisper-stream-coreml/models/
```

### Remove User Installation
```bash
rm ~/bin/whisper-stream-coreml
```

### Remove Models (Optional)
```bash
rm -rf ~/.whisper-stream-coreml/
```

## 🍺 Homebrew Formula (Future)

For public distribution, you can create a Homebrew tap:

```bash
# Create custom tap
brew tap your-username/whisper-stream-coreml

# Install via Homebrew
brew install whisper-stream-coreml

# Use anywhere
whisper-stream-coreml -m base.en
```

**Homebrew Benefits:**
- ✅ Automatic dependency management
- ✅ Easy updates (`brew upgrade`)
- ✅ Proper macOS integration
- ✅ Uninstall support (`brew uninstall`)

## 📋 Distribution Package

The `make package` command creates a self-contained distribution:

```
dist/whisper-stream-coreml.tar.gz
├── whisper-stream-coreml    # Compiled binary
├── README.md                # Quick reference
├── TUTORIAL.md              # Complete guide
├── install.sh               # Auto-installer
└── uninstall.sh             # Removal script
```

**To distribute:**
1. Run `make package`
2. Share `dist/whisper-stream-coreml.tar.gz`
3. Recipients run:
   ```bash
   tar xzf whisper-stream-coreml.tar.gz
   cd whisper-stream-coreml
   ./install.sh
   ```

## 🔧 Installation Verification

After any installation method:

```bash
# Check installation
which whisper-stream-coreml
# Should show: /usr/local/bin/whisper-stream-coreml

# Test functionality
whisper-stream-coreml --help

# Verify models directory
ls ~/.whisper-stream-coreml/models/
```

## 🚨 Troubleshooting

### Permission Denied
```bash
# If sudo fails, try user installation
make install-user
```

### Binary Not Found
```bash
# Check PATH
echo $PATH

# For user installation, add ~/bin to PATH
echo 'export PATH="$HOME/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### Dependencies Missing
```bash
# Install dependencies first
make install-deps

# Then install
make install
```

### Models Directory Issues
```bash
# Manually create models directory
mkdir -p ~/.whisper-stream-coreml/models

# Check permissions
ls -la ~/.whisper-stream-coreml/
```

## 📊 Storage Requirements

**Binary:** ~50MB (compiled executable)
**Models (optional, downloaded as needed):**
- tiny.en: 39 MB
- base.en: 148 MB  
- small.en: 488 MB
- medium.en: 1.5 GB
- large: 3.1 GB

**Total for all models:** ~5.2 GB

## 🎯 Recommended Installation Flow

1. **Development/Testing:**
   ```bash
   make build && make run
   ```

2. **Personal Use:**
   ```bash
   make install-user
   ```

3. **System-wide/Multiple Users:**
   ```bash
   make install
   ```

4. **Distribution to Others:**
   ```bash
   make package
   # Share the generated .tar.gz
   ```

---

Choose the method that best fits your needs! All methods provide the same functionality with global model storage and easy access from any directory. 🎤✨