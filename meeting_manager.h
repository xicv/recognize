#pragma once

#include <chrono>
#include <string>
#include <sstream>

// Default meeting prompt template
extern const std::string DEFAULT_MEETING_PROMPT;

// Meeting session state
struct MeetingSession {
    std::string session_id;
    std::chrono::high_resolution_clock::time_point start_time;
    std::ostringstream transcription_buffer;
    int current_speaker_id = 1;
    int total_speakers = 1;
    bool first_text_added = false;

    MeetingSession();

    void add_transcription(const std::string& text, bool speaker_turn = false);
    std::string get_transcription() const;
    double get_duration_minutes() const;
};

// File generation
std::string generate_meeting_filename(const std::string& meeting_name);
std::string generate_fallback_filename();

// Meeting transcription processing via Claude CLI
bool process_meeting_transcription(const std::string& transcription, const std::string& prompt,
                                    const std::string& output_file, int timeout_seconds = 120,
                                    double duration_minutes = 0.0, int max_single_pass = 20000);
