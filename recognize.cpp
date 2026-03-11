// Real-time speech recognition with CoreML support for macOS
// Based on whisper.cpp/examples/stream/stream.cpp with CoreML optimizations

#include "common-sdl.h"
#include "common.h"
#include "common-whisper.h"
#include "whisper.h"
#include "model_manager.h"
#include "config_manager.h"
#include "export_manager.h"
#include "ptt_manager.h"
#include "history_manager.h"
#include "text_processing.h"
#include "meeting_manager.h"
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
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <regex>
#ifdef __APPLE__
#include <pthread.h>
#endif

// Global state for signal handling
std::atomic<bool> g_interrupt_received(false);
std::atomic<bool> g_is_recording(false);

// Signal handler for graceful shutdown — only sets atomic flag (async-signal-safe)
void signal_handler(int signal) {
    if (signal == SIGINT) {
        g_interrupt_received.store(true);
    }
}

// Suppress stderr output during initialization (SDL device listing, whisper model info, etc.)
// Returns saved fd for later restoration, or -1 on failure
static int suppress_stderr() {
    fflush(stderr);
    int saved_fd = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
    return saved_fd;
}

static void restore_stderr(int saved_fd) {
    if (saved_fd >= 0) {
        fflush(stderr);
        dup2(saved_fd, STDERR_FILENO);
        close(saved_fd);
    }
}

// Silent log callback for whisper/ggml during initialization
static void silent_log_callback(enum ggml_log_level /*level*/, const char * /*text*/, void * /*user_data*/) {
    // Intentionally empty — suppress all init chatter
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

// RMS audio normalization to target dBFS level
static void normalize_audio(std::vector<float>& pcmf32, float target_dbfs = -20.0f) {
    if (pcmf32.empty()) return;
    float sum_sq = 0.0f;
    for (const float s : pcmf32) sum_sq += s * s;
    float rms = std::sqrt(sum_sq / pcmf32.size());
    if (rms < 1e-8f) return;  // silence
    float target_rms = std::pow(10.0f, target_dbfs / 20.0f);
    float gain = std::min(target_rms / rms, 10.0f);  // cap at +20dB
    for (float& s : pcmf32) s = std::max(-1.0f, std::min(1.0f, s * gain));
}

// Process audio with bilingual output support
int process_audio_segment(struct whisper_context* ctx, struct whisper_context* ctx_translate,
                         const whisper_params& params, const std::vector<float>& pcmf32,
                         std::vector<BilingualSegment>& bilingual_results,
                         const std::vector<whisper_token>& prompt_tokens = {}) {
    
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

    // Pass context tokens from previous iteration
    wparams.prompt_tokens   = params.no_context ? nullptr : prompt_tokens.data();
    wparams.prompt_n_tokens = params.no_context ? 0       : prompt_tokens.size();

    // Apply accuracy settings
    wparams.entropy_thold   = params.entropy_thold;
    wparams.logprob_thold   = params.logprob_thold;
    wparams.no_speech_thold = params.no_speech_thold;
    wparams.length_penalty  = params.length_penalty;
    if (params.best_of > 0) wparams.greedy.best_of = params.best_of;
    wparams.suppress_nst    = params.suppress_nst;

    if (!params.initial_prompt.empty()) {
        wparams.initial_prompt = params.initial_prompt.c_str();
        wparams.carry_initial_prompt = params.carry_initial_prompt;
    }
    if (!params.suppress_regex.empty()) {
        wparams.suppress_regex = params.suppress_regex.c_str();
    }

    // Meeting mode overrides (only when user hasn't explicitly changed defaults)
    if (params.meeting_mode) {
        wparams.suppress_nst = true;
        if (params.no_speech_thold == 0.6f) wparams.no_speech_thold = 0.4f;
        if (params.entropy_thold == 2.4f) wparams.entropy_thold = 2.2f;
        wparams.carry_initial_prompt = true;
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
                if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
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
                if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
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
                if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
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
                if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
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
                if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
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
                if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
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
        fprintf(stderr, "Export failed: unsupported format '%s'. Supported formats: ", params.export_format.c_str());
        for (size_t i = 0; i < supported_formats.size(); ++i) {
            fprintf(stderr, "%s", supported_formats[i].c_str());
            if (i < supported_formats.size() - 1) fprintf(stderr, ", ");
        }
        fprintf(stderr, "\n");
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
        fprintf(stderr, "Export completed successfully.\n");
    } else {
        fprintf(stderr, "Export failed.\n");
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
        else if (arg == "--entropy-thold")                    { if (!require_arg(i, arg)) return false; params.entropy_thold = std::stof(argv[++i]); }
        else if (arg == "--logprob-thold")                    { if (!require_arg(i, arg)) return false; params.logprob_thold = std::stof(argv[++i]); }
        else if (arg == "--no-speech-thold")                  { if (!require_arg(i, arg)) return false; params.no_speech_thold = std::stof(argv[++i]); }
        else if (arg == "--length-penalty")                   { if (!require_arg(i, arg)) return false; params.length_penalty = std::stof(argv[++i]); }
        else if (arg == "--best-of")                          { if (!require_arg(i, arg)) return false; params.best_of = std::stoi(argv[++i]); }
        else if (arg == "--suppress-nst")                     { params.suppress_nst = true; }
        else if (arg == "--carry-prompt")                     { params.carry_initial_prompt = true; }
        else if (arg == "--no-normalize")                     { params.normalize_audio = false; }
        // VAD model
        else if (arg == "--vad-model")                        { if (!require_arg(i, arg)) return false; params.vad_model_path = argv[++i]; }
        // Silence timeout
        else if (arg == "--silence-timeout")                  { if (!require_arg(i, arg)) return false; params.silence_timeout = std::stof(argv[++i]); }
        // Push-to-talk options
        else if (arg == "--ptt")                              { params.ptt_mode = true; }
        else if (arg == "--ptt-key")                          { if (!require_arg(i, arg)) return false; params.ptt_key = argv[++i]; }
        // Refinement
        else if (arg == "-r"    || arg == "--refine")         { params.refine = true; }
        // History
        else if (arg == "--no-history")                       { params.history_enabled = false; }
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
                std::cerr << "Available commands: list, set <key> <value>, get <key>, unset <key>, reset" << std::endl;
                exit(1);
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
    fprintf(stderr, "            --entropy-thold N     [%-7.1f] entropy threshold for decoder fallback\n", params.entropy_thold);
    fprintf(stderr, "            --logprob-thold N     [%-7.1f] avg log probability threshold for fallback (-1.0 = disabled)\n", params.logprob_thold);
    fprintf(stderr, "            --no-speech-thold N   [%-7.1f] no-speech probability threshold\n", params.no_speech_thold);
    fprintf(stderr, "            --length-penalty N    [%-7.1f] length penalty for beam search scoring (-1.0 = disabled)\n", params.length_penalty);
    fprintf(stderr, "            --best-of N           [%-7d] number of best candidates for greedy decoding (-1 = default)\n", params.best_of);
    fprintf(stderr, "            --suppress-nst        [%-7s] suppress non-speech tokens\n", params.suppress_nst ? "true" : "false");
    fprintf(stderr, "            --carry-prompt        [%-7s] keep initial prompt across all decode windows\n", params.carry_initial_prompt ? "true" : "false");
    fprintf(stderr, "            --no-normalize        [%-7s] disable audio normalization\n", params.normalize_audio ? "false" : "true");
    fprintf(stderr, "            --vad-model PATH      [%-7s] Silero VAD model path for speech detection\n", params.vad_model_path.empty() ? "none" : "set");
    fprintf(stderr, "            --silence-timeout N   [%-7.1f] auto-stop after N seconds of silence (0 = disabled)\n", params.silence_timeout);
    fprintf(stderr, "\n");
    fprintf(stderr, "push-to-talk:\n");
    fprintf(stderr, "            --ptt              [%-7s] enable push-to-talk mode (hold key to record)\n", params.ptt_mode ? "true" : "false");
    fprintf(stderr, "            --ptt-key KEY      [%-7s] PTT key: space, right_option, right_ctrl, fn, f13\n", params.ptt_key.c_str());
    fprintf(stderr, "\n");
    fprintf(stderr, "refinement:\n");
    fprintf(stderr, "  -r,       --refine           [%-7s] refine transcript via Claude CLI (ASR error correction)\n", params.refine ? "true" : "false");
    fprintf(stderr, "  --no-history                  [%-7s] do not save transcript to history\n", "false");
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
    fprintf(stderr, "subcommands:\n");
    fprintf(stderr, "  config [list|set|get|unset|reset]        manage configuration\n");
    fprintf(stderr, "  history [list|search|show|clear|count]   transcription history\n");
    fprintf(stderr, "\n");
}

// Handle model management commands that exit early (list, delete, cleanup, etc.)
// Returns: -1 if no command matched (continue to main flow), 0+ for exit code
static int handle_model_commands(const whisper_params& params, ModelManager& model_manager) {
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
        return model_manager.delete_model(params.model_to_delete, true) ? 0 : 1;
    }
    if (params.delete_all_models_flag) {
        return model_manager.delete_all_models(true) ? 0 : 1;
    }
    if (params.cleanup_models) {
        model_manager.cleanup_orphaned_files();
        return 0;
    }
    return -1; // No command matched
}

// Finalize session: auto-copy, export, and meeting processing
static void finalize_session(const whisper_params& params,
                             AutoCopySession& auto_copy_session,
                             ExportSession& export_session,
                             SpeakerTracker& speaker_tracker,
                             MeetingSession& meeting_session,
                             const std::string& meeting_output_file,
                             const std::string& transcript_text,
                             std::chrono::high_resolution_clock::time_point session_start) {
    if (params.auto_copy_enabled) {
        perform_auto_copy(auto_copy_session, params);
    }

    if (params.export_enabled) {
        export_session.metadata.total_speakers = speaker_tracker.total_speakers;
        perform_export(export_session, params);
    }

    if (params.meeting_mode) {
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
            std::ofstream raw_file(meeting_output_file);
            if (raw_file.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm tm_buf;
                localtime_r(&time_t, &tm_buf);

                raw_file << "# Meeting Transcription\n\n";
                raw_file << "**Date**: " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M") << "\n";
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

    // Save to history
    if (params.history_enabled && !transcript_text.empty()) {
        HistoryManager history;
        if (history.open()) {
            auto now = std::chrono::high_resolution_clock::now();
            double duration_s = std::chrono::duration<double>(now - session_start).count();
            std::string mode = params.ptt_mode ? "ptt" :
                               params.meeting_mode ? "meeting" :
                               (params.silence_timeout > 0) ? "auto-stop" : "continuous";
            history.save(transcript_text, duration_s, params.model, mode);
        }
    }
}

static int handle_history_command(int argc, char** argv) {
    std::string subcmd = (argc >= 1) ? argv[0] : "list";
    bool json_output = false;
    int limit = 20;
    int offset = 0;

    // Safe integer parsing — returns default_val on invalid input
    auto safe_stoi = [](const char* s, int default_val = 0) -> int {
        try { return std::stoi(s); } catch (const std::exception&) { return default_val; }
    };
    auto safe_stoll = [](const char* s, int64_t default_val = 0) -> int64_t {
        try { return std::stoll(s); } catch (const std::exception&) { return default_val; }
    };

    // Auto-detect: JSON output when stdout is not a TTY
    if (!isatty(STDOUT_FILENO)) json_output = true;

    // Parse common flags
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--json") json_output = true;
    }

    HistoryManager history;
    if (!history.open()) {
        std::cerr << "error: failed to open history database\n";
        return 1;
    }

    if (subcmd == "list" || subcmd == "--json" || subcmd == "--limit" || subcmd == "--offset") {
        // "recognize history" with just flags defaults to list
        for (int i = 0; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--limit" && i + 1 < argc) limit = safe_stoi(argv[++i], 20);
            else if (arg == "--offset" && i + 1 < argc) offset = safe_stoi(argv[++i], 0);
        }
        auto entries = history.list(limit, offset);
        std::cout << (json_output ? HistoryManager::format_json(entries) : HistoryManager::format_table(entries));
        return 0;
    }

    if (subcmd == "search") {
        std::string query;
        std::string since;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--json") { json_output = true; continue; }
            if (arg == "--limit" && i + 1 < argc) { limit = safe_stoi(argv[++i], 20); continue; }
            if (arg == "--since" && i + 1 < argc) {
                std::string val = argv[++i];
                if (!val.empty() && val.back() == 'd') {
                    int days = safe_stoi(val.substr(0, val.size() - 1).c_str(), 0);
                    time_t now = time(nullptr);
                    now -= days * 86400;
                    std::tm tm_buf;
                    localtime_r(&now, &tm_buf);
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
                    since = buf;
                } else {
                    since = val;
                }
                continue;
            }
            if (!query.empty()) query += " ";
            query += arg;
        }
        if (query.empty()) {
            std::cerr << "error: search requires a query\n"
                      << "usage: recognize history search <query> [--limit N] [--since Nd] [--json]\n";
            return 1;
        }
        auto entries = history.search(query, limit, since);
        std::cout << (json_output ? HistoryManager::format_json(entries) : HistoryManager::format_table(entries));
        return 0;
    }

    if (subcmd == "show") {
        if (argc < 2) {
            std::cerr << "error: show requires an ID\n"
                      << "usage: recognize history show <id> [--json]\n";
            return 1;
        }
        int64_t id = safe_stoll(argv[1], -1);
        if (id < 0) {
            std::cerr << "error: invalid ID '" << argv[1] << "'\n";
            return 1;
        }
        auto entry = history.get(id);
        if (!entry) {
            std::cerr << "error: transcript #" << id << " not found\n";
            return 1;
        }
        if (json_output) {
            std::cout << HistoryManager::format_entry_json(*entry) << "\n";
        } else {
            std::cout << "Transcript #" << entry->id << "\n"
                      << "  Time:     " << entry->timestamp << "\n"
                      << "  Duration: " << std::fixed << std::setprecision(1) << entry->duration_s << "s\n"
                      << "  Model:    " << entry->model << "\n"
                      << "  Mode:     " << entry->mode << "\n"
                      << "  Words:    " << entry->word_count << "\n"
                      << "  ---\n"
                      << entry->text << "\n";
        }
        return 0;
    }

    if (subcmd == "clear") {
        bool do_clear_all = false;
        int older_than = 0;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--all") do_clear_all = true;
            else if (arg == "--older-than" && i + 1 < argc) {
                std::string val = argv[++i];
                if (!val.empty() && val.back() == 'd') {
                    older_than = safe_stoi(val.substr(0, val.size() - 1).c_str(), 0);
                } else {
                    older_than = safe_stoi(val.c_str(), 0);
                }
            }
        }
        if (!do_clear_all && older_than <= 0) {
            std::cerr << "error: specify --all or --older-than Nd\n"
                      << "usage: recognize history clear [--older-than Nd] [--all]\n";
            return 1;
        }
        int deleted;
        if (do_clear_all) {
            if (isatty(STDIN_FILENO)) {
                std::cerr << "Delete ALL transcription history? (y/N): ";
                char c = 'n';
                std::cin >> c;
                if (c != 'y' && c != 'Y') {
                    std::cerr << "Cancelled.\n";
                    return 0;
                }
            }
            deleted = history.clear_all();
        } else {
            deleted = history.clear_older_than(older_than);
        }
        std::cerr << "Deleted " << deleted << " entries.\n";
        return 0;
    }

    if (subcmd == "count") {
        int total = history.count();
        if (json_output) {
            std::cout << "{\"count\":" << total << "}\n";
        } else {
            std::cout << total << " transcriptions in history\n";
        }
        return 0;
    }

    std::cerr << "error: unknown history command: " << subcmd << "\n"
              << "usage: recognize history [list|search|show|clear|count]\n";
    return 1;
}

int main(int argc, char ** argv) {
    // Handle "history" subcommand before any heavy initialization
    if (argc >= 2 && std::string(argv[1]) == "history") {
        return handle_history_command(argc - 2, argv + 2);
    }

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

    // Validate PTT settings
    if (params.ptt_mode) {
        int ptt_key_code = PushToTalkManager::key_name_to_code(params.ptt_key);
        if (ptt_key_code < 0) {
            fprintf(stderr, "error: unknown PTT key '%s'. Valid keys: space, right_option, right_ctrl, fn, f13\n", params.ptt_key.c_str());
            return 1;
        }
        if (params.meeting_mode) {
            fprintf(stderr, "error: --ptt is incompatible with --meeting (meetings require continuous recording)\n");
            return 1;
        }
        if (params.silence_timeout > 0.0f) {
            fprintf(stderr, "note: --silence-timeout ignored in PTT mode (key release stops recording)\n");
            params.silence_timeout = 0.0f;
        }
    }

    // Validate refine: check claude CLI is available early
    if (params.refine && !is_claude_cli_available()) {
        fprintf(stderr, "error: --refine requires Claude CLI. Install from: https://claude.ai/code\n");
        return 1;
    }

    // Initialize model manager
    ModelManager model_manager;
    
    // Apply configured models directory if set
    ConfigData effective_config = config_manager.get_effective_config();
    if (effective_config.models_directory) {
        model_manager.set_models_directory(*effective_config.models_directory);
    }

    // Handle model management commands (exit early if matched)
    int cmd_result = handle_model_commands(params, model_manager);
    if (cmd_result >= 0) return cmd_result;

    // Show clean loading state
    const bool stderr_is_tty = isatty(STDERR_FILENO);

    // Resolve model (with auto-download if needed)
    std::string resolved_model = model_manager.resolve_model(params.model, params.use_coreml);
    if (resolved_model.empty()) {
        std::cerr << "\n❌ No model available. Exiting.\n";
        return 1;
    }
    
    // Update params with resolved model path
    params.model = resolved_model;

    // Extract short model name for display (e.g. "ggml-large-v3-turbo.bin" → "large-v3-turbo")
    std::string display_model = std::filesystem::path(resolved_model).stem().string();
    if (display_model.rfind("ggml-", 0) == 0) {
        display_model = display_model.substr(5);
    }
    if (stderr_is_tty) {
        fprintf(stderr, "[Loading %s...]\n", display_model.c_str());
    }
    
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
                } else {
                    params.use_coreml = false;  // Disable CoreML to prevent crashes
                }
                break;
            }
        }
    }

    // Adjust thread count based on hardware acceleration
    // When CoreML + Metal are active, encoder runs on ANE and decoder on GPU,
    // so very few CPU threads are needed (just orchestration overhead).
    {
        int default_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
        if (params.use_coreml && params.use_gpu) {
            // CoreML encoder on ANE + Metal decoder: minimal CPU threads
            if (params.n_threads == default_threads) {
                params.n_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
            }
        } else if (!params.use_coreml && params.use_gpu) {
            // Metal only: GPU handles most compute
            if (params.n_threads == default_threads) {
                params.n_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
            }
        } else if (params.n_threads <= 4) {
            // CPU only: use more threads
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

    // Init audio — suppress SDL device listing during init
    audio_async audio(params.length_ms);
    int saved_stderr = suppress_stderr();
    bool audio_ok = audio.init(params.capture_id, WHISPER_SAMPLE_RATE);
    restore_stderr(saved_stderr);
    if (!audio_ok) {
        fprintf(stderr, "%s: audio.init() failed!\n", __func__);
        return 1;
    }

    audio.resume();

    // Request P-core scheduling for the inference thread
    #ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    #endif

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
        cparams.use_gpu = params.use_gpu;  // Metal for decoder, CoreML for encoder
    } else {
        cparams.use_gpu = params.use_gpu;
    }
    #else
    cparams.use_gpu = params.use_gpu;
    if (params.use_coreml) {
        fprintf(stderr, "warning: CoreML requested but not compiled with CoreML support\n");
    }
    #endif

    cparams.flash_attn = params.flash_attn;

    // Suppress whisper/ggml verbose logging during model load
    whisper_log_set(silent_log_callback, nullptr);
    ggml_log_set(silent_log_callback, nullptr);

    struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);
    if (ctx == nullptr) {
        // Restore logging before printing error
        whisper_log_set(nullptr, nullptr);
        ggml_log_set(nullptr, nullptr);
        fprintf(stderr, "error: failed to initialize whisper context\n");
        return 2;
    }

    // CoreML warm-up: first inference triggers ANE compilation (2-5s delay)
    #ifdef WHISPER_COREML
    if (params.use_coreml) {
        std::vector<float> warmup(WHISPER_SAMPLE_RATE * 1, 0.0f); // 1 second of silence
        whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wp.print_realtime   = false;
        wp.print_progress   = false;
        wp.print_timestamps = false;
        wp.print_special    = false;
        wp.n_threads        = params.n_threads;
        whisper_full(ctx, wp, warmup.data(), warmup.size());
    }
    #endif

    // Keep whisper/ggml logging suppressed for clean UX
    // (Metal kernel JIT compilation can log during first real inference)

    // Validate output mode
    if (params.output_mode != "original" && params.output_mode != "english" && params.output_mode != "bilingual") {
        fprintf(stderr, "error: invalid output mode '%s'. Valid modes: original, english, bilingual\n", params.output_mode.c_str());
        whisper_free(ctx);
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
        whisper_free(ctx);
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

    // Auto-correct language settings for non-multilingual models
    if (!whisper_is_multilingual(ctx)) {
        if (params.language != "en" || params.translate) {
            params.language = "en";
            params.translate = false;
        }
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
            whisper_free(ctx);
            if (ctx_translate) whisper_free(ctx_translate);
            return 1;
        }
    }

    wav_writer wavWriter;
    if (params.save_audio) {
        time_t now = time(0);
        char buffer[80];
        std::tm tm_buf;
        localtime_r(&now, &tm_buf);
        strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &tm_buf);
        std::string filename = std::string(buffer) + ".wav";
        wavWriter.open(filename, WHISPER_SAMPLE_RATE, 16, 1);
    }
    
    // Initialize auto-copy session
    AutoCopySession auto_copy_session;

    // Initialize export session
    ExportSession export_session;
    if (params.export_enabled) {
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
#ifdef RECOGNIZE_VERSION
        export_session.metadata.version = "recognize-" RECOGNIZE_VERSION;
#else
        export_session.metadata.version = "recognize-dev";
#endif
    }

    // Initialize speaker tracker and meeting session
    SpeakerTracker speaker_tracker;
    MeetingSession meeting_session;
    std::string meeting_output_file;
    if (params.meeting_mode) {
        meeting_output_file = generate_meeting_filename(params.meeting_name);
    }

    // ─── Push-to-Talk mode ───────────────────────────────────────────────
    if (params.ptt_mode) {
        PushToTalkManager ptt;
        int ptt_key_code = PushToTalkManager::key_name_to_code(params.ptt_key);
        if (!ptt.start(ptt_key_code)) {
            fprintf(stderr, "Failed to start PTT. Check Input Monitoring permissions.\n");
            whisper_free(ctx);
            if (ctx_translate) whisper_free(ctx_translate);
            return 7;
        }

        // Clean ready state for PTT
        if (stderr_is_tty) {
            fprintf(stderr, "[Ready — hold %s to record, release to transcribe]\n",
                    params.ptt_key.c_str());
        }
        fflush(stderr);

        bool is_running_ptt = true;
        std::string ptt_pipe_text;
        const bool stderr_tty = isatty(STDERR_FILENO);

        // PTT: single-shot — record once, transcribe, exit (no quit confirmation)
        while (is_running_ptt && !g_interrupt_received.load()) {
            // Wait for key press
            while (!ptt.is_key_held() && is_running_ptt && !g_interrupt_received.load()) {
                is_running_ptt = sdl_poll_events();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!is_running_ptt || g_interrupt_received.load()) break;

            // Key pressed — start capture
            audio.clear();
            auto t_press = std::chrono::high_resolution_clock::now();
            if (stderr_tty) fprintf(stderr, "\r[Recording...] ");

            // Capture while key is held
            while (ptt.is_key_held() && is_running_ptt && !g_interrupt_received.load()) {
                is_running_ptt = sdl_poll_events();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!is_running_ptt || g_interrupt_received.load()) break;

            // Key released — get audio and transcribe
            auto t_release = std::chrono::high_resolution_clock::now();
            int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                t_release - t_press).count());

            if (duration_ms < 200) {
                // Too short, likely accidental tap — wait for another press
                if (stderr_tty) fprintf(stderr, "\r[Too short, skipped]          \n");
                continue;
            }

            // Cap at 30s (circular buffer limit)
            duration_ms = std::min(duration_ms, 30000);

            std::vector<float> pcmf32_ptt;
            audio.get(duration_ms, pcmf32_ptt);

            if (params.normalize_audio) {
                normalize_audio(pcmf32_ptt);
            }

            if (stderr_tty) fprintf(stderr, "\r[Transcribing %.1fs...]        ", duration_ms / 1000.0f);

            // Single-pass whisper inference
            std::vector<BilingualSegment> bilingual_results;
            if (process_audio_segment(ctx, ctx_translate, params, pcmf32_ptt,
                                      bilingual_results, prompt_tokens) != 0) {
                fprintf(stderr, "\nfailed to process audio\n");
                break;  // Exit on error
            }

            // Apply hallucination filter
            for (auto& seg : bilingual_results) {
                if (!seg.original_text.empty())
                    seg.original_text = filter_hallucinations(seg.original_text);
                if (!seg.english_text.empty())
                    seg.english_text = filter_hallucinations(seg.english_text);
            }
            bilingual_results.erase(
                std::remove_if(bilingual_results.begin(), bilingual_results.end(),
                    [](const BilingualSegment& s) {
                        return s.original_text.empty() && s.english_text.empty();
                    }),
                bilingual_results.end());

            // Refine via Claude if enabled
            if (params.refine && !bilingual_results.empty()) {
                // Concatenate raw text for refinement
                std::string raw_text;
                for (const auto& seg : bilingual_results) {
                    if (!seg.original_text.empty()) raw_text += seg.original_text;
                    else if (!seg.english_text.empty()) raw_text += seg.english_text;
                }
                if (!raw_text.empty()) {
                    std::string refined = refine_transcription(raw_text);
                    // Replace all segments with a single refined segment
                    BilingualSegment refined_seg = bilingual_results[0];
                    if (!refined_seg.original_text.empty()) {
                        refined_seg.original_text = " " + refined;
                    } else {
                        refined_seg.english_text = " " + refined;
                    }
                    refined_seg.speaker_turn = false;
                    bilingual_results.clear();
                    bilingual_results.push_back(refined_seg);
                }
            }

            // Display results
            if (stderr_tty) fprintf(stderr, "\r                              \r");

            if (stdout_is_tty) {
                printf("\n");
            }

            std::ostringstream ptt_pipe_buf;
            std::ostringstream* pbuf = stdout_is_tty ? nullptr : &ptt_pipe_buf;
            print_bilingual_results(bilingual_results, params, auto_copy_session, export_session,
                                    speaker_tracker, nullptr, stdout_is_tty, pbuf);

            if (!stdout_is_tty) {
                ptt_pipe_text += ptt_pipe_buf.str();
            }

            if (stdout_is_tty) {
                printf("\n");
                fflush(stdout);
            }

            // Single-shot: exit after first successful transcription
            break;
        }

        ptt.stop();

        // Dump accumulated text in pipe mode
        if (!stdout_is_tty && !ptt_pipe_text.empty()) {
            printf("%s\n", ptt_pipe_text.c_str());
            fflush(stdout);
        }

        audio.pause();

        // Gather final transcript text for history
        std::string history_text;
        if (params.meeting_mode) {
            history_text = meeting_session.get_transcription();
        } else if (!stdout_is_tty) {
            history_text = ptt_pipe_text;
        } else {
            history_text = auto_copy_session.transcription_buffer.str();
        }

        // Finalize session
        finalize_session(params, auto_copy_session, export_session, speaker_tracker,
                         meeting_session, meeting_output_file, history_text, auto_copy_session.start_time);

        g_is_recording.store(false);
        whisper_free(ctx);
        if (ctx_translate) whisper_free(ctx_translate);
        return 0;
    }

    // ─── Standard (non-PTT) mode ────────────────────────────────────────

    // Clean ready state for standard mode
    if (stderr_is_tty) {
        fprintf(stderr, "[Start speaking]\n");
        fflush(stderr);
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

            // Silence timeout: check new audio for speech before inference
            // Use a copy because vad_simple() applies high_pass_filter() in-place
            if (silence_timeout_enabled) {
                std::vector<float> pcmf32_vad(pcmf32_new);
                bool new_audio_has_speech = ::vad_simple(pcmf32_vad, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, false);
                if (new_audio_has_speech) {
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

        // Apply audio normalization before inference
        if (params.normalize_audio) {
            normalize_audio(pcmf32);
        }

        // Run inference via bilingual processing pipeline
        {
            // Note: whisper_full_params are configured inside process_audio_segment()
            std::vector<BilingualSegment> bilingual_results;
            if (process_audio_segment(ctx, ctx_translate, params, pcmf32, bilingual_results, prompt_tokens) != 0) {
                fprintf(stderr, "%s: failed to process audio\n", argv[0]);
                whisper_free(ctx);
                if (ctx_translate) whisper_free(ctx_translate);
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
                        if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
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

    // Refine accumulated text via Claude if enabled (standard mode)
    if (params.refine) {
        // Refine pipe output
        std::string raw_pipe = pipe_finalized_text + pipe_current_text.str();
        if (!raw_pipe.empty()) {
            std::string refined = refine_transcription(raw_pipe);
            pipe_finalized_text = refined;
            pipe_current_text.str("");
            pipe_current_text.clear();
        }
        // Refine auto-copy buffer
        if (params.auto_copy_enabled) {
            std::string raw_copy = auto_copy_session.transcription_buffer.str();
            if (!raw_copy.empty()) {
                std::string refined_copy = refine_transcription(raw_copy);
                auto_copy_session.transcription_buffer.str("");
                auto_copy_session.transcription_buffer.clear();
                auto_copy_session.transcription_buffer << refined_copy;
            }
        }
    }

    // Dump accumulated text to stdout when not a TTY (pipe/redirect mode)
    if (!stdout_is_tty) {
        std::string final_text = pipe_finalized_text + pipe_current_text.str();
        if (!final_text.empty()) {
            printf("%s\n", final_text.c_str());
            fflush(stdout);
        }
    }

    // Gather final transcript text for history
    std::string history_text;
    if (params.meeting_mode) {
        history_text = meeting_session.get_transcription();
    } else if (!stdout_is_tty) {
        history_text = pipe_finalized_text + pipe_current_text.str();
    } else {
        history_text = auto_copy_session.transcription_buffer.str();
    }

    // Finalize session: auto-copy, export, meeting processing
    finalize_session(params, auto_copy_session, export_session, speaker_tracker,
                     meeting_session, meeting_output_file, history_text, auto_copy_session.start_time);

    // Clear recording state
    g_is_recording.store(false);
    
    whisper_free(ctx);

    // Clean up translation context if it was created
    if (ctx_translate) {
        whisper_free(ctx_translate);
    }

    return 0;
}