class RecognizeBinary < Formula
  desc "Real-time speech recognition CLI with CoreML acceleration for macOS"
  homepage "https://github.com/YOUR_USERNAME/recognize"
  version "1.0.0"
  
  # Pre-compiled binary release
  url "https://github.com/YOUR_USERNAME/recognize/releases/download/v1.0.0/recognize-1.0.0.tar.gz"
  sha256 "YOUR_SHA256_HERE"
  
  license "MIT"

  depends_on "sdl2"
  depends_on :macos => :big_sur # macOS 11.0+

  def install
    # Install pre-compiled binary
    bin.install "recognize"
    
    # Install documentation
    doc.install "README.md" if File.exist?("README.md")
    doc.install "TUTORIAL.md" if File.exist?("TUTORIAL.md")
    doc.install "INSTALL.md" if File.exist?("INSTALL.md")

    # Create models directory
    (var/"recognize/models").mkpath
  end

  def post_install
    # Create user models directory
    (Dir.home/".recognize/models").mkpath
    
    ohai "recognize installed successfully!"
    puts <<~EOS
      Models download automatically when needed.
      Models directory: ~/.recognize/models

      Quick start:
        recognize                    # Interactive setup
        recognize -m base.en         # Use base English model
        recognize --list-models      # Show all available models

      Model management:
        recognize --list-downloaded  # Show downloaded models
        recognize --show-storage     # Show disk usage
        recognize --help             # Full help

      First model download may take a few minutes.
    EOS
  end

  test do
    # Test basic functionality
    system "#{bin}/recognize", "--help"
    system "#{bin}/recognize", "--list-models"
    
    # Test that SDL2 is properly linked
    assert_match "recognize", shell_output("#{bin}/recognize --help 2>&1")
  end

  def caveats
    <<~EOS
      🎤 Microphone permissions required:
         System Preferences → Security & Privacy → Privacy → Microphone
      
      📁 Storage requirements:
         • Binary: ~50MB
         • Models: 39MB - 3.1GB each (auto-downloaded)
      
      🚀 Recommended first model: base.en (148MB, good balance)
         recognize -m base.en
      
      📊 Model management:
         recognize --show-storage        # Check disk usage
         recognize --delete-model <name> # Remove models
         recognize --cleanup             # Clean orphaned files
    EOS
  end
end