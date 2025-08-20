class Recognize < Formula
  desc "Real-time speech recognition CLI with CoreML acceleration for macOS"
  homepage "https://github.com/YOUR_USERNAME/recognize"
  version "1.0.0"
  
  # For source builds with submodules
  url "https://github.com/YOUR_USERNAME/recognize.git",
      tag:      "v1.0.0",
      revision: "YOUR_COMMIT_SHA"
  
  # Alternative: For binary releases
  # url "https://github.com/YOUR_USERNAME/recognize/releases/download/v1.0.0/recognize-1.0.0.tar.gz"
  # sha256 "YOUR_SHA256_HERE"
  
  license "MIT"
  head "https://github.com/YOUR_USERNAME/recognize.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "sdl2"
  depends_on :macos => :big_sur # macOS 11.0+

  # Ensure we have the whisper.cpp submodule
  resource "whisper.cpp" do
    url "https://github.com/ggerganov/whisper.cpp.git",
        revision: "YOUR_WHISPER_COMMIT_SHA"
  end

  def install
    # Set up whisper.cpp submodule if building from source
    if build.head? || version.to_s != "1.0.0"
      resource("whisper.cpp").stage do
        (buildpath/"../../fixtures/whisper.cpp").install Dir["*"]
      end
    end

    cd "src/cli" do
      # Use our Makefile for a clean build
      system "make", "install-deps" if which("brew")
      system "make", "build"
      
      # Install the binary
      bin.install "recognize"
      
      # Install documentation
      doc.install "README.md", "TUTORIAL.md", "INSTALL.md"
    end

    # Create models directory
    (var/"recognize/models").mkpath
  end

  def post_install
    # Create user models directory
    (Dir.home/".recognize/models").mkpath
    
    # Show helpful message
    ohai "recognize installed successfully!"
    puts <<~EOS
      Models are downloaded automatically when needed.
      Models directory: ~/.recognize/models

      Quick start:
        recognize                    # Interactive setup
        recognize -m base.en         # Use specific model
        recognize --list-models      # Show available models

      For more information: recognize --help
    EOS
  end

  test do
    # Test that the binary runs and shows help
    system "#{bin}/recognize", "--help"
    
    # Test model listing
    system "#{bin}/recognize", "--list-models"
  end

  def caveats
    <<~EOS
      Models are downloaded automatically when needed.
      
      Storage requirements:
      • Binary: ~50MB
      • Models: 39MB - 3.1GB each (downloaded as needed)
      
      Recommended models:
      • base.en (148MB) - Good balance for English
      • tiny.en (39MB) - Fastest processing
      
      For model management:
        recognize --list-downloaded  # Show downloaded models
        recognize --show-storage     # Show disk usage
        recognize --delete-model <name>  # Remove model
    EOS
  end
end