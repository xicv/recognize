# Homebrew Formula for recognize
# Save as: /opt/homebrew/Library/Taps/homebrew/homebrew-core/Formula/recognize.rb
# Or create a custom tap

class Recognize < Formula
  desc "Real-time speech recognition CLI with CoreML acceleration for macOS"
  homepage "https://github.com/your-username/recognize"
  url "https://github.com/your-username/recognize/archive/v1.0.0.tar.gz"
  sha256 "YOUR_SHA256_HERE"
  license "MIT"

  depends_on "cmake" => :build
  depends_on "sdl2"
  depends_on :macos => :big_sur # macOS 10.15+

  def install
    # Create build directory
    mkdir "build" do
      system "cmake", "..", 
             "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15",
             "-DWHISPER_COREML=ON",
             "-DGGML_USE_METAL=ON",
             "-DWHISPER_SDL2=ON",
             *std_cmake_args
      system "make", "-j#{ENV.make_jobs}"
      
      # Install binary
      bin.install "recognize"
    end

    # Create models directory
    (var/"recognize/models").mkpath
  end

  def post_install
    # Create user models directory
    (Dir.home/".recognize/models").mkpath
  end

  test do
    # Test that the binary runs and shows help
    system "#{bin}/recognize", "--help"
  end

  def caveats
    <<~EOS
      Models are downloaded automatically when needed.
      Models directory: ~/.recognize/models

      Quick start:
        recognize                    # Interactive setup
        recognize -m base.en         # Use specific model
        recognize --list-models      # Show available models

      For more information, see: recognize -h
    EOS
  end
end