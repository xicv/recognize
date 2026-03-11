#pragma once

#include "export_manager.h"

#include <chrono>
#include <random>
#include <sstream>
#include <string>

// Auto-copy session state
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

// Export session state
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
