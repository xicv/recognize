#pragma once

#include <string>
#include <vector>

// String utilities
std::string trim_whitespace(const std::string& str);

// Clipboard
bool copy_to_clipboard_macos(const std::string& text);

// Claude CLI
bool is_claude_cli_available();
std::string invoke_claude_cli(const std::string& prompt_text, int timeout_seconds);

// ASR refinement
extern const std::string ASR_REFINE_PROMPT;
std::string refine_transcription(const std::string& raw_text, int timeout_seconds = 30);

// Hallucination filtering
std::string filter_hallucinations(const std::string& text);

// Text analysis
int count_words(const std::string& text);
std::vector<std::string> split_into_chunks(const std::string& text, int max_words_per_chunk);
