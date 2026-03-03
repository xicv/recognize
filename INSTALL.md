# Installation Guide for recognize

Multiple ways to install recognize system-wide on macOS.

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

# This creates: dist/recognize.tar.gz
# Share this file with others for easy installation
```

## 📦 Installation Details

### System-wide Installation (`make install`)

**Requires:** macOS 12.0 (Monterey) or later
**Installs to:** `/usr/local/bin/recognize`
**Models directory:** `~/.recognize/models`
**Accessible from:** Any terminal, any directory

```bash
# After installation, use from anywhere:
recognize -m base.en
recognize --list-models
recognize -h
```

### User Installation (`make install-user`)

**Installs to:** `~/bin/recognize`
**Models directory:** `~/.recognize/models`
**Requires:** Adding `~/bin` to your PATH

```bash
# Add to PATH (if not already)
echo 'export PATH="$HOME/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

## 🗂️ Global Models Directory

When installed system-wide, models are stored in:
```
~/.recognize/models/
```

**Benefits:**
- ✅ Shared across all users
- ✅ Survives reinstallation
- ✅ Centralized location
- ✅ Easy to manage/backup

**Usage:**
```bash
# Models download automatically
recognize -m base.en
# Downloads to ~/.recognize/models/ggml-base.en.bin

# Check downloaded models
ls -lh ~/.recognize/models/

# Total models size
du -sh ~/.recognize/models/
```

## 🧹 Uninstallation

### Remove System Installation
```bash
make uninstall
# Removes: /usr/local/bin/recognize
# Preserves: ~/.recognize/models/
```

### Remove User Installation
```bash
rm ~/bin/recognize
```

### Remove Models (Optional)
```bash
rm -rf ~/.recognize/
```

## 🍺 Homebrew Formula (Future)

For public distribution, you can create a Homebrew tap:

```bash
# Create custom tap
brew tap your-username/recognize

# Install via Homebrew
brew install recognize

# Use anywhere
recognize -m base.en
```

**Homebrew Benefits:**
- ✅ Automatic dependency management
- ✅ Easy updates (`brew upgrade`)
- ✅ Proper macOS integration
- ✅ Uninstall support (`brew uninstall`)

## 📋 Distribution Package

The `make package` command creates a self-contained distribution:

```
dist/recognize.tar.gz
├── recognize                # Compiled binary
├── README.md                # Quick reference
├── TUTORIAL.md              # Complete guide
├── install.sh               # Auto-installer
└── uninstall.sh             # Removal script
```

**To distribute:**
1. Run `make package`
2. Share `dist/recognize.tar.gz`
3. Recipients run:
   ```bash
   tar xzf recognize.tar.gz
   cd recognize
   ./install.sh
   ```

## 🔧 Installation Verification

After any installation method:

```bash
# Check installation
which recognize
# Should show: /usr/local/bin/recognize

# Test functionality
recognize --help

# Verify models directory
ls ~/.recognize/models/
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
mkdir -p ~/.recognize/models

# Check permissions
ls -la ~/.recognize/
```

## 📊 Storage Requirements

**Binary:** ~50MB (compiled executable)
**Models (optional, downloaded as needed):**
- tiny.en: 39 MB
- base.en: 148 MB
- small.en: 488 MB
- medium.en: 1.5 GB
- large: 3.1 GB
- large-v3-turbo: 1.5 GB

**Total for all models:** ~6.7 GB

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