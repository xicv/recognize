#include "text_processing.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>

std::string trim_whitespace(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(start, end - start + 1);
}

bool copy_to_clipboard_macos(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    // Use pbcopy on macOS to copy to clipboard
    FILE* pipe = popen("pbcopy", "w");
    if (!pipe) {
        return false;
    }

    size_t written = fwrite(text.c_str(), 1, text.length(), pipe);
    int exit_code = pclose(pipe);

    return (written == text.length() && exit_code == 0);
}

bool is_claude_cli_available() {
    // Check if claude command is available in PATH
    FILE* pipe = popen("which claude 2>/dev/null", "r");
    if (!pipe) {
        return false;
    }

    char buffer[256];
    bool found = (fgets(buffer, sizeof(buffer), pipe) != nullptr);
    pclose(pipe);

    return found;
}

// Filter common whisper hallucination patterns from transcribed text
std::string filter_hallucinations(const std::string& text) {
    if (text.empty()) return text;

    std::string filtered = text;

    // Known phantom phrases that whisper hallucinates on silence/noise
    static const std::vector<std::string> phantom_patterns = {
        "Thank you for watching",
        "Thanks for watching",
        "Subscribe to my channel",
        "Please subscribe",
        "Like and subscribe",
        "Thank you for listening",
        "Thanks for listening",
        "Thank you.",
        "Bye.",
        "Goodbye.",
        "Amara.org",
        "This video is",
        "In this video",
        "www.",
        "http://",
        "https://",
        "[BLANK_AUDIO]",
        "(upbeat music)",
        "(dramatic music)",
        "(gentle music)",
        "(soft music)",
        "[silence]",
        "[ Silence ]",
        "[Silence]",
        "( Silence )",
        "(Silence)",
        "[typing sounds]",
        "[typing]",
        "(typing sounds)",
        "(typing)",
        "[keyboard sounds]",
        "[keyboard]",
        "[clicking]",
        "[mouse clicking]",
        "[Music]",
        "(music)",
        "you",  // common single-word hallucination on silence - only remove if it's the entire text
        "\xe5\xbe\xa1\xe8\xa6\x96\xe8\x81\xb4\xe3\x81\x82\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xa8\xe3\x81\x86\xe3\x81\x94\xe3\x81\x96\xe3\x81\x84\xe3\x81\xbe\xe3\x81\x99",  // Japanese: "Thank you for watching"
        "\xe8\xb0\xa2\xe8\xb0\xa2\xe8\xa7\x82\xe7\x9c\x8b",  // Chinese: "Thanks for watching"
    };

    // Trim whitespace first
    std::string trimmed = filtered;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = trimmed.find_last_not_of(" \t\n\r");
    trimmed = trimmed.substr(start, end - start + 1);

    // Check if entire text is a single phantom phrase (case-insensitive)
    auto to_lower = [](unsigned char c) -> char { return std::tolower(c); };
    std::string lower_trimmed = trimmed;
    std::transform(lower_trimmed.begin(), lower_trimmed.end(), lower_trimmed.begin(), to_lower);

    for (const auto& pattern : phantom_patterns) {
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), to_lower);

        // If entire trimmed text matches a phantom pattern, filter it out
        if (lower_trimmed == lower_pattern) {
            return "";
        }

        // If text starts with a URL pattern, filter it
        if ((pattern == "www." || pattern == "http://" || pattern == "https://") &&
            lower_trimmed.find(lower_pattern) == 0) {
            return "";
        }
    }

    // Deduplicate consecutive identical sentences (requires 3+ sentences total)
    // Split by sentence-ending punctuation
    std::vector<std::string> sentences;
    std::string current;
    for (char c : filtered) {
        current += c;
        if (c == '.' || c == '!' || c == '?') {
            std::string s = current;
            size_t s_start = s.find_first_not_of(" \t\n\r");
            if (s_start != std::string::npos) {
                s = s.substr(s_start);
            }
            if (!s.empty()) {
                sentences.push_back(s);
            }
            current.clear();
        }
    }
    if (!current.empty()) {
        std::string s = current;
        size_t s_start = s.find_first_not_of(" \t\n\r");
        if (s_start != std::string::npos) {
            sentences.push_back(s.substr(s_start));
        }
    }

    // Remove adjacent duplicate sentences when there are 3+ total
    if (sentences.size() >= 3) {
        std::vector<std::string> deduped;
        for (size_t idx = 1; idx < sentences.size(); idx++) {
            if (sentences[idx] != sentences[idx - 1]) {
                deduped.push_back(sentences[idx - 1]);
            }
        }
        deduped.push_back(sentences.back());

        if (deduped.size() < sentences.size()) {
            std::string result;
            for (const auto& s : deduped) {
                result += s;
            }
            return result;
        }
    }

    return filtered;
}

// Count words in a string
int count_words(const std::string& text) {
    int count = 0;
    bool in_word = false;
    for (char c : text) {
        if (std::isspace(c)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            count++;
        }
    }
    return count;
}

// Split text into roughly equal chunks by word count, breaking at sentence boundaries
std::vector<std::string> split_into_chunks(const std::string& text, int max_words_per_chunk) {
    std::vector<std::string> chunks;
    std::istringstream stream(text);
    std::string current_chunk;
    int word_count = 0;
    std::string word;

    while (stream >> word) {
        if (!current_chunk.empty()) {
            current_chunk += " ";
        }
        current_chunk += word;
        word_count++;

        // Check if we've hit the limit and we're at a sentence boundary
        bool at_sentence_end = (!word.empty() && (word.back() == '.' || word.back() == '!' || word.back() == '?'));
        if (word_count >= max_words_per_chunk && at_sentence_end) {
            chunks.push_back(current_chunk);
            current_chunk.clear();
            word_count = 0;
        }
    }

    if (!current_chunk.empty()) {
        chunks.push_back(current_chunk);
    }

    return chunks;
}

// Execute Claude CLI with a prompt string, return output. Uses temp file for safety.
std::string invoke_claude_cli(const std::string& prompt_text, int timeout_seconds) {
    // Use ~/.recognize/tmp/ for temp files (avoid world-readable /tmp)
    const char* home = getenv("HOME");
    if (!home) return "";
    std::string tmp_dir = std::string(home) + "/.recognize/tmp";
    std::filesystem::create_directories(tmp_dir);
    std::string tmp_template = tmp_dir + "/recognize_meeting_XXXXXX";
    std::vector<char> temp_path(tmp_template.begin(), tmp_template.end());
    temp_path.push_back('\0');
    int temp_fd = mkstemp(temp_path.data());
    if (temp_fd == -1) return "";

    std::string temp_path_str(temp_path.data());

    ssize_t written = write(temp_fd, prompt_text.c_str(), prompt_text.size());
    close(temp_fd);

    if (written < 0 || static_cast<size_t>(written) != prompt_text.size()) {
        std::filesystem::remove(temp_path_str);
        return "";
    }

    // Pipe prompt into claude CLI
    (void)timeout_seconds; // timeout handled by claude CLI itself
    std::ostringstream cmd;
    cmd << "cat '" << temp_path_str << "' | claude -p - 2>/dev/null";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        std::filesystem::remove(temp_path_str);
        return "";
    }

    std::ostringstream output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output << buffer;
    }

    int exit_code = pclose(pipe);
    std::filesystem::remove(temp_path_str);

    if (exit_code != 0) return "";
    return output.str();
}

// ASR refinement prompt — informed by research on LLM-based generative error
// correction (conservative bias prevents over-correction, phonetic plausibility
// anchors corrections, accent awareness handles non-native speakers, domain
// context improves accuracy for developer speech)
const std::string ASR_REFINE_PROMPT = R"(You are correcting a speech-to-text transcript from a software developer. Technical vocabulary, programming terms, CLI commands, file paths, and library names are expected.

CORRECTION PRINCIPLE: Most words are already correct. Only change words that are clearly wrong. Every correction must pass two tests:
1. The replacement sounds similar to the original word (phonetically plausible)
2. The corrected sentence reads as a coherent, meaningful whole
If either test fails, leave the original word unchanged. Under-correcting is always better than over-correcting.

ACCENT AWARENESS: The speaker may have a non-native accent, causing ASR to capture phonetically close but incorrect words. When a word doesn't fit the context, consider what similar-sounding word the speaker likely intended — sounds close in articulation are often confused by both speaker and recognizer.

Apply corrections for:

MISRECOGNIZED WORDS: Replace words that break sentence meaning with phonetically similar alternatives that restore coherence. Always consider the full sentence context, not just the individual word.

WORD BOUNDARIES AND COLLOCATIONS: ASR may split or merge words incorrectly, corrupting compound terms, technical phrases, and natural word combinations. If adjacent words don't form a meaningful expression, reconstruct the intended phrase.

GRAMMAR AND FLUENCY: Fix agreement errors, broken tense, and malformed constructions caused by ASR — not the speaker's natural style. Fix punctuation and capitalization. Preserve the speaker's tone and register.

ARTIFACTS: Remove hallucinated text, repeated phrases from audio overlap, false starts, self-corrections (keep only the final version), and transcribed background noise.

Constraints:
- Never add content the speaker did not express
- Never remove substantive meaning
- Never change words that already make sense in context
- Never over-formalize casual or conversational speech
- When uncertain, keep the original

<transcript>
[TRANSCRIPT]
</transcript>

Output ONLY the corrected text.)";

// Refine a transcript through Claude CLI for ASR error correction.
// Returns the refined text, or the original text on failure (never loses data).
std::string refine_transcription(const std::string& raw_text, int timeout_seconds) {
    if (raw_text.empty()) return raw_text;

    if (!is_claude_cli_available()) {
        fprintf(stderr, "[refine] Claude CLI not found — skipping refinement\n");
        return raw_text;
    }

    // Build prompt with transcript inserted
    std::string prompt = ASR_REFINE_PROMPT;
    const std::string placeholder = "[TRANSCRIPT]";
    size_t pos = prompt.find(placeholder);
    if (pos != std::string::npos) {
        prompt.replace(pos, placeholder.length(), raw_text);
    }

    fprintf(stderr, "[refine] Refining transcript via Claude...\n");
    std::string refined = invoke_claude_cli(prompt, timeout_seconds);

    if (refined.empty()) {
        fprintf(stderr, "[refine] Refinement failed — using raw transcript\n");
        return raw_text;
    }

    // Trim trailing whitespace
    while (!refined.empty() && (refined.back() == '\n' || refined.back() == '\r' || refined.back() == ' ')) {
        refined.pop_back();
    }

    return refined;
}
