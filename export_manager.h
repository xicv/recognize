#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <fstream>

enum class ExportFormat {
    TXT,        // Plain text
    MARKDOWN,   // Markdown with formatting
    JSON,       // Structured JSON
    CSV,        // Comma-separated values
    SRT,        // SubRip subtitle format
    VTT,        // WebVTT subtitle format
    XML         // XML format
};

struct TranscriptionSegment {
    int64_t start_time_ms;
    int64_t end_time_ms;
    std::string text;
    float confidence;
    bool speaker_turn;
    
    TranscriptionSegment(int64_t start, int64_t end, const std::string& content, float conf = 1.0f, bool turn = false)
        : start_time_ms(start), end_time_ms(end), text(content), confidence(conf), speaker_turn(turn) {}
};

struct SessionMetadata {
    std::string session_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::string model_name;
    std::string language;
    std::string device_name;
    bool coreml_enabled;
    int thread_count;
    float vad_threshold;
    int step_ms;
    int length_ms;
    size_t total_segments;
    double total_duration_seconds;
    std::string version;
};

class ExportManager {
public:
    ExportManager();
    
    // Set export parameters
    void set_format(ExportFormat format);
    void set_output_file(const std::string& filename);
    void set_auto_filename(bool auto_name);
    void set_include_metadata(bool include);
    void set_include_timestamps(bool include);
    void set_include_confidence(bool include);
    
    // Add transcription data
    void add_segment(const TranscriptionSegment& segment);
    void set_metadata(const SessionMetadata& metadata);
    
    // Export functions
    bool export_transcription();
    bool export_to_file(const std::string& filename, ExportFormat format);
    std::string get_export_string(ExportFormat format);
    
    // Utility functions
    static std::string format_to_extension(ExportFormat format);
    static ExportFormat extension_to_format(const std::string& extension);
    static std::vector<std::string> get_supported_formats();
    
    // Generate automatic filename
    std::string generate_filename(ExportFormat format);
    
    // Clear data
    void clear();
    
private:
    ExportFormat format_;
    std::string output_file_;
    bool auto_filename_;
    bool include_metadata_;
    bool include_timestamps_;
    bool include_confidence_;
    
    std::vector<TranscriptionSegment> segments_;
    SessionMetadata metadata_;
    
    // Format-specific export functions
    std::string export_txt();
    std::string export_markdown();
    std::string export_json();
    std::string export_csv();
    std::string export_srt();
    std::string export_vtt();
    std::string export_xml();
    
    // Utility functions
    std::string format_timestamp(int64_t ms, bool srt_format = false);
    std::string escape_json_string(const std::string& str);
    std::string escape_csv_field(const std::string& str);
    std::string escape_xml_string(const std::string& str);
    std::string get_current_timestamp();
};