# Publishing recognize to Homebrew

Complete guide to publish the `recognize` CLI tool to Homebrew.

## 🚀 Quick Start (Your Own Tap)

### Step 1: Prepare Your Repository

1. **Push your code to GitHub:**
   ```bash
   git remote add origin https://github.com/YOUR_USERNAME/recognize.git
   git push -u origin main
   ```

2. **Create a release:**
   ```bash
   # Prepare release files
   ./prepare-release.sh 1.0.0
   
   # Upload recognize-1.0.0.tar.gz to GitHub releases
   # Create release tag: v1.0.0
   # Use the SHA256 from the script output
   ```

### Step 2: Create Homebrew Tap

```bash
# Create the tap repository
./create-homebrew-tap.sh YOUR_USERNAME

# This creates ../homebrew-recognize/ directory
cd ../homebrew-recognize

# Create GitHub repository at: https://github.com/YOUR_USERNAME/homebrew-recognize
git remote add origin https://github.com/YOUR_USERNAME/homebrew-recognize.git
git push -u origin main
```

### Step 3: Update Formula

Edit `Formula/recognize.rb` and replace:
- `YOUR_USERNAME` → your GitHub username
- `YOUR_SHA256_HERE` → SHA256 from step 1
- `YOUR_COMMIT_SHA` → your latest commit hash

### Step 4: Test Installation

```bash
# Add your tap
brew tap YOUR_USERNAME/recognize

# Install from your tap
brew install recognize

# Test it works
recognize --help
```

## 📋 Detailed Steps

### 1. Repository Setup

Your repository structure should look like:
```
recognize/
├── src/cli/
│   ├── recognize.cpp (main source)
│   ├── model_manager.cpp
│   ├── config_manager.cpp
│   ├── CMakeLists.txt
│   ├── Makefile
│   ├── README.md
│   └── ...
└── fixtures/
    └── whisper.cpp/ (submodule)
```

### 2. GitHub Release Process

#### Option A: Manual Release
1. Go to GitHub → Releases → Create new release
2. Tag: `v1.0.0`
3. Title: `recognize v1.0.0`
4. Upload `recognize-1.0.0.tar.gz`
5. Publish release

#### Option B: Automated with GitHub Actions
1. Copy `.github/workflows/release.yml` to your repo
2. Push a git tag: `git tag v1.0.0 && git push origin v1.0.0`
3. GitHub Actions will build and create release automatically

### 3. Homebrew Formula Types

#### Source-based Formula (`recognize.rb`)
- Builds from source code
- Handles dependencies automatically
- Slower installation but more flexible

#### Binary Formula (`recognize-binary.rb`)
- Uses pre-compiled binaries
- Faster installation
- Requires you to build binaries for Intel + Apple Silicon

### 4. Testing Your Formula

```bash
# Test formula syntax
brew audit --strict recognize

# Test installation
brew uninstall recognize
brew install recognize

# Test functionality
recognize --help
recognize --list-models
```

## 🍺 Advanced: Submit to Main Homebrew

For wider distribution, you can submit to the main Homebrew repository:

### Requirements
- Formula follows Homebrew guidelines
- Project has significant usage/popularity
- Passes all Homebrew tests
- Stable API and releases

### Process
1. Fork `homebrew/homebrew-core`
2. Add your formula to `Formula/recognize.rb`
3. Test thoroughly
4. Submit pull request
5. Address review feedback

### Homebrew Guidelines
- Use system dependencies when possible
- Follow naming conventions
- Include comprehensive tests
- Provide good description and homepage
- Handle errors gracefully

## 🔧 Formula Best Practices

### Dependencies
```ruby
depends_on "cmake" => :build  # Build-time only
depends_on "sdl2"            # Runtime dependency
depends_on :macos => :big_sur # Minimum macOS version
```

### Installation
```ruby
def install
  # Use system CMake
  system "cmake", ".", *std_cmake_args
  system "make", "install"
  
  # Install to proper locations
  bin.install "recognize"
  doc.install "README.md"
end
```

### Testing
```ruby
def test
  # Test basic functionality
  system "#{bin}/recognize", "--help"
  assert_match "recognize", shell_output("#{bin}/recognize --version")
end
```

## 📊 Maintenance

### Updating Your Formula

1. **New release:**
   ```bash
   ./prepare-release.sh 1.1.0
   # Upload to GitHub releases
   ```

2. **Update formula:**
   ```ruby
   version "1.1.0"
   url "https://github.com/YOUR_USERNAME/recognize/releases/download/v1.1.0/recognize-1.1.0.tar.gz"
   sha256 "NEW_SHA256_HERE"
   ```

3. **Test and commit:**
   ```bash
   brew reinstall recognize
   git commit -am "recognize: update to 1.1.0"
   git push
   ```

## 🎯 Benefits of Homebrew Distribution

### For Users
- **Easy installation:** `brew install recognize`
- **Automatic updates:** `brew upgrade recognize`
- **Dependency management:** SDL2 installed automatically
- **Uninstall support:** `brew uninstall recognize`
- **Integration:** Works with other Homebrew tools

### For You
- **Wide reach:** Homebrew has millions of users
- **Professional distribution:** Standard macOS package manager
- **Community support:** Users can contribute fixes
- **Analytics:** Download statistics through Homebrew
- **Trust:** Users trust Homebrew-distributed software

## 📝 Example User Experience

After publishing, users can install with:

```bash
# Add your tap (one-time)
brew tap YOUR_USERNAME/recognize

# Install recognize
brew install recognize

# Use immediately
recognize -m base.en

# Update later
brew upgrade recognize
```

This provides a professional, seamless installation experience that macOS users expect.

## 🚨 Troubleshooting

### Common Issues

**Formula validation fails:**
```bash
brew audit --strict recognize
# Fix any warnings/errors
```

**Build fails:**
```bash
brew install --build-from-source recognize
# Check build logs for issues
```

**Dependencies missing:**
```bash
brew deps recognize
# Ensure all dependencies are available
```

### Testing on Clean System

```bash
# Test on fresh macOS VM or new user account
brew uninstall --ignore-dependencies recognize
brew untap YOUR_USERNAME/recognize
brew tap YOUR_USERNAME/recognize
brew install recognize
```

## 🎉 Success!

Once published, your CLI tool will be easily installable by anyone with Homebrew, providing a professional distribution channel for your macOS application.