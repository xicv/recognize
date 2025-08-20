#include "export_manager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <ctime>
#include <random>

ExportManager::ExportManager() 
    : format_(ExportFormat::TXT)
    , auto_filename_(true)
    , include_metadata_(true)
    , include_timestamps_(true)
    , include_confidence_(false) {
    
    // Initialize metadata with defaults
    metadata_.session_id = "";
    metadata_.start_time = std::chrono::system_clock::now();
    metadata_.model_name = "unknown";
    metadata_.language = "en";
    metadata_.device_name = "default";
    metadata_.coreml_enabled = false;
    metadata_.thread_count = 4;
    metadata_.vad_threshold = 0.6f;
    metadata_.step_ms = 3000;
    metadata_.length_ms = 10000;
    metadata_.version = "recognize-1.0.0";
}

void ExportManager::set_format(ExportFormat format) {
    format_ = format;
}

void ExportManager::set_output_file(const std::string& filename) {
    output_file_ = filename;
    auto_filename_ = false;
}

void ExportManager::set_auto_filename(bool auto_name) {
    auto_filename_ = auto_name;
}

void ExportManager::set_include_metadata(bool include) {
    include_metadata_ = include;
}

void ExportManager::set_include_timestamps(bool include) {
    include_timestamps_ = include;
}

void ExportManager::set_include_confidence(bool include) {
    include_confidence_ = include;
}

void ExportManager::add_segment(const TranscriptionSegment& segment) {
    segments_.push_back(segment);
}

void ExportManager::set_metadata(const SessionMetadata& metadata) {
    metadata_ = metadata;
    
    // Calculate derived metadata
    if (!segments_.empty()) {
        metadata_.total_segments = segments_.size();
        
        int64_t total_duration = 0;
        if (!segments_.empty()) {
            total_duration = segments_.back().end_time_ms - segments_.front().start_time_ms;
        }
        metadata_.total_duration_seconds = total_duration / 1000.0;
    }
}

bool ExportManager::export_transcription() {
    std::string filename = auto_filename_ ? generate_filename(format_) : output_file_;
    return export_to_file(filename, format_);
}

bool ExportManager::export_to_file(const std::string& filename, ExportFormat format) {
    try {
        std::string content = get_export_string(format);
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open file for writing: " << filename << std::endl;
            return false;
        }
        
        file << content;
        file.close();
        
        std::cout << "✅ Transcription exported to: " << filename << std::endl;
        std::cout << "📊 " << segments_.size() << " segments, " 
                  << std::fixed << std::setprecision(1) 
                  << metadata_.total_duration_seconds << " seconds" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "❌ Export failed: " << e.what() << std::endl;
        return false;
    }
}

std::string ExportManager::get_export_string(ExportFormat format) {
    switch (format) {
        case ExportFormat::TXT:      return export_txt();
        case ExportFormat::MARKDOWN: return export_markdown();
        case ExportFormat::JSON:     return export_json();
        case ExportFormat::CSV:      return export_csv();
        case ExportFormat::SRT:      return export_srt();
        case ExportFormat::VTT:      return export_vtt();
        case ExportFormat::XML:      return export_xml();
        default:                     return export_txt();
    }
}

std::string ExportManager::export_txt() {
    std::ostringstream output;
    
    if (include_metadata_) {
        output << "# Transcription Export\n";
        output << "Session ID: " << metadata_.session_id << "\n";
        output << "Date: " << get_current_timestamp() << "\n";
        output << "Model: " << metadata_.model_name << "\n";
        output << "Language: " << metadata_.language << "\n";
        output << "Duration: " << std::fixed << std::setprecision(1) 
               << metadata_.total_duration_seconds << " seconds\n";
        output << "Segments: " << metadata_.total_segments << "\n";
        output << "\n" << std::string(50, '-') << "\n\n";
    }
    
    for (const auto& segment : segments_) {
        if (include_timestamps_) {
            output << "[" << format_timestamp(segment.start_time_ms) 
                   << " --> " << format_timestamp(segment.end_time_ms) << "] ";
        }
        
        output << segment.text;
        
        if (include_confidence_) {
            output << " (confidence: " << std::fixed << std::setprecision(2) 
                   << segment.confidence << ")";
        }
        
        if (segment.speaker_turn) {
            output << " [SPEAKER_TURN]";
        }
        
        output << "\n";
    }
    
    return output.str();
}

std::string ExportManager::export_markdown() {
    std::ostringstream output;
    
    output << "# Transcription Export\n\n";
    
    if (include_metadata_) {
        output << "## Session Information\n\n";
        output << "| Field | Value |\n";
        output << "|-------|-------|\n";
        output << "| Session ID | `" << metadata_.session_id << "` |\n";
        output << "| Date | " << get_current_timestamp() << " |\n";
        output << "| Model | " << metadata_.model_name << " |\n";
        output << "| Language | " << metadata_.language << " |\n";
        output << "| Duration | " << std::fixed << std::setprecision(1) 
               << metadata_.total_duration_seconds << " seconds |\n";
        output << "| Segments | " << metadata_.total_segments << " |\n";
        output << "| CoreML | " << (metadata_.coreml_enabled ? "Enabled" : "Disabled") << " |\n";
        output << "| VAD Threshold | " << metadata_.vad_threshold << " |\n";
        output << "\n## Transcription\n\n";
    }
    
    for (size_t i = 0; i < segments_.size(); ++i) {
        const auto& segment = segments_[i];
        
        if (include_timestamps_) {
            output << "**[" << format_timestamp(segment.start_time_ms) 
                   << " → " << format_timestamp(segment.end_time_ms) << "]** ";
        }
        
        output << segment.text;
        
        if (include_confidence_) {
            output << " *(confidence: " << std::fixed << std::setprecision(2) 
                   << segment.confidence << ")*";
        }
        
        if (segment.speaker_turn) {
            output << " `[SPEAKER_TURN]`";
        }
        
        output << "\n\n";
    }
    
    output << "---\n";
    output << "*Generated by recognize v" << metadata_.version << "*\n";
    
    return output.str();
}

std::string ExportManager::export_json() {
    std::ostringstream output;
    
    output << "{\n";
    
    if (include_metadata_) {
        output << "  \"metadata\": {\n";
        output << "    \"session_id\": \"" << escape_json_string(metadata_.session_id) << "\",\n";
        output << "    \"export_timestamp\": \"" << get_current_timestamp() << "\",\n";
        output << "    \"model\": \"" << escape_json_string(metadata_.model_name) << "\",\n";
        output << "    \"language\": \"" << escape_json_string(metadata_.language) << "\",\n";
        output << "    \"duration_seconds\": " << metadata_.total_duration_seconds << ",\n";
        output << "    \"total_segments\": " << metadata_.total_segments << ",\n";
        output << "    \"coreml_enabled\": " << (metadata_.coreml_enabled ? "true" : "false") << ",\n";
        output << "    \"thread_count\": " << metadata_.thread_count << ",\n";
        output << "    \"vad_threshold\": " << metadata_.vad_threshold << ",\n";
        output << "    \"step_ms\": " << metadata_.step_ms << ",\n";
        output << "    \"length_ms\": " << metadata_.length_ms << ",\n";
        output << "    \"version\": \"" << escape_json_string(metadata_.version) << "\"\n";
        output << "  },\n";
    }
    
    output << "  \"segments\": [\n";
    
    for (size_t i = 0; i < segments_.size(); ++i) {
        const auto& segment = segments_[i];
        
        output << "    {\n";
        output << "      \"id\": " << i << ",\n";
        output << "      \"start_time_ms\": " << segment.start_time_ms << ",\n";
        output << "      \"end_time_ms\": " << segment.end_time_ms << ",\n";
        output << "      \"text\": \"" << escape_json_string(segment.text) << "\"";
        
        if (include_confidence_) {
            output << ",\n      \"confidence\": " << segment.confidence;
        }
        
        if (segment.speaker_turn) {
            output << ",\n      \"speaker_turn\": true";
        }
        
        output << "\n    }";
        if (i < segments_.size() - 1) output << ",";
        output << "\n";
    }
    
    output << "  ]\n";
    output << "}\n";
    
    return output.str();
}

std::string ExportManager::export_csv() {
    std::ostringstream output;
    
    // CSV header
    output << "id,start_time_ms,end_time_ms,start_time,end_time,text";
    if (include_confidence_) output << ",confidence";
    output << ",speaker_turn\n";
    
    for (size_t i = 0; i < segments_.size(); ++i) {
        const auto& segment = segments_[i];
        
        output << i << ","
               << segment.start_time_ms << ","
               << segment.end_time_ms << ","
               << "\"" << format_timestamp(segment.start_time_ms) << "\","
               << "\"" << format_timestamp(segment.end_time_ms) << "\","
               << "\"" << escape_csv_field(segment.text) << "\"";
        
        if (include_confidence_) {
            output << "," << segment.confidence;
        }
        
        output << "," << (segment.speaker_turn ? "true" : "false") << "\n";
    }
    
    return output.str();
}

std::string ExportManager::export_srt() {
    std::ostringstream output;
    
    for (size_t i = 0; i < segments_.size(); ++i) {
        const auto& segment = segments_[i];
        
        output << (i + 1) << "\n";
        output << format_timestamp(segment.start_time_ms, true) 
               << " --> " 
               << format_timestamp(segment.end_time_ms, true) << "\n";
        output << segment.text;
        if (segment.speaker_turn) {
            output << " [SPEAKER_TURN]";
        }
        output << "\n\n";
    }
    
    return output.str();
}

std::string ExportManager::export_vtt() {
    std::ostringstream output;
    
    output << "WEBVTT\n\n";
    
    if (include_metadata_) {
        output << "NOTE\n";
        output << "Generated by recognize v" << metadata_.version << "\n";
        output << "Session: " << metadata_.session_id << "\n";
        output << "Model: " << metadata_.model_name << "\n\n";
    }
    
    for (const auto& segment : segments_) {
        output << format_timestamp(segment.start_time_ms) 
               << " --> " 
               << format_timestamp(segment.end_time_ms) << "\n";
        output << segment.text;
        if (segment.speaker_turn) {
            output << " [SPEAKER_TURN]";
        }
        output << "\n\n";
    }
    
    return output.str();
}

std::string ExportManager::export_xml() {
    std::ostringstream output;
    
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    output << "<transcription>\n";
    
    if (include_metadata_) {
        output << "  <metadata>\n";
        output << "    <session_id>" << escape_xml_string(metadata_.session_id) << "</session_id>\n";
        output << "    <export_timestamp>" << get_current_timestamp() << "</export_timestamp>\n";
        output << "    <model>" << escape_xml_string(metadata_.model_name) << "</model>\n";
        output << "    <language>" << escape_xml_string(metadata_.language) << "</language>\n";
        output << "    <duration_seconds>" << metadata_.total_duration_seconds << "</duration_seconds>\n";
        output << "    <total_segments>" << metadata_.total_segments << "</total_segments>\n";
        output << "    <coreml_enabled>" << (metadata_.coreml_enabled ? "true" : "false") << "</coreml_enabled>\n";
        output << "    <version>" << escape_xml_string(metadata_.version) << "</version>\n";
        output << "  </metadata>\n";
    }
    
    output << "  <segments>\n";
    
    for (size_t i = 0; i < segments_.size(); ++i) {
        const auto& segment = segments_[i];
        
        output << "    <segment id=\"" << i << "\"";
        output << " start_time_ms=\"" << segment.start_time_ms << "\"";
        output << " end_time_ms=\"" << segment.end_time_ms << "\"";
        if (include_confidence_) {
            output << " confidence=\"" << segment.confidence << "\"";
        }
        if (segment.speaker_turn) {
            output << " speaker_turn=\"true\"";
        }
        output << ">";
        output << escape_xml_string(segment.text);
        output << "</segment>\n";
    }
    
    output << "  </segments>\n";
    output << "</transcription>\n";
    
    return output.str();
}

std::string ExportManager::format_timestamp(int64_t ms, bool srt_format) {
    int64_t total_seconds = ms / 1000;
    int64_t milliseconds = ms % 1000;
    int64_t hours = total_seconds / 3600;
    int64_t minutes = (total_seconds % 3600) / 60;
    int64_t seconds = total_seconds % 60;
    
    std::ostringstream timestamp;
    timestamp << std::setfill('0');
    
    if (srt_format) {
        timestamp << std::setw(2) << hours << ":"
                  << std::setw(2) << minutes << ":"
                  << std::setw(2) << seconds << ","
                  << std::setw(3) << milliseconds;
    } else {
        timestamp << std::setw(2) << hours << ":"
                  << std::setw(2) << minutes << ":"
                  << std::setw(2) << seconds << "."
                  << std::setw(3) << milliseconds;
    }
    
    return timestamp.str();
}

std::string ExportManager::escape_json_string(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

std::string ExportManager::escape_csv_field(const std::string& str) {
    std::string escaped = str;
    // Replace quotes with double quotes
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\"\"");
        pos += 2;
    }
    return escaped;
}

std::string ExportManager::escape_xml_string(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '&': escaped += "&amp;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

std::string ExportManager::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return timestamp.str();
}

std::string ExportManager::format_to_extension(ExportFormat format) {
    switch (format) {
        case ExportFormat::TXT:      return ".txt";
        case ExportFormat::MARKDOWN: return ".md";
        case ExportFormat::JSON:     return ".json";
        case ExportFormat::CSV:      return ".csv";
        case ExportFormat::SRT:      return ".srt";
        case ExportFormat::VTT:      return ".vtt";
        case ExportFormat::XML:      return ".xml";
        default:                     return ".txt";
    }
}

ExportFormat ExportManager::extension_to_format(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".txt") return ExportFormat::TXT;
    if (ext == ".md" || ext == ".markdown") return ExportFormat::MARKDOWN;
    if (ext == ".json") return ExportFormat::JSON;
    if (ext == ".csv") return ExportFormat::CSV;
    if (ext == ".srt") return ExportFormat::SRT;
    if (ext == ".vtt") return ExportFormat::VTT;
    if (ext == ".xml") return ExportFormat::XML;
    
    return ExportFormat::TXT; // Default
}

std::vector<std::string> ExportManager::get_supported_formats() {
    return {"txt", "md", "json", "csv", "srt", "vtt", "xml"};
}

std::string ExportManager::generate_filename(ExportFormat format) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream filename;
    filename << "transcript_" 
             << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    if (!metadata_.session_id.empty()) {
        filename << "_" << metadata_.session_id;
    }
    
    filename << format_to_extension(format);
    
    return filename.str();
}

void ExportManager::clear() {
    segments_.clear();
    metadata_ = SessionMetadata{};
    metadata_.start_time = std::chrono::system_clock::now();
}