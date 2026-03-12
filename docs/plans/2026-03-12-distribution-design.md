# recognize Distribution Design

**Goal:** Enable one-command installation of `recognize` (native binary + Claude Code integration) for macOS users.

**Approach:** Two-layer distribution — a Claude Code plugin for the integration layer, and install script + Homebrew tap for the native binary.

## Architecture

```
User installs recognize:
  ├── curl install script (primary)     → downloads pre-built binary + installs plugin
  └── brew install recognize (secondary) → builds from source + installs plugin

Claude Code plugin (recognize-voice):
  ├── commands/: /r, /rs, /rc, /rh, /rp, /recognize, /recognize-stop, /recognize-history
  ├── scripts/: claude-launch.sh, setup.sh
  └── plugin.json: metadata + platform constraint (macOS only)
```

## Layer 1: Claude Code Plugin (`recognize-voice`)

A Claude Code plugin that bundles all integration files. Does NOT include the native binary — references it from PATH.

### Plugin Structure

```
plugin/
  .claude-plugin/
    plugin.json
  commands/
    recognize.md
    recognize-stop.md
    recognize-history.md
    r.md
    rs.md
    rc.md
    rh.md
    rp.md
  scripts/
    claude-launch.sh
    setup.sh
```

### plugin.json

```json
{
  "name": "recognize-voice",
  "version": "1.0.0",
  "description": "Voice input for Claude Code — speak instead of type. Use /r to start recording.",
  "author": "recogniz.ing",
  "homepage": "https://github.com/user/recogniz.ing",
  "platforms": ["macos"]
}
```

### Command Files

Moved from `~/.claude/commands/` into the plugin's `commands/` directory. Same content, now version-controlled and distributable.

### Setup Script

`scripts/setup.sh` runs on first use (called from `claude-launch.sh` if `~/.recognize/` doesn't exist):
1. Check `recognize` binary is in PATH
2. If missing, print install instructions (curl or brew)
3. Create `~/.recognize/{models,tmp}/`
4. Copy `claude-launch.sh` to `~/.recognize/`
5. Optionally download default model (`base.en`)

### Marketplace Distribution

The plugin is hosted in this repo (or a dedicated repo) with a `marketplace.json`:

```json
{
  "name": "recognizing",
  "owner": { "name": "recogniz.ing" },
  "plugins": [{
    "name": "recognize-voice",
    "source": "./plugin",
    "description": "Voice input for Claude Code — speak instead of type"
  }]
}
```

Install flow: `/plugin marketplace add recognizing/recogniz.ing && /plugin install recognize-voice`

## Layer 2: Native Binary Distribution

### Primary: Install Script

```bash
curl -sSL https://recogniz.ing/install.sh | sh
```

The script:
1. Detects architecture (arm64/x86_64)
2. Downloads pre-built binary from latest GitHub Release
3. Verifies SHA256 checksum
4. Installs to `/usr/local/bin/recognize` (prompts for sudo if needed)
5. Creates `~/.recognize/{models,tmp}/`
6. Copies `claude-launch.sh` to `~/.recognize/`
7. Downloads default model (`base.en`, ~148MB)
8. If `claude` CLI detected: installs Claude Code plugin automatically
9. Prints success message with usage instructions

### Secondary: Homebrew Tap

Separate repo: `recognizing/homebrew-tap`

```ruby
class Recognize < Formula
  desc "Real-time speech recognition with CoreML for macOS"
  homepage "https://github.com/user/recogniz.ing"
  url "https://github.com/user/recogniz.ing/archive/refs/tags/v1.0.0.tar.gz"
  license "MIT"
  depends_on "cmake" => :build
  depends_on "sdl2"
  depends_on :macos

  def install
    cd "src/cli" do
      system "make", "build"
      bin.install "recognize"
    end
  end

  def post_install
    system bin/"recognize", "--setup"
  end
end
```

Install: `brew tap recognizing/tap && brew install recognize`

## CI/CD Updates

The existing GitHub Actions release workflow needs:

1. **Universal binary**: Use `lipo` to combine arm64 + x86_64 binaries
2. **Checksums**: Generate SHA256 for each artifact
3. **Install script**: Attach `install.sh` to each release
4. **Plugin bundle**: Attach `plugin/` as a tarball for offline installation
5. **Release notes**: Auto-generate from commit messages

## CLI Changes

Add `--setup` flag to the `recognize` binary for post-install initialization:
- Create `~/.recognize/{models,tmp}/`
- Download default model if none exists
- Print status

Add `--uninstall` flag (or separate `uninstall.sh`):
- Remove `/usr/local/bin/recognize`
- Optionally remove `~/.recognize/` (prompt for confirmation)
- Remove Claude Code plugin

## User Journeys

### Journey 1: New user (curl install)
```
$ curl -sSL https://recogniz.ing/install.sh | sh
Installing recognize v1.0.0 (arm64)...
✓ Binary installed to /usr/local/bin/recognize
✓ Config directory created at ~/.recognize/
✓ Default model downloaded (base.en, 148MB)
✓ Claude Code plugin installed

Ready! Open Claude Code and type /r to start speaking.
```

### Journey 2: Claude Code marketplace discovery
```
> /plugin marketplace add recognizing/recogniz.ing
> /plugin install recognize-voice
> /r
→ Plugin detects recognize not installed
→ Shows: Run `curl -sSL https://recogniz.ing/install.sh | sh` to install
```

### Journey 3: Homebrew user
```
$ brew tap recognizing/tap && brew install recognize
# Builds from source (~3 min), installs binary + runs setup
$ claude
> /r
→ Works immediately
```

## Files to Create

| File | Purpose |
|------|---------|
| `plugin/.claude-plugin/plugin.json` | Plugin metadata |
| `plugin/commands/*.md` | All command/alias files (8 files) |
| `plugin/scripts/claude-launch.sh` | Launcher script |
| `plugin/scripts/setup.sh` | First-run setup |
| `plugin/marketplace.json` | Marketplace manifest |
| `install.sh` | Curl-installable setup script |
| `.github/workflows/release.yml` | Updated CI/CD for universal binary + checksums |

## Constraints

- macOS only (CoreML, Metal, Accelerate)
- arm64 (Apple Silicon) and x86_64 (Intel) support
- No code signing initially (users may see Gatekeeper warning)
- whisper models downloaded on demand, not bundled
- Plugin must gracefully handle missing binary
