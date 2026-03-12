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
