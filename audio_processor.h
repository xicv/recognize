#pragma once

#include "whisper_params.h"
#include "session_types.h"
#include "meeting_manager.h"
#include "whisper.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Bilingual transcription result
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

// RMS audio normalization
void normalize_audio(std::vector<float>& pcmf32, float target_dbfs = -20.0f);

// Whisper inference pipeline — processes audio into bilingual segments
int process_audio_segment(struct whisper_context* ctx, struct whisper_context* ctx_translate,
                          const whisper_params& params, const std::vector<float>& pcmf32,
                          std::vector<BilingualSegment>& bilingual_results,
                          const std::vector<whisper_token>& prompt_tokens = {});

// Print tokens with confidence-based colors
void print_colored_tokens(whisper_context* ctx, int i_segment, const whisper_params& params);

// Shared speaker tracking
struct SpeakerTracker {
    int current_id = 0;
    int total_speakers = 0;

    int on_turn();
    int get_current() const;
};

// Check if auto-copy should be performed
bool should_auto_copy(const AutoCopySession& session, const whisper_params& params);

// Print bilingual results with formatting and session accumulation
void print_bilingual_results(const std::vector<BilingualSegment>& segments, const whisper_params& params,
                             AutoCopySession& auto_copy_session, ExportSession& export_session,
                             SpeakerTracker& speaker_tracker, MeetingSession* meeting_session = nullptr,
                             bool tty_output = true, std::ostringstream* pipe_buffer = nullptr);
