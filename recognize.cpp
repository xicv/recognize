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

// Global state for signal handling
std::atomic<bool> g_interrupt_received(false);
std::atomic<bool> g_is_recording(false);

// Default meeting organization prompt
const std::string DEFAULT_MEETING_PROMPT = R"(
You are an expert meeting organizer and transcription analyst. Please organize this raw meeting transcription into a structured, actionable format.

## INPUT:
Raw meeting transcription: [Paste raw transcription here]

## OUTPUT REQUIREMENTS:

### 1. MEETING METADATA
- **Meeting Title**: Clear, descriptive title
- **Date & Time**: [Extract from transcription]
- **Duration**: [Estimate from content]
- **Attendees**: [List all speakers/participants]
- **Meeting Type**: [Stand-up, Planning, Review, Brainstorm, etc.]

### 2. EXECUTIVE SUMMARY
- **Main Objective**: What was the primary goal?
- **Key Outcomes**: 3-5 bullet points of major results
- **Critical Decisions**: Important decisions made
- **Next Meeting**: [If mentioned]

### 3. DETAILED AGENDA & DISCUSSION
**Organize by topics discussed:**

#### **Topic 1: [Topic Name]**
- **Discussion Points**: [Key points raised]
- **Decisions Made**: [Specific decisions]
- **Action Items**: [Tasks assigned]
- **Owner & Deadline**: [Who & when]

#### **Topic 2: [Topic Name]**
- [Repeat structure for each major topic]

### 4. ACTION ITEMS TRACKER
| Task | Owner | Deadline | Status | Priority |
|------|-------|----------|--------|----------|
| [Specific task] | [Person] | [Date] | [Not Started/In Progress/Done] | [High/Medium/Low] |

### 5. KEY DECISIONS LOG
1. **Decision**: [Clear statement]
   - **Rationale**: [Reasoning behind decision]
   - **Impact**: [What this affects]
   - **Made by**: [Who decided]

### 6. OPEN ISSUES & CONCERNS
| Issue | Raised By | Impact | Proposed Solution |
|-------|-----------|--------|------------------|
| [Issue] | [Person] | [High/Medium/Low] | [Solution suggested] |

### 7. FOLLOW-UP REQUIREMENTS
- **Immediate Actions** (24-48 hours):
- **Short-term** (1-2 weeks):
- **Long-term** (1+ month):

### 8. ADDITIONAL NOTES
- **Resources Mentioned**: [Links, documents, tools]
- **Budget/Financial Notes**: [If applicable]
- **Stakeholders Not Present**: [Missing key people]
- **Conflicts/Disagreements**: [Any tensions to resolve]

### 9. QUALITY IMPROVEMENT NOTES
- **Meeting Effectiveness**: [Rate 1-10, why?]
- **Time Management**: [Was time well-used?]
- **Participation**: [Was everyone engaged?]
- **Suggestions for Improvement**: [What could be better?]

## PROCESSING INSTRUCTIONS:

1. **Clean the transcription** first:
   - Remove filler words ("um", "uh", "like")
   - Fix obvious transcription errors
   - Identify different speakers
   - Remove repetitive content

2. **Identify structure**:
   - Group related topics
   - Extract key themes
   - Note decision points
   - Find action items

3. **Clarify ambiguities**:
   - Use [?] for unclear items
   - Note when timestamps would help
   - Mark items needing verification

4. **Format professionally**:
   - Use clear headings
   - Be concise but thorough
   - Prioritize action items
   - Make it scannable

5. **Add context**:
   - Note meeting atmosphere
   - Highlight urgency levels
   - Flag time-sensitive items
   - Identify dependencies

Please organize this transcription systematically and make it immediately actionable for all participants. Make it clear, concise, and actionable.

Pro Tips:

1. Before pasting: Clean up obvious transcription errors
2. For long meetings: Break into sections or topics
3. For technical meetings: Ask to preserve technical terms
4. For decision-heavy meetings: Emphasize the rationale section
5. For action-oriented meetings: Focus on the action items tracker
)";

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    if (signal == SIGINT) {
        if (g_is_recording.load()) {
            // If recording, ask for confirmation
            std::cout << "\n\n⚠️  Recording in progress! Are you sure you want to quit? (y/N): " << std::flush;
            
            // Set terminal to raw mode to read single character
            struct termios old_termios, new_termios;
            tcgetattr(STDIN_FILENO, &old_termios);
            new_termios = old_termios;
            new_termios.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
            
            char c = getchar();
            
            // Restore terminal mode
            tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
            
            if (c == 'y' || c == 'Y') {
                std::cout << "\n🛑 Stopping recording and exiting...\n" << std::endl;
                g_interrupt_received.store(true);
            } else {
                std::cout << "\n▶️  Continuing recording...\n" << std::endl;
                return;
            }
        } else {
            // Not recording, exit immediately
            std::cout << "\n🛑 Exiting...\n" << std::endl;
            g_interrupt_received.store(true);
        }
    }
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

    MeetingSession() {
        // Generate a unique session ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100000, 999999);
        session_id = std::to_string(dis(gen));
        start_time = std::chrono::high_resolution_clock::now();
    }

    void add_transcription(const std::string& text) {
        transcription_buffer << text;
    }

    std::string get_transcription() const {
        return transcription_buffer.str();
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
    if (!meeting_name.empty()) {
        // Use provided name (could be a path)
        return meeting_name;
    }

    // Generate default filename with current date
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << "meeting-";

    // Format: YYYY-MM-DD
    std::tm* tm_ptr = std::localtime(&time_t);
    oss << std::put_time(tm_ptr, "%Y-%m-%d");
    oss << ".md";

    return oss.str();
}

std::string generate_fallback_filename() {
    // Generate fallback filename with current date
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_ptr = std::localtime(&time_t);

    std::ostringstream base_name;
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

bool process_meeting_transcription(const std::string& transcription, const std::string& prompt, const std::string& output_file) {
    if (transcription.empty()) {
        std::cerr << "Error: Empty transcription for meeting processing" << std::endl;
        return false;
    }

    if (!is_claude_cli_available()) {
        std::cerr << "Error: Claude CLI not found. Please install Claude Code first." << std::endl;
        std::cerr << "Visit: https://claude.ai/code for installation instructions." << std::endl;
        return false;
    }

    // Use the provided prompt or default
    std::string effective_prompt = prompt.empty() ? DEFAULT_MEETING_PROMPT : prompt;

    // Replace the placeholder in the prompt with actual transcription
    std::string full_prompt = effective_prompt;
    size_t placeholder_pos = full_prompt.find("[Paste raw transcription here]");
    if (placeholder_pos != std::string::npos) {
        full_prompt.replace(placeholder_pos, std::string("[Paste raw transcription here]").length(), transcription);
    } else {
        // If placeholder not found, append transcription
        full_prompt += "\n\n## RAW TRANSCRIPTION:\n" + transcription;
    }

    // Construct the claude command
    std::ostringstream cmd;
    cmd << "claude -p \"" << full_prompt << "\"";

    // Execute claude command and capture output
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Failed to execute Claude CLI" << std::endl;
        return false;
    }

    // Read the output
    std::ostringstream output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output << buffer;
    }

    int exit_code = pclose(pipe);
    if (exit_code != 0) {
        std::cerr << "Error: Claude CLI execution failed with exit code " << exit_code << std::endl;
        return false;
    }

    // Write output to file
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create meeting output file: " << output_file << std::endl;
        return false;
    }

    file << output.str();
    file.close();

    std::cout << "✅ Meeting transcription processed and saved to: " << output_file << std::endl;
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
        printf("Auto-copy skipped: no content to copy.\n");
        return;
    }
    
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(now - session.start_time);
    
    // Check duration limit
    if (duration.count() > params.auto_copy_max_duration_hours) {
        printf("Auto-copy skipped: session duration (%ld hours) exceeded limit (%d hours).\n", 
               duration.count(), params.auto_copy_max_duration_hours);
        return;
    }
    
    // Check size limit
    if (content.size() > static_cast<size_t>(params.auto_copy_max_size_bytes)) {
        printf("Auto-copy skipped: content size (%zu bytes) exceeded limit (%d bytes).\n", 
               content.size(), params.auto_copy_max_size_bytes);
        return;
    }
    
    // Perform the copy
    if (copy_to_clipboard_macos(content)) {
        printf("Transcription copied.\n");
        session.has_been_copied = true;
    } else {
        printf("Auto-copy failed: unable to copy to clipboard.\n");
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

// Print bilingual results with proper formatting
void print_bilingual_results(const std::vector<BilingualSegment>& segments, const whisper_params& params,
                             AutoCopySession& auto_copy_session, ExportSession& export_session, MeetingSession* meeting_session = nullptr) {
    
    for (const auto& seg : segments) {
        if (params.no_timestamps) {
            // Plain text mode
            if (params.output_mode == "original") {
                if (params.print_colors) {
                    // Color printing not implemented for bilingual mode
                    printf("%s", seg.original_text.c_str());
                } else {
                    printf("%s", seg.original_text.c_str());
                }
                
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
                        seg.speaker_turn
                    );
                }

                // Add to meeting session
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text);
                }
            }
            else if (params.output_mode == "english") {
                printf("%s", seg.english_text.c_str());
                
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
                        seg.speaker_turn
                    );
                }

                // Add to meeting session
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.english_text);
                }
            }
            else if (params.output_mode == "bilingual") {
                // Detect language for prefixes
                std::string lang_code = params.language;
                if (lang_code == "auto") lang_code = "orig";
                
                printf("%s: %s\n", lang_code.c_str(), seg.original_text.c_str());
                printf("en: %s\n", seg.english_text.c_str());
                
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
                        seg.speaker_turn
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text);
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
                printf("%s%s", timestamp_prefix.c_str(), seg.original_text.c_str());
                if (seg.speaker_turn) printf(" [SPEAKER_TURN]");
                printf("\n");
                
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
                        seg.speaker_turn
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text);
                    meeting_session->add_transcription(" ");
                }
            }
            else if (params.output_mode == "english") {
                printf("%s%s", timestamp_prefix.c_str(), seg.english_text.c_str());
                if (seg.speaker_turn) printf(" [SPEAKER_TURN]");
                printf("\n");
                
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
                        seg.speaker_turn
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.english_text);
                    meeting_session->add_transcription(" ");
                }
            }
            else if (params.output_mode == "bilingual") {
                // Detect language for prefixes
                std::string lang_code = params.language;
                if (lang_code == "auto") lang_code = "orig";
                
                printf("%s%s: %s\n", timestamp_prefix.c_str(), lang_code.c_str(), seg.original_text.c_str());
                printf("%sen: %s", timestamp_prefix.c_str(), seg.english_text.c_str());
                if (seg.speaker_turn) printf(" [SPEAKER_TURN]");
                printf("\n");
                
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
                        seg.speaker_turn
                    );
                }

                // Add to meeting session (clean format, just the content)
                if (meeting_session && params.meeting_mode) {
                    meeting_session->add_transcription(seg.original_text);
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
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            whisper_print_usage(argc, argv, params);
            exit(0);
        }
        else if (arg == "-t"    || arg == "--threads")       { params.n_threads     = std::stoi(argv[++i]); }
        else if (                  arg == "--step")          { params.step_ms       = std::stoi(argv[++i]); }
        else if (                  arg == "--length")        { params.length_ms     = std::stoi(argv[++i]); }
        else if (                  arg == "--keep")          { params.keep_ms       = std::stoi(argv[++i]); }
        else if (arg == "-c"    || arg == "--capture")       { params.capture_id    = std::stoi(argv[++i]); }
        else if (arg == "-mt"   || arg == "--max-tokens")    { params.max_tokens    = std::stoi(argv[++i]); }
        else if (arg == "-ac"   || arg == "--audio-ctx")     { params.audio_ctx     = std::stoi(argv[++i]); }
        else if (arg == "-bs"   || arg == "--beam-size")     { params.beam_size     = std::stoi(argv[++i]); }
        else if (arg == "-vth"  || arg == "--vad-thold")     { params.vad_thold     = std::stof(argv[++i]); }
        else if (arg == "-fth"  || arg == "--freq-thold")    { params.freq_thold    = std::stof(argv[++i]); }
        else if (arg == "-tr"   || arg == "--translate")     { params.translate     = true; }
        else if (arg == "-nf"   || arg == "--no-fallback")   { params.no_fallback   = true; }
        else if (arg == "-ps"   || arg == "--print-special") { params.print_special = true; }
        else if (arg == "-pc"   || arg == "--print-colors")  { params.print_colors  = true; }
        else if (arg == "-kc"   || arg == "--keep-context")  { params.no_context    = false; }
        else if (arg == "-l"    || arg == "--language")      { params.language      = argv[++i]; }
        else if (arg == "-m"    || arg == "--model")         { params.model         = argv[++i]; }
        else if (arg == "-f"    || arg == "--file")          { params.fname_out     = argv[++i]; }
        else if (arg == "-om"   || arg == "--output-mode")   { params.output_mode   = argv[++i]; }
        else if (arg == "-tdrz" || arg == "--tinydiarize")   { params.tinydiarize   = true; }
        else if (arg == "-sa"   || arg == "--save-audio")    { params.save_audio    = true; }
        else if (arg == "-ng"   || arg == "--no-gpu")        { params.use_gpu       = false; }
        else if (arg == "-fa"   || arg == "--flash-attn")    { params.flash_attn    = true; }
        // CoreML specific options
        else if (arg == "-coreml" || arg == "--coreml")      { params.use_coreml    = true; }
        else if (arg == "-ncoreml" || arg == "--no-coreml")  { params.use_coreml    = false; }
        else if (arg == "-cm"   || arg == "--coreml-model")  { params.coreml_model  = argv[++i]; }
        // Model management options
        else if (arg == "--list-models")                     { params.list_models   = true; }
        else if (arg == "--list-downloaded")                 { params.list_downloaded = true; }
        else if (arg == "--show-storage")                    { params.show_storage  = true; }
        else if (arg == "--delete-model")                    { params.delete_model_flag = true; params.model_to_delete = argv[++i]; }
        else if (arg == "--delete-all-models")               { params.delete_all_models_flag = true; }
        else if (arg == "--cleanup")                         { params.cleanup_models = true; }
        // Auto-copy options
        else if (arg == "--auto-copy")                       { params.auto_copy_enabled = true; }
        else if (arg == "--no-auto-copy")                    { params.auto_copy_enabled = false; }
        else if (arg == "--auto-copy-max-duration")          { params.auto_copy_max_duration_hours = std::stoi(argv[++i]); }
        else if (arg == "--auto-copy-max-size")              { params.auto_copy_max_size_bytes = std::stoi(argv[++i]); }
        // Export options
        else if (arg == "--export")                          { params.export_enabled = true; }
        else if (arg == "--no-export")                       { params.export_enabled = false; }
        else if (arg == "--export-format")                   { params.export_format = argv[++i]; }
        else if (arg == "--export-file")                     { params.export_file = argv[++i]; params.export_auto_filename = false; }
        else if (arg == "--export-auto-filename")            { params.export_auto_filename = true; }
        else if (arg == "--export-no-metadata")              { params.export_include_metadata = false; }
        else if (arg == "--export-no-timestamps")            { params.export_include_timestamps = false; }
        else if (arg == "--export-include-confidence")       { params.export_include_confidence = true; }
        // Meeting options
        else if (arg == "--meeting")                         { params.meeting_mode = true; }
        else if (arg == "--prompt")                          { params.meeting_prompt = argv[++i]; }
        else if (arg == "--name")                            { params.meeting_name = argv[++i]; }
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
    fprintf(stderr, "            --prompt PROMPT  [%-7s] custom prompt for meeting organization\n", params.meeting_prompt.empty() ? "default" : "custom");
    fprintf(stderr, "            --name NAME      [%-7s] output filename or path for meeting summary\n", params.meeting_name.empty() ? "auto-generated" : params.meeting_name.c_str());
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
    fprintf(stderr, "  %s --meeting                         # organize meeting transcription\n", argv[0]);
    fprintf(stderr, "  %s --meeting --name project-review    # custom output filename\n", argv[0]);
    fprintf(stderr, "  %s --meeting --name ~/docs/meeting.md # custom output path\n", argv[0]);
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
        std::cout << "\n❌ No model available. Exiting.\n";
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
                    std::cout << "✅ Auto-detected CoreML model: " << coreml_path << "\n";
                } else {
                    std::cout << "⚠️  CoreML enabled but model not available: " << coreml_path << "\n";
                    params.use_coreml = false;  // Disable CoreML to prevent crashes
                }
                break;
            }
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
    params.no_context    |= use_vad;
    params.max_tokens     = 0;

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
    
    printf("[Start speaking]\n");
    fflush(stdout);

    // Initialize auto-copy session
    AutoCopySession auto_copy_session;
    if (params.auto_copy_enabled) {
        printf("Auto-copy enabled (Session ID: %s, Max Duration: %d hours, Max Size: %d bytes)\n", 
               auto_copy_session.session_id.c_str(), 
               params.auto_copy_max_duration_hours, 
               params.auto_copy_max_size_bytes);
    }
    
    // Initialize export session
    ExportSession export_session;
    if (params.export_enabled) {
        printf("Export enabled (Session ID: %s, Format: %s, File: %s)\n",
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

    // Initialize meeting session
    MeetingSession meeting_session;
    if (params.meeting_mode) {
        std::string output_filename = generate_meeting_filename(params.meeting_name);
        printf("Meeting mode enabled (Session ID: %s, Output: %s)\n",
               meeting_session.session_id.c_str(), output_filename.c_str());
        printf("Note: Transcription will be processed by Claude CLI when recording ends.\n");
    }

    auto t_last  = std::chrono::high_resolution_clock::now();
    const auto t_start = t_last;

    // Main audio processing loop
    while (is_running && !g_interrupt_received.load()) {
        if (params.save_audio) {
            wavWriter.write(pcmf32_new.data(), pcmf32_new.size());
        }
        
        is_running = sdl_poll_events();
        if (!is_running || g_interrupt_received.load()) {
            break;
        }

        // Process new audio
        if (!use_vad) {
            while (true) {
                is_running = sdl_poll_events();
                if (!is_running || g_interrupt_received.load()) {
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
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            t_last = t_now;
        }

        // Run inference with optimized parameters for CoreML
        {
            whisper_full_params wparams = whisper_full_default_params(params.beam_size > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);

            wparams.print_progress   = false;
            wparams.print_special    = params.print_special;
            wparams.print_realtime   = false;
            wparams.print_timestamps = !params.no_timestamps;
            wparams.translate        = params.translate;
            wparams.single_segment   = !use_vad;
            wparams.max_tokens       = params.max_tokens;
            wparams.language         = params.language.c_str();
            wparams.n_threads        = params.n_threads;
            wparams.beam_search.beam_size = params.beam_size;
            wparams.audio_ctx        = params.audio_ctx;
            wparams.tdrz_enable      = params.tinydiarize;
            wparams.temperature_inc  = params.no_fallback ? 0.0f : wparams.temperature_inc;
            wparams.prompt_tokens    = params.no_context ? nullptr : prompt_tokens.data();
            wparams.prompt_n_tokens  = params.no_context ? 0       : prompt_tokens.size();

            // Use new bilingual processing
            std::vector<BilingualSegment> bilingual_results;
            if (process_audio_segment(ctx, ctx_translate, params, pcmf32, bilingual_results) != 0) {
                fprintf(stderr, "%s: failed to process audio\n", argv[0]);
                return 6;
            }

            // Print results
            {
                if (!use_vad) {
                    printf("\33[2K\r");
                    printf("%s", std::string(100, ' ').c_str());
                    printf("\33[2K\r");
                } else {
                    const int64_t t1 = (t_last - t_start).count()/1000000;
                    const int64_t t0 = std::max(0.0, t1 - pcmf32.size()*1000.0/WHISPER_SAMPLE_RATE);
                    printf("\n### Transcription %d START | t0 = %d ms | t1 = %d ms\n", n_iter, (int) t0, (int) t1);
                    printf("\n");
                }
                
                // Use colored token output if enabled, otherwise use segment-based output
                if (params.print_colors) {
                    // Print tokens directly from whisper context with colors
                    const int n_segments = whisper_full_n_segments(ctx);
                    for (int i = 0; i < n_segments; ++i) {
                        if (!params.no_timestamps) {
                            const int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                            const int64_t t1 = whisper_full_get_segment_t1(ctx, i);
                            printf("[%s --> %s]  ", to_timestamp(t0).c_str(), to_timestamp(t1).c_str());
                        }
                        
                        print_colored_tokens(ctx, i, params);
                        printf("\n");
                    }
                } else {
                    // Use segment-based bilingual output
                    print_bilingual_results(bilingual_results, params, auto_copy_session, export_session, &meeting_session);
                }

                if (params.fname_out.length() > 0) {
                    fout << std::endl;
                }

                if (use_vad) {
                    printf("\n### Transcription %d END\n", n_iter);
                }
            }

            ++n_iter;

            if (!use_vad && (n_iter % n_new_line) == 0) {
                printf("\n");
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
    
    // Perform auto-copy when session ends
    if (params.auto_copy_enabled) {
        perform_auto_copy(auto_copy_session, params);
    }
    
    // Perform export when session ends
    if (params.export_enabled) {
        perform_export(export_session, params);
    }

    // Perform meeting processing when session ends
    if (params.meeting_mode) {
        std::string meeting_output_file = generate_meeting_filename(params.meeting_name);
        std::cout << "\n🚀 Processing meeting transcription with Claude CLI..." << std::endl;

        bool success = process_meeting_transcription(
            meeting_session.get_transcription(),
            params.meeting_prompt,
            meeting_output_file
        );

        if (!success) {
            // Generate fallback filename with date-based naming
            std::string fallback_file = generate_fallback_filename();
            std::ofstream raw_file(fallback_file);
            if (raw_file.is_open()) {
                // Save raw transcription as markdown
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm* tm_ptr = std::localtime(&time_t);

                raw_file << "# Meeting Transcription\n\n";
                raw_file << "**Date**: " << std::put_time(tm_ptr, "%Y-%m-%d %H:%M") << "\n\n";
                raw_file << "**Session ID**: " << meeting_session.session_id << "\n\n";
                raw_file << "---\n\n";
                raw_file << "## Raw Transcription\n\n";
                raw_file << meeting_session.get_transcription();
                raw_file.close();
                std::cout << "✅ Transcription saved to: " << fallback_file << std::endl;
            } else {
                std::cout << "❌ Failed to save transcription to file" << std::endl;
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