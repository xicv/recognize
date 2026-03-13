#include "audio_processor.h"
#include "export_manager.h"
#include "text_processing.h"

#include "common.h"
#include "common-whisper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unistd.h>

// RMS audio normalization to target dBFS level
void normalize_audio(std::vector<float>& pcmf32, float target_dbfs) {
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
                         const std::vector<whisper_token>& prompt_tokens) {

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

// SpeakerTracker methods
int SpeakerTracker::on_turn() {
    ++current_id;
    total_speakers = std::max(total_speakers, current_id);
    return current_id;
}

int SpeakerTracker::get_current() const { return current_id == 0 ? 1 : current_id; }

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

// Print bilingual results with proper formatting
void print_bilingual_results(const std::vector<BilingualSegment>& segments, const whisper_params& params,
                             AutoCopySession& auto_copy_session, ExportSession& export_session,
                             SpeakerTracker& speaker_tracker, MeetingSession* meeting_session,
                             bool tty_output, std::ostringstream* pipe_buffer,
                             bool accumulate) {

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

                if (accumulate) {
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
            }
            else if (params.output_mode == "english") {
                out(seg.english_text);

                if (accumulate) {
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
            }
            else if (params.output_mode == "bilingual") {
                // Detect language for prefixes
                std::string lang_code = params.language;
                if (lang_code == "auto") lang_code = "orig";

                out(lang_code + ": " + seg.original_text + "\n");
                out("en: " + seg.english_text + "\n");

                if (accumulate) {
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
        }
        else {
            // Timestamped mode
            std::string timestamp_prefix = "[" + to_timestamp(seg.t0, false) + " --> " + to_timestamp(seg.t1, false) + "]  ";

            if (params.output_mode == "original") {
                out(timestamp_prefix + seg.original_text);
                if (seg.speaker_turn) out(" [SPEAKER_TURN]");
                out("\n");

                if (accumulate) {
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
            }
            else if (params.output_mode == "english") {
                out(timestamp_prefix + seg.english_text);
                if (seg.speaker_turn) out(" [SPEAKER_TURN]");
                out("\n");

                if (accumulate) {
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
            }
            else if (params.output_mode == "bilingual") {
                // Detect language for prefixes
                std::string lang_code = params.language;
                if (lang_code == "auto") lang_code = "orig";

                out(timestamp_prefix + lang_code + ": " + seg.original_text + "\n");
                out(timestamp_prefix + "en: " + seg.english_text);
                if (seg.speaker_turn) out(" [SPEAKER_TURN]");
                out("\n");

                if (accumulate) {
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
        }

        fflush(stdout);
    }
}
