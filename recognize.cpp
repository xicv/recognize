// Real-time speech recognition with CoreML support for macOS
// Based on whisper.cpp/examples/stream/stream.cpp with CoreML optimizations

#include "common-sdl.h"
#include "common.h"
#include "common-whisper.h"
#include "whisper.h"
#include "model_manager.h"
#include "config_manager.h"
#include "export_manager.h"
#include "whisper_params.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <random>
#include <csignal>
#include <atomic>
#include <termios.h>
#include <unistd.h>
#include <filesystem>
#include <iomanip>
#include <regex>

// Global state for signal handling
std::atomic<bool> g_interrupt_received(false);
std::atomic<bool> g_is_recording(false);

// Default meeting organization prompt
const std::string DEFAULT_MEETING_PROMPT = R"(You are a senior executive assistant organizing a meeting transcription. Use ONLY content explicitly present in the transcript — do not infer, assume, or hallucinate details.

<transcript>
[TRANSCRIPT_PLACEHOLDER]
</transcript>

<context>
Meeting date: [MEETING_DATE]
Duration: [DURATION_PLACEHOLDER]
</context>

<instructions>
1. Read the full transcript and classify the meeting type by these signals:
   - Standup/sync: short updates per person, blockers
   - Planning: estimations, priorities, timelines
   - Retrospective: what went well, improvements
   - Brainstorm: ideation, "what if", divergent thinking
   - 1-on-1: two speakers, feedback, career discussion
   - All-hands: announcements, company-wide updates
   - Other: describe in 2-3 words

2. Tag each piece of information as SIGNAL (actionable: decisions, tasks, deadlines, owners) or NOISE (filler, off-topic, pleasantries). Only include SIGNAL content in the output.

3. If [Speaker N] labels appear, attribute statements consistently. If no labels, do not fabricate speaker identities.

4. Mark uncertain items with [?]. Do not guess names, dates, or numbers.

5. Match the language of the transcript in your output.
</instructions>

<output_format>
Output only Markdown. No preamble, no explanation outside the format below.

---
title: "[inferred meeting title]"
type: "[classified type]"
date: "[MEETING_DATE]"
duration: "[DURATION_PLACEHOLDER]"
speakers: [count or "unknown"]
---

## Summary
- [3-5 bullet points of key outcomes, decisions, and next steps]

## Discussion Topics

### [Topic 1]
- Key points discussed
- Relevant speaker attributions if available

### [Topic 2]
[Repeat as needed]

## Decisions
1. **[Decision]**: [rationale or context]

## Action Items
| Owner | Task | Due Date |
|-------|------|----------|
| [person or Speaker N] | [specific task] | [date or TBD] |

## Open Issues
- [Unresolved item]: [proposed next step]

<!-- meeting_metadata
{
  "title": "[meeting title]",
  "type": "[classified type]",
  "speakers": [count],
  "action_items": [count],
  "decisions": [count]
}
-->
</output_format>)";

// Signal handler for graceful shutdown — only sets atomic flag (async-signal-safe)
void signal_handler(int signal) {
    if (signal == SIGINT) {
        g_interrupt_received.store(true);
    }
}

// Check for interrupt in main loop and handle confirmation dialog safely
static bool check_interrupt_with_confirmation() {
    if (!g_interrupt_received.load()) return false;

    if (g_is_recording.load()) {
        // If no TTY attached (background process), exit immediately
        if (!isatty(STDIN_FILENO)) {
            return true;
        }

        std::cout << "\n\n Recording in progress! Are you sure you want to quit? (y/N): " << std::flush;

        struct termios old_termios, new_termios;
        tcgetattr(STDIN_FILENO, &old_termios);
        new_termios = old_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

        char c = getchar();

        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);

        if (c == 'y' || c == 'Y') {
            std::cout << "\n Stopping recording and exiting...\n" << std::endl;
            return true;
        } else {
            std::cout << "\n Continuing recording...\n" << std::endl;
            g_interrupt_received.store(false);
            return false;
        }
    }
    // Not recording — exit immediately
    return true;
}

// Auto-copy functionality
struct AutoCopySession {
    std::string session_id;
    std::chrono::high_resolution_clock::time_point start_time;
    std::ostringstream transcription_buffer;
    bool has_been_copied = false;
    
    AutoCopySession() {
        // Generate a unique session ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100000, 999999);
        session_id = std::to_string(dis(gen));
        start_time = std::chrono::high_resolution_clock::now();
    }
};

// Export session functionality
struct ExportSession {
    std::string session_id;
    std::chrono::high_resolution_clock::time_point start_time;
    std::vector<TranscriptionSegment> segments;
    SessionMetadata metadata;

    ExportSession() {
        // Generate a unique session ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100000, 999999);
        session_id = std::to_string(dis(gen));
        start_time = std::chrono::high_resolution_clock::now();
    }
};

// Meeting session functionality
struct MeetingSession {
    std::string session_id;
    std::chrono::high_resolution_clock::time_point start_time;
    std::ostringstream transcription_buffer;
    int current_speaker_id = 1;
    int total_speakers = 1;
    bool first_text_added = false;

    MeetingSession() {
        // Generate a unique session ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100000, 999999);
        session_id = std::to_string(dis(gen));
        start_time = std::chrono::high_resolution_clock::now();
    }

    void add_transcription(const std::string& text, bool speaker_turn = false) {
        // Label the very first speaker on the first call
        if (!first_text_added) {
            first_text_added = true;
            transcription_buffer << "[Speaker 1] ";
        }
        if (speaker_turn) {
            current_speaker_id++;
            if (current_speaker_id > total_speakers) {
                total_speakers = current_speaker_id;
            }
            transcription_buffer << "\n[Speaker " << current_speaker_id << "] ";
        }
        transcription_buffer << text;
    }

    std::string get_transcription() const {
        return transcription_buffer.str();
    }

    double get_duration_minutes() const {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
        return duration.count() / 60.0;
    }
};

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

std::string generate_meeting_filename(const std::string& meeting_name) {
    // Date-based naming: [YYYY]-[MM]-[DD].md
    // With --name: [name]-[YYYY]-[MM]-[DD].md
    // If the filename exists, add numeric suffix: ...-1.md, ...-2.md, etc.

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_ptr = std::localtime(&time_t);

    std::ostringstream base_name;
    if (!meeting_name.empty()) {
        base_name << meeting_name << "-";
    }
    base_name << std::put_time(tm_ptr, "%Y-%m-%d");
    std::string base = base_name.str();
    std::string filename = base + ".md";

    // Check if file exists, add numeric suffix if needed
    int suffix = 1;
    while (std::filesystem::exists(filename)) {
        filename = base + "-" + std::to_string(suffix) + ".md";
        suffix++;
    }

    return filename;
}

std::string generate_fallback_filename() {
    // Alias to generate_meeting_filename for backward compatibility
    return generate_meeting_filename("");
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
    std::string lower_trimmed = trimmed;
    std::transform(lower_trimmed.begin(), lower_trimmed.end(), lower_trimmed.begin(), ::tolower);

    for (const auto& pattern : phantom_patterns) {
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);

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

    // Detect repeated sentences (same sentence 3+ times → remove duplicates)
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

    // Count consecutive repetitions and deduplicate
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
static int count_words(const std::string& text) {
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
static std::vector<std::string> split_into_chunks(const std::string& text, int max_words_per_chunk) {
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
static std::string invoke_claude_cli(const std::string& prompt_text, int timeout_seconds) {
    // Use ~/.recognize/tmp/ for temp files (avoid world-readable /tmp)
    std::string tmp_dir = std::string(getenv("HOME")) + "/.recognize/tmp";
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

    std::ostringstream cmd;
    cmd << "timeout " << timeout_seconds << " sh -c 'cat \"" << temp_path_str << "\" | claude -p -'";

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

bool process_meeting_transcription(const std::string& transcription, const std::string& prompt,
                                    const std::string& output_file, int timeout_seconds = 120,
                                    double duration_minutes = 0.0, int max_single_pass = 20000) {
    if (transcription.empty()) {
        std::cerr << "Error: Empty transcription for meeting processing" << std::endl;
        return false;
    }

    if (!is_claude_cli_available()) {
        std::cerr << "Error: Claude CLI not found. Please install Claude Code first." << std::endl;
        std::cerr << "Visit: https://claude.ai/code for installation instructions." << std::endl;
        return false;
    }

    // Resolve prompt: if it's a file path, read its contents
    std::string effective_prompt;
    if (!prompt.empty() && std::filesystem::exists(prompt) && std::filesystem::is_regular_file(prompt)) {
        std::ifstream prompt_file(prompt);
        if (prompt_file.is_open()) {
            effective_prompt = std::string((std::istreambuf_iterator<char>(prompt_file)),
                                            std::istreambuf_iterator<char>());
            std::cout << "Using custom prompt from file: " << prompt << std::endl;
        } else {
            std::cerr << "Warning: Could not read prompt file '" << prompt << "', using default prompt" << std::endl;
            effective_prompt = DEFAULT_MEETING_PROMPT;
        }
    } else if (!prompt.empty()) {
        effective_prompt = prompt;
    } else {
        effective_prompt = DEFAULT_MEETING_PROMPT;
    }

    // Format duration string
    std::string duration_str_val;
    if (duration_minutes > 0.0) {
        std::ostringstream ds;
        int hours = static_cast<int>(duration_minutes) / 60;
        int mins = static_cast<int>(duration_minutes) % 60;
        if (hours > 0) {
            ds << hours << "h " << mins << "m";
        } else {
            ds << mins << "m";
        }
        duration_str_val = ds.str();
    } else {
        duration_str_val = "unknown";
    }

    // Format meeting date
    std::string meeting_date;
    {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_ptr = std::localtime(&time_t);
        std::ostringstream date_ss;
        date_ss << std::put_time(tm_ptr, "%Y-%m-%d");
        meeting_date = date_ss.str();
    }

    // Helper to substitute all placeholders in a prompt
    auto substitute_placeholders = [&](std::string& prompt, const std::string& transcript_content) {
        // Replace transcript placeholder
        size_t pos = prompt.find("[TRANSCRIPT_PLACEHOLDER]");
        if (pos != std::string::npos) {
            prompt.replace(pos, std::string("[TRANSCRIPT_PLACEHOLDER]").length(), transcript_content);
        } else {
            // Legacy placeholder support
            pos = prompt.find("[Paste raw transcription here]");
            if (pos != std::string::npos) {
                prompt.replace(pos, std::string("[Paste raw transcription here]").length(), transcript_content);
            } else {
                prompt += "\n\n## RAW TRANSCRIPTION:\n" + transcript_content;
            }
        }
        // Replace duration placeholder
        pos = 0;
        while ((pos = prompt.find("[DURATION_PLACEHOLDER]", pos)) != std::string::npos) {
            prompt.replace(pos, std::string("[DURATION_PLACEHOLDER]").length(), duration_str_val);
            pos += duration_str_val.length();
        }
        // Replace meeting date placeholder
        pos = 0;
        while ((pos = prompt.find("[MEETING_DATE]", pos)) != std::string::npos) {
            prompt.replace(pos, std::string("[MEETING_DATE]").length(), meeting_date);
            pos += meeting_date.length();
        }
    };

    int word_count = count_words(transcription);
    std::string final_output;

    if (word_count > max_single_pass) {
        // Multi-pass mode for long meetings
        std::cout << "Long meeting detected (" << word_count << " words). Using multi-pass summarization..." << std::endl;

        auto chunks = split_into_chunks(transcription, max_single_pass);
        std::cout << "Split into " << chunks.size() << " chunks for processing." << std::endl;

        // Pass 1: Extract structured data from each chunk
        std::string combined_extracts;
        for (size_t idx = 0; idx < chunks.size(); idx++) {
            std::cout << "Processing chunk " << (idx + 1) << "/" << chunks.size() << "..." << std::endl;

            std::string chunk_prompt = "Extract the following from this meeting transcript chunk ("
                + std::to_string(idx + 1) + "/" + std::to_string(chunks.size())
                + "). Output as structured bullet points. Preserve [Speaker N] labels.\n\n"
                + "DECISIONS: List each decision with rationale.\n"
                + "ACTION ITEMS: List each task with owner and deadline.\n"
                + "TOPICS: List key discussion topics with 2-3 bullet points each.\n"
                + "KEY FACTS: Numbers, dates, names mentioned.\n\n"
                + chunks[idx];

            std::string chunk_result = invoke_claude_cli(chunk_prompt, timeout_seconds);
            if (chunk_result.empty()) {
                std::cerr << "Warning: Failed to process chunk " << (idx + 1) << std::endl;
                combined_extracts += "\n## Chunk " + std::to_string(idx + 1) + " (raw):\n" + chunks[idx] + "\n";
            } else {
                combined_extracts += "\n## Chunk " + std::to_string(idx + 1) + " extracts:\n" + chunk_result + "\n";
            }
        }

        // Pass 2: Generate full summary from extracted data
        std::cout << "Generating final summary from extracted data..." << std::endl;
        std::string pass2_prompt = effective_prompt;
        substitute_placeholders(pass2_prompt, combined_extracts);

        final_output = invoke_claude_cli(pass2_prompt, timeout_seconds);
        if (final_output.empty()) {
            std::cerr << "Error: Failed to generate final summary" << std::endl;
            return false;
        }
    } else {
        // Single-pass mode
        std::string full_prompt = effective_prompt;
        substitute_placeholders(full_prompt, transcription);

        final_output = invoke_claude_cli(full_prompt, timeout_seconds);
        if (final_output.empty()) {
            std::cerr << "Error: Claude CLI failed to produce output" << std::endl;
            return false;
        }
    }

    // Write output to file with original transcription in HTML comments
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create meeting output file: " << output_file << std::endl;
        return false;
    }

    file << final_output;
    file << "\n\n<!--\n";
    file << "## Original Raw Transcription\n\n";
    // Escape --> in transcription to prevent breaking the HTML comment
    std::string safe_transcription = transcription;
    {
        size_t pos = 0;
        while ((pos = safe_transcription.find("-->", pos)) != std::string::npos) {
            safe_transcription.replace(pos, 3, "--&gt;");
            pos += 5;
        }
    }
    file << safe_transcription;
    file << "\n-->\n";
    file.close();

    std::cerr << "Meeting transcription processed and saved to: " << output_file << std::endl;
    return true;
}

bool should_auto_copy(const AutoCopySession& session, const whisper_params& params) {
    if (!params.auto_copy_enabled || session.has_been_copied) {
        return false;
    }
    
    // Check session duration
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(now - session.start_time);
    if (duration.count() > params.auto_copy_max_duration_hours) {
        return false;
    }
    
    // Check content size
    std::string content = session.transcription_buffer.str();
    if (content.size() > static_cast<size_t>(params.auto_copy_max_size_bytes)) {
        return false;
    }
    
    return true;
}

void perform_auto_copy(AutoCopySession& session, const whisper_params& params) {
    if (!should_auto_copy(session, params)) {
        return;
    }
    
    std::string content = session.transcription_buffer.str();
    content = trim_whitespace(content);
    
    if (content.empty()) {
        fprintf(stderr, "Auto-copy skipped: no content to copy.\n");
        return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(now - session.start_time);

    // Check duration limit
    if (duration.count() > params.auto_copy_max_duration_hours) {
        fprintf(stderr, "Auto-copy skipped: session duration (%ld hours) exceeded limit (%d hours).\n",
               duration.count(), params.auto_copy_max_duration_hours);
        return;
    }
    
    // Check size limit
    if (content.size() > static_cast<size_t>(params.auto_copy_max_size_bytes)) {
        fprintf(stderr, "Auto-copy skipped: content size (%zu bytes) exceeded limit (%d bytes).\n",
               content.size(), params.auto_copy_max_size_bytes);
        return;
    }

    // Perform the copy
    if (copy_to_clipboard_macos(content)) {
        fprintf(stderr, "Transcription copied.\n");
        session.has_been_copied = true;
    } else {
        fprintf(stderr, "Auto-copy failed: unable to copy to clipboard.\n");
    }
}

// Structure to hold bilingual results
struct BilingualSegment {
    int64_t t0;
    int64_t t1;
    std::string original_text;
    std::string english_text;
    float original_confidence;
    float english_confidence;
    bool speaker_turn;
    int speaker_id = -1;
};

// Process audio with bilingual output support
int process_audio_segment(struct whisper_context* ctx, struct whisper_context* ctx_translate, 
                         const whisper_params& params, const std::vector<float>& pcmf32,
                         std::vector<BilingualSegment>& bilingual_results) {
    
    whisper_full_params wparams = whisper_full_default_params(params.beam_size > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    
    // Configure common parameters
    wparams.print_progress   = false;
    wparams.print_special    = params.print_special;
    wparams.print_realtime   = false;
    wparams.print_timestamps = !params.no_timestamps;
    wparams.single_segment   = true; // Force single segment for bilingual processing
    wparams.max_tokens       = params.max_tokens;
    wparams.language         = params.language.c_str();
    wparams.n_threads        = params.n_threads;
    wparams.beam_search.beam_size = params.beam_size;
    wparams.audio_ctx        = params.audio_ctx;
    wparams.tdrz_enable      = params.tinydiarize;
    wparams.temperature_inc  = params.no_fallback ? 0.0f : wparams.temperature_inc;

    // Apply accuracy settings
    if (!params.initial_prompt.empty()) {
        wparams.initial_prompt = params.initial_prompt.c_str();
        if (params.meeting_mode) {
            wparams.carry_initial_prompt = true;
        }
    }
    if (!params.suppress_regex.empty()) {
        wparams.suppress_regex = params.suppress_regex.c_str();
    }
    if (params.meeting_mode) {
        wparams.suppress_nst = true;
        wparams.no_speech_thold = 0.4f;  // Research-recommended: more permissive for meetings
        wparams.entropy_thold = 2.2f;    // Slightly more permissive for natural speech
    }
    if (!params.vad_model_path.empty()) {
        wparams.vad = true;
        wparams.vad_model_path = params.vad_model_path.c_str();
        if (params.meeting_mode) {
            wparams.vad_params.threshold = 0.5f;
            wparams.vad_params.min_speech_duration_ms = 500;
            wparams.vad_params.min_silence_duration_ms = 800;   // Tuned: 2000 was too aggressive
            wparams.vad_params.max_speech_duration_s = 30.0f;
            wparams.vad_params.speech_pad_ms = 500;             // Slightly more padding
        }
    }

    bilingual_results.clear();
    
    if (params.output_mode == "original") {
        // Original language only
        wparams.translate = false;
        if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
            return -1;
        }
        
        // Extract segments
        const int n_segments = whisper_full_n_segments(ctx);
        for (int i = 0; i < n_segments; ++i) {
            BilingualSegment seg;
            seg.t0 = whisper_full_get_segment_t0(ctx, i);
            seg.t1 = whisper_full_get_segment_t1(ctx, i);
            seg.original_text = whisper_full_get_segment_text(ctx, i);
            seg.english_text = ""; // No translation
            
            // Calculate confidence
            seg.original_confidence = 0.0f;
            int token_count = whisper_full_n_tokens(ctx, i);
            if (token_count > 0) {
                for (int j = 0; j < token_count; ++j) {
                    seg.original_confidence += whisper_full_get_token_p(ctx, i, j);
                }
                seg.original_confidence /= token_count;
            }
            seg.english_confidence = 0.0f;
            seg.speaker_turn = whisper_full_get_segment_speaker_turn_next(ctx, i);
            
            bilingual_results.push_back(seg);
        }
    }
    else if (params.output_mode == "english") {
        // English translation only
        wparams.translate = true;
        if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
            return -1;
        }
        
        // Extract segments
        const int n_segments = whisper_full_n_segments(ctx);
        for (int i = 0; i < n_segments; ++i) {
            BilingualSegment seg;
            seg.t0 = whisper_full_get_segment_t0(ctx, i);
            seg.t1 = whisper_full_get_segment_t1(ctx, i);
            seg.original_text = ""; // No original
            seg.english_text = whisper_full_get_segment_text(ctx, i);
            
            // Calculate confidence
            seg.english_confidence = 0.0f;
            int token_count = whisper_full_n_tokens(ctx, i);
            if (token_count > 0) {
                for (int j = 0; j < token_count; ++j) {
                    seg.english_confidence += whisper_full_get_token_p(ctx, i, j);
                }
                seg.english_confidence /= token_count;
            }
            seg.original_confidence = 0.0f;
            seg.speaker_turn = whisper_full_get_segment_speaker_turn_next(ctx, i);
            
            bilingual_results.push_back(seg);
        }
    }
    else if (params.output_mode == "bilingual") {
        // Two-pass processing: original + translation
        
        // First pass: original language
        wparams.translate = false;
        if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
            return -1;
        }
        
        // Second pass: translation
        wparams.translate = true;
        if (whisper_full(ctx_translate, wparams, pcmf32.data(), pcmf32.size()) != 0) {
            return -1;
        }
        
        // Merge results (using original segments as base, matching by timestamps)
        const int n_segments_orig = whisper_full_n_segments(ctx);
        const int n_segments_trans = whisper_full_n_segments(ctx_translate);
        
        for (int i = 0; i < n_segments_orig; ++i) {
            BilingualSegment seg;
            seg.t0 = whisper_full_get_segment_t0(ctx, i);
            seg.t1 = whisper_full_get_segment_t1(ctx, i);
            seg.original_text = whisper_full_get_segment_text(ctx, i);
            
            // Calculate original confidence
            seg.original_confidence = 0.0f;
            int token_count = whisper_full_n_tokens(ctx, i);
            if (token_count > 0) {
                for (int j = 0; j < token_count; ++j) {
                    seg.original_confidence += whisper_full_get_token_p(ctx, i, j);
                }
                seg.original_confidence /= token_count;
            }
            
            // Find matching translation segment (approximate timestamp matching)
            seg.english_text = "";
            seg.english_confidence = 0.0f;
            for (int j = 0; j < n_segments_trans; ++j) {
                int64_t trans_t0 = whisper_full_get_segment_t0(ctx_translate, j);
                int64_t trans_t1 = whisper_full_get_segment_t1(ctx_translate, j);
                
                // Check for overlap (allow some tolerance)
                int64_t overlap_start = std::max(seg.t0, trans_t0);
                int64_t overlap_end = std::min(seg.t1, trans_t1);
                if (overlap_end > overlap_start) {
                    // Found overlapping segment
                    if (seg.english_text.empty()) {
                        seg.english_text = whisper_full_get_segment_text(ctx_translate, j);
                    } else {
                        seg.english_text += " " + std::string(whisper_full_get_segment_text(ctx_translate, j));
                    }
                    
                    // Update confidence (average)
                    int trans_token_count = whisper_full_n_tokens(ctx_translate, j);
                    if (trans_token_count > 0) {
                        float trans_confidence = 0.0f;
                        for (int k = 0; k < trans_token_count; ++k) {
                            trans_confidence += whisper_full_get_token_p(ctx_translate, j, k);
                        }
                        trans_confidence /= trans_token_count;
                        seg.english_confidence = (seg.english_confidence + trans_confidence) / 2.0f;
                    }
                }
            }
            
            seg.speaker_turn = whisper_full_get_segment_speaker_turn_next(ctx, i);
            bilingual_results.push_back(seg);
        }
    }
    
    return 0;
}

// Print tokens with confidence-based colors
void print_colored_tokens(whisper_context * ctx, int i_segment, const whisper_params& params) {
    for (int j = 0; j < whisper_full_n_tokens(ctx, i_segment); ++j) {
        if (params.print_special == false) {
            const whisper_token id = whisper_full_get_token_id(ctx, i_segment, j);
            if (id >= whisper_token_eot(ctx)) {
                continue;
            }
        }

        const char * text = whisper_full_get_token_text(ctx, i_segment, j);
        const float  p    = whisper_full_get_token_p   (ctx, i_segment, j);

        const int col = std::max(0, std::min((int) k_colors.size() - 1, (int) (std::pow(p, 3)*float(k_colors.size()))));

        printf("%s%s%s", k_colors[col].c_str(), text, "\033[0m");
    }
}

// Shared speaker tracking across export, meeting, and display paths
struct SpeakerTracker {
    int current_id = 0;
    int total_speakers = 0;

    int on_turn() {
        ++current_id;
        total_speakers = std::max(total_speakers, current_id);
        return current_id;
    }

    int get_current() const { return current_id == 0 ? 1 : current_id; }
};

// Print bilingual results with proper formatting
void print_bilingual_results(const std::vector<BilingualSegment>& segments, const whisper_params& params,
                             AutoCopySession& auto_copy_session, ExportSession& export_session,
                             SpeakerTracker& speaker_tracker, MeetingSession* meeting_session = nullptr,
                             bool tty_output = true, std::ostringstream* pipe_buffer = nullptr) {

    // Helper: route text to stdout (TTY) or pipe buffer
    auto out = [&](const std::string& text) {
        if (tty_output) { printf("%s", text.c_str()); }
        if (pipe_buffer) { *pipe_buffer << text; }
    };

    for (const auto& seg : segments) {
        // Track speaker IDs via shared tracker
        int seg_speaker_id = speaker_tracker.get_current();
        if (seg.speaker_turn) {
            seg_speaker_id = speaker_tracker.on_turn();
        }
        if (params.no_timestamps) {
            // Plain text mode
            if (params.output_mode == "original") {
                out(seg.original_text);

                // Add to auto-copy buffer
                if (params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) {
                    auto_copy_session.transcription_buffer << seg.original_text;
                }

                // Add to export session
                if (params.export_enabled) {
                    export_session.segments.emplace_back(
                        0, 0,
                        seg.original_text,
                        seg.original_confidence,
                        seg.speaker_turn,
                        seg.speaker_turn ? seg_speaker_id : -1
                    );
                }

                // Add to meeting session
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text, seg.speaker_turn);
                }
            }
            else if (params.output_mode == "english") {
                out(seg.english_text);

                // Add to auto-copy buffer
                if (params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) {
                    auto_copy_session.transcription_buffer << seg.english_text;
                }

                // Add to export session
                if (params.export_enabled) {
                    export_session.segments.emplace_back(
                        0, 0,
                        seg.english_text,
                        seg.english_confidence,
                        seg.speaker_turn,
                        seg.speaker_turn ? seg_speaker_id : -1
                    );
                }

                // Add to meeting session
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.english_text, seg.speaker_turn);
                }
            }
            else if (params.output_mode == "bilingual") {
                // Detect language for prefixes
                std::string lang_code = params.language;
                if (lang_code == "auto") lang_code = "orig";

                out(lang_code + ": " + seg.original_text + "\n");
                out("en: " + seg.english_text + "\n");
                
                // Add to auto-copy buffer (bilingual format)
                if (params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) {
                    auto_copy_session.transcription_buffer << lang_code << ": " << seg.original_text << "\n";
                    auto_copy_session.transcription_buffer << "en: " << seg.english_text << "\n";
                }

                // Add to export session (combine both texts)
                if (params.export_enabled) {
                    std::string combined_text = lang_code + ": " + seg.original_text + "\nen: " + seg.english_text;
                    export_session.segments.emplace_back(
                        0, 0,
                        combined_text,
                        (seg.original_confidence + seg.english_confidence) / 2.0f,
                        seg.speaker_turn,
                        seg.speaker_turn ? seg_speaker_id : -1
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text, seg.speaker_turn);
                    meeting_session->add_transcription(" ");
                    meeting_session->add_transcription(seg.english_text);
                    meeting_session->add_transcription("\n");
                }
            }
        }
        else {
            // Timestamped mode
            std::string timestamp_prefix = "[" + to_timestamp(seg.t0, false) + " --> " + to_timestamp(seg.t1, false) + "]  ";
            
            if (params.output_mode == "original") {
                out(timestamp_prefix + seg.original_text);
                if (seg.speaker_turn) out(" [SPEAKER_TURN]");
                out("\n");

                // Add to auto-copy buffer
                if (params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) {
                    auto_copy_session.transcription_buffer << timestamp_prefix << seg.original_text;
                    if (seg.speaker_turn) auto_copy_session.transcription_buffer << " [SPEAKER_TURN]";
                    auto_copy_session.transcription_buffer << "\n";
                }

                // Add to export session
                if (params.export_enabled) {
                    export_session.segments.emplace_back(
                        seg.t0 / 10,
                        seg.t1 / 10,
                        seg.original_text,
                        seg.original_confidence,
                        seg.speaker_turn,
                        seg.speaker_turn ? seg_speaker_id : -1
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text, seg.speaker_turn);
                    meeting_session->add_transcription(" ");
                }
            }
            else if (params.output_mode == "english") {
                out(timestamp_prefix + seg.english_text);
                if (seg.speaker_turn) out(" [SPEAKER_TURN]");
                out("\n");

                // Add to auto-copy buffer
                if (params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) {
                    auto_copy_session.transcription_buffer << timestamp_prefix << seg.english_text;
                    if (seg.speaker_turn) auto_copy_session.transcription_buffer << " [SPEAKER_TURN]";
                    auto_copy_session.transcription_buffer << "\n";
                }

                // Add to export session
                if (params.export_enabled) {
                    export_session.segments.emplace_back(
                        seg.t0 / 10,
                        seg.t1 / 10,
                        seg.english_text,
                        seg.english_confidence,
                        seg.speaker_turn,
                        seg.speaker_turn ? seg_speaker_id : -1
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.english_text, seg.speaker_turn);
                    meeting_session->add_transcription(" ");
                }
            }
            else if (params.output_mode == "bilingual") {
                // Detect language for prefixes
                std::string lang_code = params.language;
                if (lang_code == "auto") lang_code = "orig";

                out(timestamp_prefix + lang_code + ": " + seg.original_text + "\n");
                out(timestamp_prefix + "en: " + seg.english_text);
                if (seg.speaker_turn) out(" [SPEAKER_TURN]");
                out("\n");

                // Add to auto-copy buffer
                if (params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) {
                    auto_copy_session.transcription_buffer << timestamp_prefix << lang_code << ": " << seg.original_text << "\n";
                    auto_copy_session.transcription_buffer << timestamp_prefix << "en: " << seg.english_text;
                    if (seg.speaker_turn) auto_copy_session.transcription_buffer << " [SPEAKER_TURN]";
                    auto_copy_session.transcription_buffer << "\n";
                }

                // Add to export session (combine both texts)
                if (params.export_enabled) {
                    std::string combined_text = lang_code + ": " + seg.original_text + "\nen: " + seg.english_text;
                    export_session.segments.emplace_back(
                        seg.t0 / 10,
                        seg.t1 / 10,
                        combined_text,
                        (seg.original_confidence + seg.english_confidence) / 2.0f,
                        seg.speaker_turn,
                        seg.speaker_turn ? seg_speaker_id : -1
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text, seg.speaker_turn);
                    meeting_session->add_transcription(" ");
                    meeting_session->add_transcription(seg.english_text);
                    meeting_session->add_transcription(" ");
                }
            }
        }

        fflush(stdout);
    }
}

void perform_export(ExportSession& session, const whisper_params& params) {
    if (!params.export_enabled || session.segments.empty()) {
        return;
    }
    
    // Validate export format
    auto supported_formats = ExportManager::get_supported_formats();
    bool format_valid = false;
    for (const auto& format : supported_formats) {
        if (format == params.export_format) {
            format_valid = true;
            break;
        }
    }
    
    if (!format_valid) {
        printf("Export failed: unsupported format '%s'. Supported formats: ", params.export_format.c_str());
        for (size_t i = 0; i < supported_formats.size(); ++i) {
            printf("%s", supported_formats[i].c_str());
            if (i < supported_formats.size() - 1) printf(", ");
        }
        printf("\n");
        return;
    }
    
    // Setup export manager
    ExportManager export_manager;
    
    // Set export format
    ExportFormat format = ExportManager::extension_to_format("." + params.export_format);
    export_manager.set_format(format);
    
    // Set output file
    if (!params.export_file.empty()) {
        export_manager.set_output_file(params.export_file);
    }
    export_manager.set_auto_filename(params.export_auto_filename);
    
    // Set export options
    export_manager.set_include_metadata(params.export_include_metadata);
    export_manager.set_include_timestamps(params.export_include_timestamps);
    export_manager.set_include_confidence(params.export_include_confidence);
    
    // Add all segments
    for (const auto& segment : session.segments) {
        export_manager.add_segment(segment);
    }
    
    // Set session metadata
    session.metadata.end_time = std::chrono::system_clock::now();
    session.metadata.total_segments = session.segments.size();
    
    if (!session.segments.empty()) {
        int64_t total_duration = session.segments.back().end_time_ms - session.segments.front().start_time_ms;
        session.metadata.total_duration_seconds = total_duration / 1000.0;
    }
    
    export_manager.set_metadata(session.metadata);
    
    // Perform export
    if (export_manager.export_transcription()) {
        printf("Export completed successfully.\n");
    } else {
        printf("Export failed.\n");
    }
}

void whisper_print_usage(int argc, char ** argv, const whisper_params & params);

static bool whisper_params_parse(int argc, char ** argv, whisper_params & params) {
    // Helper to check that a required argument value exists
    auto require_arg = [&](int idx, const std::string& flag) -> bool {
        if (idx + 1 >= argc) {
            fprintf(stderr, "error: '%s' requires an argument\n", flag.c_str());
            return false;
        }
        return true;
    };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            whisper_print_usage(argc, argv, params);
            exit(0);
        }
        else if (arg == "-t"    || arg == "--threads")       { if (!require_arg(i, arg)) return false; params.n_threads     = std::stoi(argv[++i]); }
        else if (                  arg == "--step")          { if (!require_arg(i, arg)) return false; params.step_ms       = std::stoi(argv[++i]); }
        else if (                  arg == "--length")        { if (!require_arg(i, arg)) return false; params.length_ms     = std::stoi(argv[++i]); }
        else if (                  arg == "--keep")          { if (!require_arg(i, arg)) return false; params.keep_ms       = std::stoi(argv[++i]); }
        else if (arg == "-c"    || arg == "--capture")       { if (!require_arg(i, arg)) return false; params.capture_id    = std::stoi(argv[++i]); }
        else if (arg == "-mt"   || arg == "--max-tokens")    { if (!require_arg(i, arg)) return false; params.max_tokens    = std::stoi(argv[++i]); }
        else if (arg == "-ac"   || arg == "--audio-ctx")     { if (!require_arg(i, arg)) return false; params.audio_ctx     = std::stoi(argv[++i]); }
        else if (arg == "-bs"   || arg == "--beam-size")     { if (!require_arg(i, arg)) return false; params.beam_size     = std::stoi(argv[++i]); }
        else if (arg == "-vth"  || arg == "--vad-thold")     { if (!require_arg(i, arg)) return false; params.vad_thold     = std::stof(argv[++i]); }
        else if (arg == "-fth"  || arg == "--freq-thold")    { if (!require_arg(i, arg)) return false; params.freq_thold    = std::stof(argv[++i]); }
        else if (arg == "-tr"   || arg == "--translate")     { params.translate     = true; }
        else if (arg == "-nf"   || arg == "--no-fallback")   { params.no_fallback   = true; }
        else if (arg == "-ps"   || arg == "--print-special") { params.print_special = true; }
        else if (arg == "-pc"   || arg == "--print-colors")  { params.print_colors  = true; }
        else if (arg == "-kc"   || arg == "--keep-context")  { params.no_context    = false; }
        else if (arg == "-l"    || arg == "--language")      { if (!require_arg(i, arg)) return false; params.language      = argv[++i]; }
        else if (arg == "-m"    || arg == "--model")         { if (!require_arg(i, arg)) return false; params.model         = argv[++i]; }
        else if (arg == "-f"    || arg == "--file")          { if (!require_arg(i, arg)) return false; params.fname_out     = argv[++i]; }
        else if (arg == "-om"   || arg == "--output-mode")   { if (!require_arg(i, arg)) return false; params.output_mode   = argv[++i]; }
        else if (arg == "-tdrz" || arg == "--tinydiarize")   { params.tinydiarize   = true; }
        else if (arg == "-sa"   || arg == "--save-audio")    { params.save_audio    = true; }
        else if (arg == "-ng"   || arg == "--no-gpu")        { params.use_gpu       = false; }
        else if (arg == "-fa"   || arg == "--flash-attn")    { params.flash_attn    = true; }
        // CoreML specific options
        else if (arg == "-coreml" || arg == "--coreml")      { params.use_coreml    = true; }
        else if (arg == "-ncoreml" || arg == "--no-coreml")  { params.use_coreml    = false; }
        else if (arg == "-cm"   || arg == "--coreml-model")  { if (!require_arg(i, arg)) return false; params.coreml_model  = argv[++i]; }
        // Model management options
        else if (arg == "--list-models")                     { params.list_models   = true; }
        else if (arg == "--list-downloaded")                 { params.list_downloaded = true; }
        else if (arg == "--show-storage")                    { params.show_storage  = true; }
        else if (arg == "--delete-model")                    { if (!require_arg(i, arg)) return false; params.delete_model_flag = true; params.model_to_delete = argv[++i]; }
        else if (arg == "--delete-all-models")               { params.delete_all_models_flag = true; }
        else if (arg == "--cleanup")                         { params.cleanup_models = true; }
        // Auto-copy options
        else if (arg == "--auto-copy")                       { params.auto_copy_enabled = true; }
        else if (arg == "--no-auto-copy")                    { params.auto_copy_enabled = false; }
        else if (arg == "--auto-copy-max-duration")          { if (!require_arg(i, arg)) return false; params.auto_copy_max_duration_hours = std::stoi(argv[++i]); }
        else if (arg == "--auto-copy-max-size")              { if (!require_arg(i, arg)) return false; params.auto_copy_max_size_bytes = std::stoi(argv[++i]); }
        // Export options
        else if (arg == "--export")                          { params.export_enabled = true; }
        else if (arg == "--no-export")                       { params.export_enabled = false; }
        else if (arg == "--export-format")                   { if (!require_arg(i, arg)) return false; params.export_format = argv[++i]; }
        else if (arg == "--export-file")                     { if (!require_arg(i, arg)) return false; params.export_file = argv[++i]; params.export_auto_filename = false; }
        else if (arg == "--export-auto-filename")            { params.export_auto_filename = true; }
        else if (arg == "--export-no-metadata")              { params.export_include_metadata = false; }
        else if (arg == "--export-no-timestamps")            { params.export_include_timestamps = false; }
        else if (arg == "--export-include-confidence")       { params.export_include_confidence = true; }
        // Meeting options
        else if (arg == "--meeting")                         { params.meeting_mode = true; }
        else if (arg == "--no-meeting")                      { params.meeting_mode = false; }
        else if (arg == "--prompt")                          { if (!require_arg(i, arg)) return false; params.meeting_prompt = argv[++i]; }
        else if (arg == "--name")                            { if (!require_arg(i, arg)) return false; params.meeting_name = argv[++i]; }
        else if (arg == "--meeting-timeout")                  { if (!require_arg(i, arg)) return false; params.meeting_timeout = std::stoi(argv[++i]); }
        else if (arg == "--meeting-max-single-pass")          { if (!require_arg(i, arg)) return false; params.meeting_max_single_pass = std::stoi(argv[++i]); }
        // Accuracy options
        else if (arg == "--initial-prompt")                   { if (!require_arg(i, arg)) return false; params.initial_prompt = argv[++i]; }
        else if (arg == "--suppress-regex")                   { if (!require_arg(i, arg)) return false; params.suppress_regex = argv[++i]; }
        // VAD model
        else if (arg == "--vad-model")                        { if (!require_arg(i, arg)) return false; params.vad_model_path = argv[++i]; }
        // Silence timeout
        else if (arg == "--silence-timeout")                  { if (!require_arg(i, arg)) return false; params.silence_timeout = std::stof(argv[++i]); }
        // Config management options
        else if (arg == "config") {
            if (i + 1 < argc) {
                std::string config_cmd = argv[++i];
                ConfigManager config_manager;
                config_manager.load_config();
                
                if (config_cmd == "list") {
                    config_manager.list_config();
                    exit(0);
                } else if (config_cmd == "set" && i + 2 < argc) {
                    std::string key = argv[++i];
                    std::string value = argv[++i];
                    if (config_manager.set_config(key, value)) {
                        config_manager.save_user_config();
                        std::cout << "Set " << key << " = " << value << std::endl;
                        exit(0);
                    } else {
                        std::cerr << "Failed to set config: " << key << std::endl;
                        exit(1);
                    }
                } else if (config_cmd == "get" && i + 1 < argc) {
                    std::string key = argv[++i];
                    auto value = config_manager.get_config(key);
                    if (value) {
                        std::cout << key << " = " << *value << std::endl;
                    } else {
                        std::cout << key << " is not set" << std::endl;
                    }
                    exit(0);
                } else if (config_cmd == "unset" && i + 1 < argc) {
                    std::string key = argv[++i];
                    if (config_manager.unset_config(key)) {
                        config_manager.save_user_config();
                        std::cout << "Unset " << key << std::endl;
                        exit(0);
                    } else {
                        std::cerr << "Failed to unset config: " << key << std::endl;
                        exit(1);
                    }
                } else if (config_cmd == "reset") {
                    config_manager.reset_config();
                    config_manager.save_user_config();
                    std::cout << "Configuration reset to defaults" << std::endl;
                    exit(0);
                } else {
                    std::cerr << "Unknown config command: " << config_cmd << std::endl;
                    std::cerr << "Available commands: list, set <key> <value>, get <key>, unset <key>, reset" << std::endl;
                    exit(1);
                }
            } else {
                std::cerr << "Config command requires a subcommand" << std::endl;
                return 1;
            }
        }
        else if (arg == "--no-timestamps")                   { params.no_timestamps = true; }

        else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            whisper_print_usage(argc, argv, params);
            exit(0);
        }
    }

    return true;
}

void whisper_print_usage(int /*argc*/, char ** argv, const whisper_params & params) {
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -h,       --help          [default] show this help message and exit\n");
    fprintf(stderr, "  -t N,     --threads N     [%-7d] number of threads to use during computation\n",    params.n_threads);
    fprintf(stderr, "            --step N        [%-7d] audio step size in milliseconds\n",                params.step_ms);
    fprintf(stderr, "            --length N      [%-7d] audio length in milliseconds\n",                   params.length_ms);
    fprintf(stderr, "            --keep N        [%-7d] audio to keep from previous step in ms\n",         params.keep_ms);
    fprintf(stderr, "  -c ID,    --capture ID    [%-7d] capture device ID\n",                              params.capture_id);
    fprintf(stderr, "  -mt N,    --max-tokens N  [%-7d] maximum number of tokens per audio chunk\n",       params.max_tokens);
    fprintf(stderr, "  -ac N,    --audio-ctx N   [%-7d] audio context size (0 - all)\n",                   params.audio_ctx);
    fprintf(stderr, "  -bs N,    --beam-size N   [%-7d] beam size for beam search\n",                      params.beam_size);
    fprintf(stderr, "  -vth N,   --vad-thold N   [%-7.2f] voice activity detection threshold\n",           params.vad_thold);
    fprintf(stderr, "  -fth N,   --freq-thold N  [%-7.2f] high-pass frequency cutoff\n",                   params.freq_thold);
    fprintf(stderr, "  -tr,      --translate     [%-7s] translate from source language to english\n",      params.translate ? "true" : "false");
    fprintf(stderr, "  -nf,      --no-fallback   [%-7s] do not use temperature fallback while decoding\n", params.no_fallback ? "true" : "false");
    fprintf(stderr, "  -ps,      --print-special [%-7s] print special tokens\n",                           params.print_special ? "true" : "false");
    fprintf(stderr, "  -pc,      --print-colors  [%-7s] print colors based on token confidence\n",         params.print_colors ? "true" : "false");
    fprintf(stderr, "  -kc,      --keep-context  [%-7s] keep context between audio chunks\n",              params.no_context ? "false" : "true");
    fprintf(stderr, "  -l LANG,  --language LANG [%-7s] spoken language\n",                                params.language.c_str());
    fprintf(stderr, "  -m FNAME, --model FNAME   [%-7s] model path\n",                                     params.model.c_str());
    fprintf(stderr, "  -f FNAME, --file FNAME    [%-7s] text output file name\n",                          params.fname_out.c_str());
    fprintf(stderr, "  -om MODE, --output-mode MODE [%-7s] output mode: original, english, bilingual\n",    params.output_mode.c_str());
    fprintf(stderr, "  -tdrz,    --tinydiarize   [%-7s] enable speaker segmentation (requires tdrz model)\n", params.tinydiarize ? "true" : "false");
    fprintf(stderr, "  -sa,      --save-audio    [%-7s] save the recorded audio to a file\n",              params.save_audio ? "true" : "false");
    fprintf(stderr, "  -ng,      --no-gpu        [%-7s] disable GPU inference\n",                          params.use_gpu ? "false" : "true");
    fprintf(stderr, "  -fa,      --flash-attn    [%-7s] flash attention during inference\n",               params.flash_attn ? "true" : "false");
    // CoreML specific help
    fprintf(stderr, "  -coreml,  --coreml        [%-7s] enable CoreML acceleration (macOS)\n",             params.use_coreml ? "true" : "false");
    fprintf(stderr, "  -ncoreml, --no-coreml     [%-7s] disable CoreML acceleration\n",                    params.use_coreml ? "false" : "true");
    fprintf(stderr, "  -cm FNAME,--coreml-model FNAME [%-7s] CoreML model path\n",                        params.coreml_model.c_str());
    fprintf(stderr, "\n");
    fprintf(stderr, "auto-copy options:\n");
    fprintf(stderr, "            --auto-copy     [%-7s] automatically copy transcription to clipboard when session ends\n", params.auto_copy_enabled ? "true" : "false");
    fprintf(stderr, "            --no-auto-copy  [%-7s] disable auto-copy functionality\n",                params.auto_copy_enabled ? "false" : "true");
    fprintf(stderr, "            --auto-copy-max-duration N [%-7d] max session duration in hours before skipping auto-copy\n", params.auto_copy_max_duration_hours);
    fprintf(stderr, "            --auto-copy-max-size N     [%-7d] max transcription size in bytes before skipping auto-copy\n", params.auto_copy_max_size_bytes);
    fprintf(stderr, "\n");
    fprintf(stderr, "export options:\n");
    fprintf(stderr, "            --export        [%-7s] enable transcription export when session ends\n", params.export_enabled ? "true" : "false");
    fprintf(stderr, "            --no-export     [%-7s] disable transcription export\n",                params.export_enabled ? "false" : "true");
    fprintf(stderr, "            --export-format FORMAT [%-7s] export format: txt, md, json, csv, srt, vtt, xml\n", params.export_format.c_str());
    fprintf(stderr, "            --export-file FILE      [%-7s] export to specific file (default: auto-generated)\n", params.export_file.c_str());
    fprintf(stderr, "            --export-auto-filename  [%-7s] generate automatic filename\n",          params.export_auto_filename ? "true" : "false");
    fprintf(stderr, "            --export-no-metadata    [%-7s] exclude session metadata from export\n", params.export_include_metadata ? "false" : "true");
    fprintf(stderr, "            --export-no-timestamps  [%-7s] exclude timestamps from export\n",     params.export_include_timestamps ? "false" : "true");
    fprintf(stderr, "            --export-include-confidence [%-7s] include confidence scores in export\n", params.export_include_confidence ? "true" : "false");
    fprintf(stderr, "\n");
    fprintf(stderr, "meeting organization:\n");
    fprintf(stderr, "            --meeting        [%-7s] enable meeting transcription mode\n", params.meeting_mode ? "true" : "false");
    fprintf(stderr, "            --no-meeting     [%-7s] disable meeting mode (when enabled via config)\n", params.meeting_mode ? "false" : "true");
    fprintf(stderr, "            --prompt PROMPT  [%-7s] custom prompt text or file path for meeting organization\n", params.meeting_prompt.empty() ? "default" : "custom");
    fprintf(stderr, "            --name NAME      [%-7s] meeting output filename prefix\n", params.meeting_name.empty() ? "auto" : params.meeting_name.c_str());
    fprintf(stderr, "            --meeting-timeout N [%-4d] timeout for Claude CLI in seconds\n", params.meeting_timeout);
    fprintf(stderr, "            --meeting-max-single-pass N [%-4d] max words before multi-pass\n", params.meeting_max_single_pass);
    fprintf(stderr, "\n");
    fprintf(stderr, "accuracy options:\n");
    fprintf(stderr, "            --initial-prompt TEXT [%-7s] initial prompt for conditioning\n", params.initial_prompt.empty() ? "none" : "set");
    fprintf(stderr, "            --suppress-regex PAT  [%-7s] regex pattern to suppress from output\n", params.suppress_regex.empty() ? "none" : "set");
    fprintf(stderr, "            --vad-model PATH      [%-7s] Silero VAD model path for speech detection\n", params.vad_model_path.empty() ? "none" : "set");
    fprintf(stderr, "            --silence-timeout N   [%-7.1f] auto-stop after N seconds of silence (0 = disabled)\n", params.silence_timeout);
    fprintf(stderr, "\n");
    fprintf(stderr, "model management:\n");
    fprintf(stderr, "            --list-models      list available models for download\n");
    fprintf(stderr, "            --list-downloaded  list downloaded models with sizes and paths\n");
    fprintf(stderr, "            --show-storage     show detailed storage usage\n");
    fprintf(stderr, "            --delete-model MODEL  delete a specific model\n");
    fprintf(stderr, "            --delete-all-models    delete all downloaded models\n");
    fprintf(stderr, "            --cleanup          remove orphaned model files\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "configuration:\n");
    fprintf(stderr, "  config list                 show current configuration\n");
    fprintf(stderr, "  config set <key> <value>    set configuration value\n");
    fprintf(stderr, "  config get <key>            get configuration value\n");
    fprintf(stderr, "  config unset <key>          unset configuration value\n");
    fprintf(stderr, "  config reset                reset all configuration to defaults\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "examples:\n");
    fprintf(stderr, "  %s                                    # interactive model selection\n", argv[0]);
    fprintf(stderr, "  %s -m base.en                        # download and use base.en model\n", argv[0]);
    fprintf(stderr, "  %s -m tiny.en --step 0 --length 30000 # VAD mode with tiny model\n", argv[0]);
    fprintf(stderr, "  %s --list-models                      # show available models\n", argv[0]);
    fprintf(stderr, "  %s --list-downloaded                  # show downloaded models\n", argv[0]);
    fprintf(stderr, "  %s --show-storage                     # show storage usage\n", argv[0]);
    fprintf(stderr, "  %s --delete-model base.en             # delete specific model\n", argv[0]);
    fprintf(stderr, "  %s --cleanup                          # remove orphaned files\n", argv[0]);
    fprintf(stderr, "  %s --export --export-format json      # export transcription to JSON\n", argv[0]);
    fprintf(stderr, "  %s --export --export-format md --export-file meeting.md # export to Markdown\n", argv[0]);
    fprintf(stderr, "  %s --export --export-format srt       # generate SRT subtitles\n", argv[0]);
    fprintf(stderr, "  %s --auto-copy                        # auto-copy to clipboard\n", argv[0]);
    fprintf(stderr, "  %s --output-mode bilingual            # show original + English translation\n", argv[0]);
    fprintf(stderr, "  %s --output-mode english -l zh        # translate Chinese to English only\n", argv[0]);
    fprintf(stderr, "  %s config set model base.en           # set default model\n", argv[0]);
    fprintf(stderr, "  %s config set export_enabled true     # enable auto-export\n", argv[0]);
    fprintf(stderr, "  %s config list                        # show current config\n", argv[0]);
    fprintf(stderr, "  %s --meeting                         # organize meeting transcription (saves to [YYYY]-[MM]-[DD].md)\n", argv[0]);
    fprintf(stderr, "  %s --meeting --prompt custom.txt      # use custom prompt file\n", argv[0]);
    fprintf(stderr, "\n");
}

int main(int argc, char ** argv) {
    ggml_backend_load_all();

    // Register signal handler for graceful shutdown
    signal(SIGINT, signal_handler);

    whisper_params params;

    // Load configuration before parsing command line
    ConfigManager config_manager;
    config_manager.load_config();
    config_manager.apply_to_params(params);

    if (whisper_params_parse(argc, argv, params) == false) {
        return 1;
    }

    if (params.silence_timeout < 0.0f) {
        fprintf(stderr, "error: --silence-timeout must be non-negative\n");
        return 1;
    }

    // Initialize model manager
    ModelManager model_manager;
    
    // Apply configured models directory if set
    ConfigData effective_config = config_manager.get_effective_config();
    if (effective_config.models_directory) {
        model_manager.set_models_directory(*effective_config.models_directory);
    }

    // Handle special commands
    if (params.list_models) {
        model_manager.list_available_models();
        return 0;
    }
    
    if (params.list_downloaded) {
        model_manager.list_downloaded_models();
        return 0;
    }
    
    if (params.show_storage) {
        model_manager.show_storage_usage();
        return 0;
    }
    
    if (params.delete_model_flag) {
        bool success = model_manager.delete_model(params.model_to_delete, true);
        return success ? 0 : 1;
    }
    
    if (params.delete_all_models_flag) {
        bool success = model_manager.delete_all_models(true);
        return success ? 0 : 1;
    }
    
    if (params.cleanup_models) {
        model_manager.cleanup_orphaned_files();
        return 0;
    }

    // Resolve model (with auto-download if needed)
    std::string resolved_model = model_manager.resolve_model(params.model, params.use_coreml);
    if (resolved_model.empty()) {
        std::cerr << "\n❌ No model available. Exiting.\n";
        return 1;
    }
    
    // Update params with resolved model path
    params.model = resolved_model;
    
    // Auto-set CoreML model path if CoreML is enabled and not explicitly set
    if (params.use_coreml && params.coreml_model.empty()) {
        // Extract model name from the resolved path
        std::filesystem::path model_path(resolved_model);
        std::string model_filename = model_path.filename().string();
        
        // Try to find corresponding model name
        for (const auto& name : model_manager.get_model_names()) {
            if (model_manager.get_model_path(name) == resolved_model) {
                std::string coreml_path = model_manager.get_coreml_model_path(name);
                if (model_manager.coreml_model_exists(name)) {
                    params.coreml_model = coreml_path;
                    std::cerr << "✅ Auto-detected CoreML model: " << coreml_path << "\n";
                } else {
                    std::cerr << "⚠️  CoreML enabled but model not available: " << coreml_path << "\n";
                    params.use_coreml = false;  // Disable CoreML to prevent crashes
                }
                break;
            }
        }
    }

    // Adjust thread count based on CoreML availability
    // CoreML handles the encoder on ANE, so fewer CPU threads are needed for the decoder
    {
        int default_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
        if (params.use_coreml && params.n_threads == default_threads) {
            params.n_threads = 4; // CoreML: 4 is optimal for decoder-only CPU work
        } else if (!params.use_coreml && params.n_threads <= 4) {
            params.n_threads = std::min(8, (int32_t)std::thread::hardware_concurrency());
        }
    }

    // Resolve VAD model path (supports "auto" for auto-download)
    if (!params.vad_model_path.empty()) {
        params.vad_model_path = model_manager.resolve_vad_model(params.vad_model_path);
    }

    // Meeting mode: apply optimized defaults (can still be overridden by explicit CLI args)
    // These are set after CLI parsing so they only apply if the user didn't explicitly set them
    if (params.meeting_mode) {
        // Auto-enable speaker turn detection for meetings
        if (!params.tinydiarize) params.tinydiarize = true;
        // Better accuracy defaults for meeting transcription
        if (params.keep_ms == 200) params.keep_ms = 1000;      // Research: 1000ms optimal overlap
        if (params.step_ms == 3000) params.step_ms = 5000;     // Longer processing windows
        if (params.length_ms == 10000) params.length_ms = 15000;
        if (params.beam_size <= 0) params.beam_size = 5;       // Beam search for meetings
        if (params.freq_thold == 100.0f) params.freq_thold = 200.0f; // Better high-pass filter
        if (params.initial_prompt.empty()) {
            params.initial_prompt = "Meeting transcription with proper punctuation and capitalization.";
        }
    }

    params.keep_ms   = std::min(params.keep_ms,   params.step_ms);
    params.length_ms = std::max(params.length_ms, params.step_ms);

    const int n_samples_step = (1e-3*params.step_ms  )*WHISPER_SAMPLE_RATE;
    const int n_samples_len  = (1e-3*params.length_ms)*WHISPER_SAMPLE_RATE;
    const int n_samples_keep = (1e-3*params.keep_ms  )*WHISPER_SAMPLE_RATE;
    const int n_samples_30s  = (1e-3*30000.0         )*WHISPER_SAMPLE_RATE;

    const bool use_vad = n_samples_step <= 0; // sliding window mode uses VAD

    const int n_new_line = !use_vad ? std::max(1, params.length_ms / params.step_ms - 1) : 1;

    params.no_timestamps  = !use_vad;
    // In meeting mode, keep context between chunks for better accuracy
    if (!params.meeting_mode) {
        params.no_context |= use_vad;
    }
    if (use_vad) params.max_tokens = 0;

    // Init audio
    audio_async audio(params.length_ms);
    if (!audio.init(params.capture_id, WHISPER_SAMPLE_RATE)) {
        fprintf(stderr, "%s: audio.init() failed!\n", __func__);
        return 1;
    }

    audio.resume();
    
    // Set recording state for signal handler
    g_is_recording.store(true);

    // Whisper init with CoreML support
    if (params.language != "auto" && whisper_lang_id(params.language.c_str()) == -1){
        fprintf(stderr, "error: unknown language '%s'\n", params.language.c_str());
        whisper_print_usage(argc, argv, params);
        exit(0);
    }

    struct whisper_context_params cparams = whisper_context_default_params();

    // Configure CoreML if available and requested
    #ifdef WHISPER_COREML
    if (params.use_coreml) {
        cparams.use_gpu = false;  // CoreML handles GPU acceleration
        fprintf(stderr, "%s: CoreML acceleration enabled\n", __func__);
    } else {
        cparams.use_gpu = params.use_gpu;
    }
    #else
    cparams.use_gpu = params.use_gpu;
    if (params.use_coreml) {
        fprintf(stderr, "%s: WARNING: CoreML requested but not compiled with CoreML support\n", __func__);
    }
    #endif

    cparams.flash_attn = params.flash_attn;

    struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);
    if (ctx == nullptr) {
        fprintf(stderr, "error: failed to initialize whisper context\n");
        return 2;
    }

    // CoreML warm-up: first inference triggers ANE compilation (2-5s delay)
    // Running a dummy inference at startup moves this cost before "Start speaking"
    #ifdef WHISPER_COREML
    if (params.use_coreml) {
        fprintf(stderr, "%s: warming up CoreML...\n", __func__);
        std::vector<float> warmup(WHISPER_SAMPLE_RATE * 1, 0.0f); // 1 second of silence
        whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wp.print_realtime   = false;
        wp.print_progress   = false;
        wp.print_timestamps = false;
        wp.print_special    = false;
        wp.n_threads        = params.n_threads;
        whisper_full(ctx, wp, warmup.data(), warmup.size());
        fprintf(stderr, "%s: CoreML ready\n", __func__);
    }
    #endif

    // Validate output mode
    if (params.output_mode != "original" && params.output_mode != "english" && params.output_mode != "bilingual") {
        fprintf(stderr, "error: invalid output mode '%s'. Valid modes: original, english, bilingual\n", params.output_mode.c_str());
        return 1;
    }
    
    // Check compatibility between translate flag and output mode
    if (params.translate) {
        // If --translate is used, default to "english" mode for compatibility
        if (params.output_mode == "original") {
            params.output_mode = "english";
            fprintf(stderr, "%s: --translate flag detected, switching to 'english' output mode\n", __func__);
        }
    }
    
    // Check if translation is supported for non-original modes
    bool needs_translation = (params.output_mode == "english" || params.output_mode == "bilingual");
    if (needs_translation && !whisper_is_multilingual(ctx)) {
        fprintf(stderr, "error: output mode '%s' requires a multilingual model, but current model is English-only\n", params.output_mode.c_str());
        return 1;
    }
    
    // For bilingual mode, we need a second context for translation
    struct whisper_context * ctx_translate = nullptr;
    if (params.output_mode == "bilingual") {
        ctx_translate = whisper_init_from_file_with_params(params.model.c_str(), cparams);
        if (ctx_translate == nullptr) {
            fprintf(stderr, "error: failed to initialize translation context for bilingual mode\n");
            whisper_free(ctx);
            return 2;
        }
    }

    std::vector<float> pcmf32    (n_samples_30s, 0.0f);
    std::vector<float> pcmf32_old;
    pcmf32_old.reserve(n_samples_30s);
    std::vector<float> pcmf32_new(n_samples_30s, 0.0f);

    std::vector<whisper_token> prompt_tokens;

    // Print processing info with CoreML status
    {
        fprintf(stderr, "\n");
        if (!whisper_is_multilingual(ctx)) {
            if (params.language != "en" || params.translate) {
                params.language = "en";
                params.translate = false;
                fprintf(stderr, "%s: WARNING: model is not multilingual, ignoring language and translation options\n", __func__);
            }
        }
        
        #ifdef WHISPER_COREML
        fprintf(stderr, "%s: CoreML support: %s\n", __func__, params.use_coreml ? "enabled" : "disabled");
        #else
        fprintf(stderr, "%s: CoreML support: not compiled\n", __func__);
        #endif
        
        fprintf(stderr, "%s: processing %d samples (step = %.1f sec / len = %.1f sec / keep = %.1f sec), %d threads, lang = %s, task = %s, output_mode = %s, timestamps = %d ...\n",
                __func__,
                n_samples_step,
                float(n_samples_step)/WHISPER_SAMPLE_RATE,
                float(n_samples_len )/WHISPER_SAMPLE_RATE,
                float(n_samples_keep)/WHISPER_SAMPLE_RATE,
                params.n_threads,
                params.language.c_str(),
                params.translate ? "translate" : "transcribe",
                params.output_mode.c_str(),
                params.no_timestamps ? 0 : 1);

        if (!use_vad) {
            fprintf(stderr, "%s: n_new_line = %d, no_context = %d\n", __func__, n_new_line, params.no_context);
        } else {
            fprintf(stderr, "%s: using VAD, will transcribe on speech activity\n", __func__);
        }

        if (params.print_colors) {
            fprintf(stderr, "%s: color scheme: red (low confidence), yellow (medium), green (high confidence)\n", __func__);
        }

        fprintf(stderr, "\n");
    }

    int n_iter = 0;
    bool is_running = true;

    const bool stdout_is_tty = isatty(STDOUT_FILENO);
    // When stdout is not a TTY (redirected to file/pipe), accumulate text
    // and dump it on exit instead of streaming with ANSI codes
    std::string pipe_finalized_text;
    std::ostringstream pipe_current_text;

    std::ofstream fout;
    if (params.fname_out.length() > 0) {
        fout.open(params.fname_out);
        if (!fout.is_open()) {
            fprintf(stderr, "%s: failed to open output file '%s'!\n", __func__, params.fname_out.c_str());
            return 1;
        }
    }

    wav_writer wavWriter;
    if (params.save_audio) {
        time_t now = time(0);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", localtime(&now));
        std::string filename = std::string(buffer) + ".wav";
        wavWriter.open(filename, WHISPER_SAMPLE_RATE, 16, 1);
    }
    
    fprintf(stderr, "[Start speaking]\n");
    fflush(stderr);

    // Initialize auto-copy session
    AutoCopySession auto_copy_session;
    if (params.auto_copy_enabled) {
        fprintf(stderr, "Auto-copy enabled (Session ID: %s, Max Duration: %d hours, Max Size: %d bytes)\n",
               auto_copy_session.session_id.c_str(),
               params.auto_copy_max_duration_hours,
               params.auto_copy_max_size_bytes);
    }
    
    // Initialize export session
    ExportSession export_session;
    if (params.export_enabled) {
        fprintf(stderr, "Export enabled (Session ID: %s, Format: %s, File: %s)\n",
               export_session.session_id.c_str(),
               params.export_format.c_str(),
               params.export_auto_filename ? "auto-generated" : params.export_file.c_str());

        // Setup session metadata
        export_session.metadata.session_id = export_session.session_id;
        export_session.metadata.start_time = std::chrono::system_clock::now();
        export_session.metadata.model_name = params.model;
        export_session.metadata.language = params.language;
        export_session.metadata.coreml_enabled = params.use_coreml;
        export_session.metadata.thread_count = params.n_threads;
        export_session.metadata.vad_threshold = params.vad_thold;
        export_session.metadata.step_ms = params.step_ms;
        export_session.metadata.length_ms = params.length_ms;
        export_session.metadata.version = "recognize-1.0.0";
    }

    // Initialize speaker tracker and meeting session
    SpeakerTracker speaker_tracker;
    MeetingSession meeting_session;
    if (params.meeting_mode) {
        std::string output_filename = generate_meeting_filename(params.meeting_name);
        fprintf(stderr, "Meeting mode enabled (Session ID: %s, Output: %s)\n",
               meeting_session.session_id.c_str(), output_filename.c_str());
        fprintf(stderr, "Note: Will save to [YYYY]-[MM]-[DD].md with AI organization (or raw transcription on failure).\n");
    }

    auto t_last  = std::chrono::high_resolution_clock::now();
    const auto t_start = t_last;

    // Silence timeout state
    auto t_last_speech = std::chrono::high_resolution_clock::now();
    bool has_spoken = false;
    const bool silence_timeout_enabled = params.silence_timeout > 0.0f && !params.meeting_mode;

    // Main audio processing loop
    while (is_running && !check_interrupt_with_confirmation()) {
        if (params.save_audio) {
            wavWriter.write(pcmf32_new.data(), pcmf32_new.size());
        }

        is_running = sdl_poll_events();
        if (!is_running || check_interrupt_with_confirmation()) {
            break;
        }

        // Process new audio
        if (!use_vad) {
            while (true) {
                is_running = sdl_poll_events();
                if (!is_running || check_interrupt_with_confirmation()) {
                    break;
                }
                
                audio.get(params.step_ms, pcmf32_new);

                if ((int) pcmf32_new.size() > 2*n_samples_step) {
                    fprintf(stderr, "\n\n%s: WARNING: cannot process audio fast enough, dropping audio ...\n\n", __func__);
                    audio.clear();
                    continue;
                }

                if ((int) pcmf32_new.size() >= n_samples_step) {
                    audio.clear();
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            const int n_samples_new = pcmf32_new.size();
            const int n_samples_take = std::min((int) pcmf32_old.size(), std::max(0, n_samples_keep + n_samples_len - n_samples_new));

            pcmf32.resize(n_samples_new + n_samples_take);

            for (int i = 0; i < n_samples_take; i++) {
                pcmf32[i] = pcmf32_old[pcmf32_old.size() - n_samples_take + i];
            }

            memcpy(pcmf32.data() + n_samples_take, pcmf32_new.data(), n_samples_new*sizeof(float));
            pcmf32_old = pcmf32;
        } else {
            const auto t_now  = std::chrono::high_resolution_clock::now();
            const auto t_diff = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_last).count();

            if (t_diff < 2000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            audio.get(2000, pcmf32_new);

            if (::vad_simple(pcmf32_new, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, false)) {
                audio.get(params.length_ms, pcmf32);
                if (silence_timeout_enabled) {
                    has_spoken = true;
                    t_last_speech = std::chrono::high_resolution_clock::now();
                }
            } else {
                if (silence_timeout_enabled && has_spoken) {
                    const auto t_silence = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - t_last_speech).count();
                    if (t_silence >= static_cast<int64_t>(params.silence_timeout * 1000.0f)) {
                        fprintf(stderr, "[Silence timeout: %.1fs, auto-stopping]\n", params.silence_timeout);
                        is_running = false;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            t_last = t_now;
        }

        // Run inference via bilingual processing pipeline
        {
            // Note: whisper_full_params are configured inside process_audio_segment()
            std::vector<BilingualSegment> bilingual_results;
            if (process_audio_segment(ctx, ctx_translate, params, pcmf32, bilingual_results) != 0) {
                fprintf(stderr, "%s: failed to process audio\n", argv[0]);
                return 6;
            }

            // Apply hallucination filter
            for (auto& seg : bilingual_results) {
                if (!seg.original_text.empty()) {
                    seg.original_text = filter_hallucinations(seg.original_text);
                }
                if (!seg.english_text.empty()) {
                    seg.english_text = filter_hallucinations(seg.english_text);
                }
            }
            // Remove segments where both texts became empty after filtering
            bilingual_results.erase(
                std::remove_if(bilingual_results.begin(), bilingual_results.end(),
                    [](const BilingualSegment& s) {
                        return s.original_text.empty() && s.english_text.empty();
                    }),
                bilingual_results.end());

            // Silence timeout tracking (non-VAD path)
            if (silence_timeout_enabled) {
                if (!bilingual_results.empty()) {
                    has_spoken = true;
                    t_last_speech = std::chrono::high_resolution_clock::now();
                } else if (has_spoken) {
                    const auto t_silence = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - t_last_speech).count();
                    if (t_silence >= static_cast<int64_t>(params.silence_timeout * 1000.0f)) {
                        fprintf(stderr, "[Silence timeout: %.1fs, auto-stopping]\n", params.silence_timeout);
                        is_running = false;
                        break;
                    }
                }
            }

            // Print results
            {
                // For pipe mode: clear current buffer before re-rendering
                if (!stdout_is_tty && !use_vad) {
                    pipe_current_text.str("");
                    pipe_current_text.clear();
                }

                if (!use_vad) {
                    if (stdout_is_tty) {
                        printf("\33[2K\r");
                        printf("%s", std::string(100, ' ').c_str());
                        printf("\33[2K\r");
                    }
                } else {
                    const int64_t t1 = (t_last - t_start).count()/1000000;
                    const int64_t t0 = std::max(0.0, t1 - pcmf32.size()*1000.0/WHISPER_SAMPLE_RATE);
                    if (stdout_is_tty) {
                        printf("\n### Transcription %d START | t0 = %d ms | t1 = %d ms\n", n_iter, (int) t0, (int) t1);
                        printf("\n");
                    }
                }

                // Use colored token output if enabled, otherwise use segment-based output
                if (params.print_colors) {
                    // Print tokens directly from whisper context with colors
                    const int n_segments = whisper_full_n_segments(ctx);
                    for (int i = 0; i < n_segments; ++i) {
                        if (stdout_is_tty) {
                            if (!params.no_timestamps) {
                                const int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                                const int64_t t1 = whisper_full_get_segment_t1(ctx, i);
                                printf("[%s --> %s]  ", to_timestamp(t0).c_str(), to_timestamp(t1).c_str());
                            }
                            print_colored_tokens(ctx, i, params);
                            printf("\n");
                        }

                        // Accumulate text for meeting/auto-copy/export/pipe even in color mode
                        const char* seg_text = whisper_full_get_segment_text(ctx, i);
                        bool speaker_turn = whisper_full_get_segment_speaker_turn_next(ctx, i);

                        // Use shared speaker tracker
                        int seg_speaker_id = speaker_tracker.get_current();
                        if (speaker_turn) {
                            seg_speaker_id = speaker_tracker.on_turn();
                        }

                        if (!stdout_is_tty) {
                            pipe_current_text << seg_text;
                        }
                        if (params.meeting_mode) {
                            meeting_session.add_transcription(std::string(seg_text), speaker_turn);
                            meeting_session.add_transcription(" ");
                        }
                        if (params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) {
                            auto_copy_session.transcription_buffer << seg_text;
                        }
                        if (params.export_enabled) {
                            int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                            int64_t t1 = whisper_full_get_segment_t1(ctx, i);
                            export_session.segments.emplace_back(t0 / 10, t1 / 10, std::string(seg_text), 1.0f, speaker_turn, speaker_turn ? seg_speaker_id : -1);
                        }
                    }
                } else {
                    // Use segment-based bilingual output
                    std::ostringstream* pbuf = stdout_is_tty ? nullptr : &pipe_current_text;
                    print_bilingual_results(bilingual_results, params, auto_copy_session, export_session, speaker_tracker, &meeting_session,
                                            stdout_is_tty, pbuf);
                }

                if (params.fname_out.length() > 0) {
                    const int n_seg = whisper_full_n_segments(ctx);
                    for (int i = 0; i < n_seg; ++i) {
                        const char* seg_text = whisper_full_get_segment_text(ctx, i);
                        if (seg_text) {
                            fout << seg_text;
                        }
                    }
                    fout << std::endl;
                    fout.flush();
                }

                if (use_vad) {
                    if (stdout_is_tty) {
                        printf("\n### Transcription %d END\n", n_iter);
                    }
                    // In VAD mode, each iteration is independent — finalize immediately
                    if (!stdout_is_tty) {
                        pipe_finalized_text += pipe_current_text.str();
                        pipe_current_text.str("");
                        pipe_current_text.clear();
                    }
                }
            }

            ++n_iter;

            if (!use_vad && (n_iter % n_new_line) == 0) {
                if (stdout_is_tty) {
                    printf("\n");
                }
                // Finalize current group's text for pipe output
                if (!stdout_is_tty) {
                    pipe_finalized_text += pipe_current_text.str();
                    pipe_current_text.str("");
                    pipe_current_text.clear();
                }
                pcmf32_old = std::vector<float>(pcmf32.end() - n_samples_keep, pcmf32.end());

                if (!params.no_context) {
                    prompt_tokens.clear();
                    const int n_segments = whisper_full_n_segments(ctx);
                    for (int i = 0; i < n_segments; ++i) {
                        const int token_count = whisper_full_n_tokens(ctx, i);
                        for (int j = 0; j < token_count; ++j) {
                            prompt_tokens.push_back(whisper_full_get_token_id(ctx, i, j));
                        }
                    }
                }
            }
            fflush(stdout);
        }
    }

    audio.pause();

    // Dump accumulated text to stdout when not a TTY (pipe/redirect mode)
    if (!stdout_is_tty) {
        std::string final_text = pipe_finalized_text + pipe_current_text.str();
        if (!final_text.empty()) {
            printf("%s\n", final_text.c_str());
            fflush(stdout);
        }
    }

    // Perform auto-copy when session ends
    if (params.auto_copy_enabled) {
        perform_auto_copy(auto_copy_session, params);
    }
    
    // Perform export when session ends
    if (params.export_enabled) {
        export_session.metadata.total_speakers = speaker_tracker.total_speakers;
        perform_export(export_session, params);
    }

    // Perform meeting processing when session ends
    if (params.meeting_mode) {
        std::string meeting_output_file = generate_meeting_filename(params.meeting_name);
        // Sync speaker count from shared tracker
        meeting_session.total_speakers = std::max(meeting_session.total_speakers, speaker_tracker.total_speakers);
        double duration_minutes = meeting_session.get_duration_minutes();
        std::cerr << "\nProcessing meeting transcription with Claude CLI..." << std::endl;
        std::cerr << "Duration: " << static_cast<int>(duration_minutes) << " minutes, "
                  << "Speakers detected: " << meeting_session.total_speakers << std::endl;

        bool success = process_meeting_transcription(
            meeting_session.get_transcription(),
            params.meeting_prompt,
            meeting_output_file,
            params.meeting_timeout,
            duration_minutes,
            params.meeting_max_single_pass
        );

        if (!success) {
            // Fallback: save raw transcription to the same date-based filename
            std::ofstream raw_file(meeting_output_file);
            if (raw_file.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm* tm_ptr = std::localtime(&time_t);

                raw_file << "# Meeting Transcription\n\n";
                raw_file << "**Date**: " << std::put_time(tm_ptr, "%Y-%m-%d %H:%M") << "\n";
                raw_file << "**Duration**: " << static_cast<int>(duration_minutes) << " minutes\n";
                raw_file << "**Speakers**: " << meeting_session.total_speakers << "\n";
                raw_file << "**Session ID**: " << meeting_session.session_id << "\n\n";
                raw_file << "---\n\n";
                raw_file << "## Raw Transcription\n\n";
                raw_file << meeting_session.get_transcription();
                raw_file.close();
                std::cerr << "Transcription saved to: " << meeting_output_file << std::endl;
            } else {
                std::cerr << "Failed to save transcription to file" << std::endl;
            }
        }
    }

    // Clear recording state
    g_is_recording.store(false);
    
    whisper_print_timings(ctx);
    whisper_free(ctx);
    
    // Clean up translation context if it was created
    if (ctx_translate) {
        whisper_free(ctx_translate);
    }

    return 0;
}