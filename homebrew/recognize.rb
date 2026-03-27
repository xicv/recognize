class Recognize < Formula
  desc "Real-time speech recognition with CoreML acceleration for macOS"
  homepage "https://github.com/xicv/recognize"
  version "2.2.0"
  license "MIT"

  on_arm do
    url "https://github.com/xicv/recognize/releases/download/v2.2.0/recognize-2.2.0-arm64-v2.tar.gz"
    sha256 "a26ceb54a64ea4811f6a2da27feb191dd1f4f77d72d1aaabc503cf2f2c78a483"
  end

  depends_on :macos
  depends_on "sdl2"

  def install
    bin.install "bin/recognize"
    lib.install Dir["lib/*.dylib"]
  end

  def post_install
    system "mkdir", "-p", "#{ENV["HOME"]}/.recognize/models", "#{ENV["HOME"]}/.recognize/tmp"
  end

  test do
    assert_match "usage:", shell_output("#{bin}/recognize --help 2>&1", 0)
  end
end
