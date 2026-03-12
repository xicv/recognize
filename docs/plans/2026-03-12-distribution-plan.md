# recognize Distribution Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Package `recognize` for one-command installation — pre-built binary + Claude Code plugin — via install script and Homebrew tap.

**Architecture:** Two-layer distribution. Layer 1: Claude Code plugin (`recognize-voice`) bundles all command/skill files, hosted as a plugin marketplace on GitHub. Layer 2: install script downloads pre-built universal binary from GitHub Releases, sets up `~/.recognize/`, and auto-installs the plugin. Homebrew tap as a secondary channel.

**Tech Stack:** Bash (install script), GitHub Actions (CI/CD), Homebrew Ruby DSL (formula), Claude Code plugin format (JSON + Markdown)

---

### Task 1: Create the Claude Code plugin structure

**Files:**
- Create: `plugin/.claude-plugin/plugin.json`
- Create: `plugin/commands/recognize.md`
- Create: `plugin/commands/recognize-stop.md`
- Create: `plugin/commands/recognize-history.md`
- Create: `plugin/commands/r.md`
- Create: `plugin/commands/rs.md`
- Create: `plugin/commands/rc.md`
- Create: `plugin/commands/rh.md`
- Create: `plugin/commands/rp.md`

**Step 1: Create plugin directory structure**

```bash
mkdir -p plugin/.claude-plugin plugin/commands plugin/scripts
```

**Step 2: Create `plugin/.claude-plugin/plugin.json`**

```json
{
  "name": "recognize-voice",
  "version": "1.0.0",
  "description": "Voice input for Claude Code — speak instead of type. /r to record, auto-stops after silence.",
  "author": "recogniz.ing",
  "homepage": "https://github.com/anthropic-xi/recogniz.ing",
  "platforms": ["macos"]
}
```

**Step 3: Copy command files into plugin**

Copy each command file from `~/.claude/commands/` into `plugin/commands/`. The files are:

- `recognize.md` — main voice command (auto-stop, continuous, meeting, PTT modes)
- `recognize-stop.md` — stop recording and send transcript
- `recognize-history.md` — search past transcriptions
- `r.md` — alias for /recognize
- `rs.md` — alias for /recognize-stop
- `rc.md` — alias for /recognize c
- `rh.md` — alias for /recognize-history
- `rp.md` — alias for /recognize p

Copy them verbatim — no content changes needed. The plugin system loads commands from the plugin's `commands/` directory just like `~/.claude/commands/`.

**Step 4: Copy launcher script into plugin**

```bash
cp ~/.recognize/claude-launch.sh plugin/scripts/claude-launch.sh
```

**Step 5: Verify plugin structure**

```bash
find plugin/ -type f | sort
```

Expected:
```
plugin/.claude-plugin/plugin.json
plugin/commands/r.md
plugin/commands/rc.md
plugin/commands/recognize-history.md
plugin/commands/recognize-stop.md
plugin/commands/recognize.md
plugin/commands/rh.md
plugin/commands/rp.md
plugin/commands/rs.md
plugin/scripts/claude-launch.sh
```

**Step 6: Commit**

```bash
git add plugin/
git commit -m "feat: create Claude Code plugin structure for recognize-voice"
```

---

### Task 2: Create the setup script

**Files:**
- Create: `plugin/scripts/setup.sh`

**Step 1: Create `plugin/scripts/setup.sh`**

This script runs on first use. It ensures `~/.recognize/` exists and the launcher script is in place.

```bash
#!/bin/bash
# First-run setup for recognize Claude Code integration
set -e

RECOGNIZE_DIR="$HOME/.recognize"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Check recognize binary
if ! command -v recognize &>/dev/null; then
    echo "ERROR: 'recognize' binary not found in PATH."
    echo ""
    echo "Install it with:"
    echo "  curl -sSL https://raw.githubusercontent.com/anthropic-xi/recogniz.ing/main/src/cli/install.sh | sh"
    echo ""
    echo "Or with Homebrew:"
    echo "  brew tap recognizing/tap && brew install recognize"
    exit 1
fi

# Create directories
mkdir -p "$RECOGNIZE_DIR/models" "$RECOGNIZE_DIR/tmp"

# Install launcher script
cp "$SCRIPT_DIR/claude-launch.sh" "$RECOGNIZE_DIR/claude-launch.sh"
chmod +x "$RECOGNIZE_DIR/claude-launch.sh"

echo "recognize setup complete."
echo "  Binary: $(which recognize)"
echo "  Config: $RECOGNIZE_DIR/"
echo ""
echo "Type /r in Claude Code to start speaking."
```

**Step 2: Make it executable**

```bash
chmod +x plugin/scripts/setup.sh
```

**Step 3: Test the setup script**

```bash
bash plugin/scripts/setup.sh
```

Expected: Prints success message, `~/.recognize/claude-launch.sh` matches `plugin/scripts/claude-launch.sh`.

**Step 4: Commit**

```bash
git add plugin/scripts/setup.sh
git commit -m "feat: add first-run setup script for recognize plugin"
```

---

### Task 3: Create the marketplace manifest

**Files:**
- Create: `plugin/marketplace.json`

**Step 1: Create `plugin/marketplace.json`**

```json
{
  "name": "recognizing",
  "description": "Voice input tools for Claude Code",
  "owner": {
    "name": "recogniz.ing",
    "url": "https://github.com/anthropic-xi/recogniz.ing"
  },
  "plugins": [
    {
      "name": "recognize-voice",
      "source": ".",
      "description": "Voice input for Claude Code — speak instead of type. /r to record, auto-stops after silence, transcript becomes your input."
    }
  ]
}
```

Note: `"source": "."` means the plugin is in the same directory as the marketplace.json. Users will add the marketplace pointing at the `plugin/` directory or the repo root.

**Step 2: Commit**

```bash
git add plugin/marketplace.json
git commit -m "feat: add marketplace manifest for plugin discovery"
```

---

### Task 4: Rewrite the install script for pre-built binary download

**Files:**
- Modify: `install.sh` (complete rewrite)

**Step 1: Rewrite `install.sh`**

The new script downloads a pre-built binary from GitHub Releases instead of building from source.

```bash
#!/bin/bash
# One-command installer for recognize
# Downloads pre-built binary + sets up Claude Code integration
#
# Usage: curl -sSL https://raw.githubusercontent.com/anthropic-xi/recogniz.ing/main/src/cli/install.sh | sh

set -e

# Configuration
REPO="anthropic-xi/recogniz.ing"
INSTALL_DIR="/usr/local/bin"
RECOGNIZE_DIR="$HOME/.recognize"
BINARY_NAME="recognize"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}recognize installer${NC}"
echo ""

# macOS only
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}Error: recognize requires macOS (CoreML, Metal, Accelerate)${NC}"
    exit 1
fi

# Detect architecture
ARCH="$(uname -m)"
case "$ARCH" in
    arm64)  ARCH_LABEL="arm64" ;;
    x86_64) ARCH_LABEL="x86_64" ;;
    *)
        echo -e "${RED}Error: unsupported architecture: $ARCH${NC}"
        exit 1
        ;;
esac
echo -e "  Architecture: ${ARCH_LABEL}"

# Check for existing installation
if command -v recognize &>/dev/null; then
    EXISTING_VERSION=$(recognize --help 2>&1 | head -1 || echo "unknown")
    echo -e "${YELLOW}  Existing installation found. Will upgrade.${NC}"
fi

# Get latest release tag
echo -e "${BLUE}Fetching latest release...${NC}"
if command -v curl &>/dev/null; then
    LATEST_TAG=$(curl -sSL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')
elif command -v wget &>/dev/null; then
    LATEST_TAG=$(wget -qO- "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')
else
    echo -e "${RED}Error: curl or wget required${NC}"
    exit 1
fi

if [[ -z "$LATEST_TAG" ]]; then
    echo -e "${RED}Error: could not determine latest release${NC}"
    echo -e "${YELLOW}Check: https://github.com/${REPO}/releases${NC}"
    exit 1
fi
echo -e "  Version: ${LATEST_TAG}"

# Download binary
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${LATEST_TAG}/recognize-${LATEST_TAG}-${ARCH_LABEL}.tar.gz"
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

echo -e "${BLUE}Downloading recognize ${LATEST_TAG} (${ARCH_LABEL})...${NC}"
curl -sSL "$DOWNLOAD_URL" -o "$TEMP_DIR/recognize.tar.gz"

# Extract
cd "$TEMP_DIR"
tar xzf recognize.tar.gz

# Find the binary (may be in a subdirectory)
BINARY_PATH=$(find . -name "recognize" -type f -perm +111 | head -1)
if [[ -z "$BINARY_PATH" ]]; then
    echo -e "${RED}Error: binary not found in release archive${NC}"
    exit 1
fi

# Verify binary runs
if ! "$BINARY_PATH" --help &>/dev/null; then
    echo -e "${RED}Error: downloaded binary failed verification${NC}"
    exit 1
fi

# Install binary
echo -e "${BLUE}Installing to ${INSTALL_DIR}...${NC}"
if [[ -w "$INSTALL_DIR" ]]; then
    cp "$BINARY_PATH" "$INSTALL_DIR/$BINARY_NAME"
    chmod +x "$INSTALL_DIR/$BINARY_NAME"
else
    sudo cp "$BINARY_PATH" "$INSTALL_DIR/$BINARY_NAME"
    sudo chmod +x "$INSTALL_DIR/$BINARY_NAME"
fi
echo -e "${GREEN}✓ Binary installed${NC}"

# Create config directory
mkdir -p "$RECOGNIZE_DIR/models" "$RECOGNIZE_DIR/tmp"
echo -e "${GREEN}✓ Config directory created${NC}"

# Download launcher script from repo
LAUNCH_URL="https://raw.githubusercontent.com/${REPO}/main/src/cli/plugin/scripts/claude-launch.sh"
curl -sSL "$LAUNCH_URL" -o "$RECOGNIZE_DIR/claude-launch.sh"
chmod +x "$RECOGNIZE_DIR/claude-launch.sh"
echo -e "${GREEN}✓ Launcher script installed${NC}"

# Install Claude Code plugin if claude CLI exists
if command -v claude &>/dev/null; then
    echo -e "${BLUE}Installing Claude Code plugin...${NC}"
    # Try plugin install via marketplace
    claude plugin marketplace add "${REPO}" --path src/cli/plugin 2>/dev/null && \
    claude plugin install recognize-voice 2>/dev/null && \
    echo -e "${GREEN}✓ Claude Code plugin installed${NC}" || \
    echo -e "${YELLOW}⚠ Could not auto-install plugin. Install manually:${NC}"
    echo -e "  claude plugin marketplace add ${REPO}"
    echo -e "  claude plugin install recognize-voice"
else
    echo -e "${YELLOW}⚠ Claude Code CLI not found. Install plugin manually after installing Claude Code.${NC}"
fi

# Download default model
echo -e "${BLUE}Downloading default model (base.en, ~148MB)...${NC}"
"$INSTALL_DIR/$BINARY_NAME" -m base.en --help &>/dev/null 2>&1 || true
echo -e "${GREEN}✓ Default model ready${NC}"

echo ""
echo -e "${GREEN}Installation complete!${NC}"
echo ""
echo -e "  Binary:  ${INSTALL_DIR}/${BINARY_NAME}"
echo -e "  Config:  ${RECOGNIZE_DIR}/"
echo -e "  Models:  ${RECOGNIZE_DIR}/models/"
echo ""
echo -e "${BLUE}Quick start:${NC}"
echo -e "  recognize --help          # CLI usage"
echo -e "  In Claude Code: /r        # Speak, auto-stops, transcript → Claude"
echo ""
```

**Step 2: Make executable**

```bash
chmod +x install.sh
```

**Step 3: Test the script locally**

Since we can't test the full download flow without a release, verify syntax:
```bash
bash -n install.sh
```
Expected: No syntax errors.

**Step 4: Commit**

```bash
git add install.sh
git commit -m "feat: rewrite install script for pre-built binary download"
```

---

### Task 5: Update CI/CD release workflow

**Files:**
- Modify: `.github/workflows/release.yml`

**Step 1: Update the release workflow**

Key changes:
1. Use `lipo` to create a universal binary from arm64 + x86_64
2. Generate SHA256 checksums
3. Attach `install.sh` to the release
4. Bundle the plugin as a separate asset

Read the current workflow at `/Users/xicao/Projects/recogniz.ing/.github/workflows/release.yml`, then update it with:

- In the `create-release` job, add a step to create the universal binary:
  ```bash
  lipo -create release-arm64/recognize release-x86_64/recognize -output recognize-universal
  ```

- Add a step to generate checksums:
  ```bash
  shasum -a 256 recognize-*.tar.gz > checksums.txt
  ```

- Add `install.sh` from `src/cli/install.sh` to the release assets

- Add the plugin directory as a tarball:
  ```bash
  tar czf recognize-voice-plugin.tar.gz -C src/cli plugin/
  ```

**Step 2: Build and verify workflow syntax**

```bash
# GitHub Actions syntax check — just verify YAML parses
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml'))" 2>/dev/null || echo "install pyyaml or check manually"
```

**Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: update release workflow for universal binary and plugin bundle"
```

---

### Task 6: Create the Homebrew tap formula

**Files:**
- Create: `homebrew/recognize.rb`

This file lives in the main repo for now. When ready to publish, it moves to a separate `recognizing/homebrew-tap` repo.

**Step 1: Create `homebrew/recognize.rb`**

```ruby
class Recognize < Formula
  desc "Real-time speech recognition with CoreML acceleration for macOS"
  homepage "https://github.com/anthropic-xi/recogniz.ing"
  url "https://github.com/anthropic-xi/recogniz.ing/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "PLACEHOLDER"
  license "MIT"

  depends_on "cmake" => :build
  depends_on "sdl2"
  depends_on :macos

  def install
    cd "src/cli" do
      # Initialize whisper.cpp submodule
      system "git", "submodule", "update", "--init", "--recursive", "../../fixtures/whisper.cpp" rescue nil

      # Build with CoreML and Metal acceleration
      mkdir "build" do
        system "cmake", "..",
               "-DWHISPER_COREML=ON",
               "-DWHISPER_COREML_ALLOW_FALLBACK=ON",
               "-DGGML_METAL=ON",
               "-DGGML_METAL_EMBED_LIBRARY=ON",
               "-DGGML_NATIVE=ON",
               "-DGGML_ACCELERATE=ON",
               "-DGGML_BLAS=ON",
               "-DWHISPER_SDL2=ON",
               *std_cmake_args
        system "make", "-j#{ENV.make_jobs}"
      end

      bin.install "build/recognize"
    end
  end

  def post_install
    # Create config directory
    (var/"recognize").mkpath rescue nil
    system "mkdir", "-p", "#{ENV["HOME"]}/.recognize/models", "#{ENV["HOME"]}/.recognize/tmp"
  end

  test do
    assert_match "usage:", shell_output("#{bin}/recognize --help 2>&1", 0)
  end
end
```

**Step 2: Commit**

```bash
mkdir -p homebrew
git add homebrew/recognize.rb
git commit -m "feat: add Homebrew formula for recognize"
```

---

### Task 7: Add `--version` flag to recognize CLI

**Files:**
- Modify: `cli_parser.cpp`

Currently `recognize` has `RECOGNIZE_VERSION` defined in CMakeLists.txt but no `--version` flag. Add one for the install script to detect versions.

**Step 1: Add `--version` to `whisper_params_parse()` in `cli_parser.cpp`**

After the `--help` handler (near the top of the argument loop), add:

```cpp
else if (arg == "-v" || arg == "--version") {
#ifdef RECOGNIZE_VERSION
    printf("recognize %s\n", RECOGNIZE_VERSION);
#else
    printf("recognize (unknown version)\n");
#endif
    exit(0);
}
```

**Step 2: Build and test**

```bash
make rebuild
./recognize --version
```

Expected: `recognize 1.0.0`

**Step 3: Commit**

```bash
git add cli_parser.cpp
git commit -m "feat: add --version flag to recognize CLI"
```

---

### Task 8: Final verification

**Step 1: Verify plugin structure is complete**

```bash
find plugin/ -type f | sort
cat plugin/.claude-plugin/plugin.json | python3 -m json.tool
```

**Step 2: Verify install script syntax**

```bash
bash -n install.sh && echo "OK"
```

**Step 3: Verify build still works**

```bash
make rebuild && make test
```

**Step 4: Verify --version works**

```bash
./recognize --version
```

**Step 5: Commit any remaining fixes**

If anything needs fixing, commit with descriptive messages.

**Step 6: Push**

```bash
git push
```
