#include "config_manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Simple JSON handling without external dependencies
namespace {
    // Basic JSON escape function
    std::string escape_json_string(const std::string& str) {
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
    
    // Basic JSON unescape function
    std::string unescape_json_string(const std::string& str) {
        std::string unescaped;
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '\\' && i + 1 < str.length()) {
                switch (str[i + 1]) {
                    case '"': unescaped += '"'; ++i; break;
                    case '\\': unescaped += '\\'; ++i; break;
                    case 'b': unescaped += '\b'; ++i; break;
                    case 'f': unescaped += '\f'; ++i; break;
                    case 'n': unescaped += '\n'; ++i; break;
                    case 'r': unescaped += '\r'; ++i; break;
                    case 't': unescaped += '\t'; ++i; break;
                    default: unescaped += str[i]; break;
                }
            } else {
                unescaped += str[i];
            }
        }
        return unescaped;
    }
    
    // Extract JSON value (simple parser for our use case)
    std::string extract_json_value(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        
        // Skip whitespace
        ++pos;
        while (pos < json.length() && std::isspace(json[pos])) ++pos;
        
        if (pos >= json.length()) return "";
        
        if (json[pos] == '"') {
            // String value
            ++pos;
            size_t end = json.find('"', pos);
            if (end == std::string::npos) return "";
            return unescape_json_string(json.substr(pos, end - pos));
        } else {
            // Non-string value (number, boolean, null)
            size_t end = pos;
            while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
                ++end;
            }
            std::string value = json.substr(pos, end - pos);
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t\n\r"));
            value.erase(value.find_last_not_of(" \t\n\r") + 1);
            return value;
        }
    }
}

ConfigManager::ConfigManager() {
    init_config_paths();
}

void ConfigManager::init_config_paths() {
    // User config: ~/.recognize/config.json
    const char* home = getenv("HOME");
    if (home) {
        std::string config_dir = std::string(home) + "/.recognize";
        std::filesystem::create_directories(config_dir);
        user_config_path_ = config_dir + "/config.json";
    }
    
    // Project config: ./config.json or ./.whisper-config.json
    if (std::filesystem::exists(".whisper-config.json")) {
        project_config_path_ = ".whisper-config.json";
    } else if (std::filesystem::exists("config.json")) {
        project_config_path_ = "config.json";
    } else {
        project_config_path_ = ".whisper-config.json"; // Default for new files
    }
}

void ConfigManager::load_config() {
    // Load in priority order: user config, project config, environment
    if (!user_config_path_.empty() && std::filesystem::exists(user_config_path_)) {
        user_config_ = load_config_file(user_config_path_);
    }
    
    if (std::filesystem::exists(project_config_path_)) {
        project_config_ = load_config_file(project_config_path_);
    }
    
    load_env_vars();
}

void ConfigManager::apply_to_params(whisper_params& params) const {
    ConfigData effective = get_effective_config();
    
    if (effective.default_model) params.model = *effective.default_model;
    if (effective.use_coreml) params.use_coreml = *effective.use_coreml;
    if (effective.coreml_no_ane) params.coreml_no_ane = *effective.coreml_no_ane;
    if (effective.coreml_model) params.coreml_model = *effective.coreml_model;
    if (effective.capture_device) params.capture_id = *effective.capture_device;
    if (effective.step_ms) params.step_ms = *effective.step_ms;
    if (effective.length_ms) params.length_ms = *effective.length_ms;
    if (effective.keep_ms) params.keep_ms = *effective.keep_ms;
    if (effective.vad_threshold) params.vad_thold = *effective.vad_threshold;
    if (effective.freq_threshold) params.freq_thold = *effective.freq_threshold;
    if (effective.threads) params.n_threads = *effective.threads;
    if (effective.max_tokens) params.max_tokens = *effective.max_tokens;
    if (effective.beam_size) params.beam_size = *effective.beam_size;
    if (effective.language) params.language = *effective.language;
    if (effective.translate) params.translate = *effective.translate;
    if (effective.no_timestamps) params.no_timestamps = *effective.no_timestamps;
    if (effective.print_special) params.print_special = *effective.print_special;
    if (effective.print_colors) params.print_colors = *effective.print_colors;
    if (effective.save_audio) params.save_audio = *effective.save_audio;
    if (effective.tinydiarize) params.tinydiarize = *effective.tinydiarize;
    if (effective.output_file) params.fname_out = *effective.output_file;
    if (effective.output_mode) params.output_mode = *effective.output_mode;
    
    // Auto-copy settings
    if (effective.auto_copy_enabled) params.auto_copy_enabled = *effective.auto_copy_enabled;
    if (effective.auto_copy_max_duration_hours) params.auto_copy_max_duration_hours = *effective.auto_copy_max_duration_hours;
    if (effective.auto_copy_max_size_bytes) params.auto_copy_max_size_bytes = *effective.auto_copy_max_size_bytes;

    // Meeting settings
    if (effective.meeting_mode) params.meeting_mode = *effective.meeting_mode;
    if (effective.meeting_prompt) params.meeting_prompt = *effective.meeting_prompt;
    if (effective.meeting_name) params.meeting_name = *effective.meeting_name;
    if (effective.meeting_initial_prompt) params.initial_prompt = *effective.meeting_initial_prompt;
    if (effective.meeting_timeout) params.meeting_timeout = *effective.meeting_timeout;
    if (effective.meeting_max_single_pass) params.meeting_max_single_pass = *effective.meeting_max_single_pass;
    // Push-to-talk settings
    if (effective.ptt_mode) params.ptt_mode = *effective.ptt_mode;
    if (effective.ptt_key) params.ptt_key = *effective.ptt_key;

    // Refinement settings
    if (effective.refine) params.refine = *effective.refine;
    if (effective.history_enabled) params.history_enabled = *effective.history_enabled;

    if (effective.silence_timeout) params.silence_timeout = *effective.silence_timeout;

    // Accuracy settings
    if (effective.entropy_thold) params.entropy_thold = *effective.entropy_thold;
    if (effective.logprob_thold) params.logprob_thold = *effective.logprob_thold;
    if (effective.no_speech_thold) params.no_speech_thold = *effective.no_speech_thold;
    if (effective.length_penalty) params.length_penalty = *effective.length_penalty;
    if (effective.best_of) params.best_of = *effective.best_of;
    if (effective.suppress_nst) params.suppress_nst = *effective.suppress_nst;
    if (effective.carry_initial_prompt) params.carry_initial_prompt = *effective.carry_initial_prompt;
    if (effective.normalize_audio) params.normalize_audio = *effective.normalize_audio;
}

const std::map<std::string, std::string>& ConfigManager::get_config_key_map() const {
    static const std::map<std::string, std::string> key_map = {
        {"model", "default_model"},
        {"default_model", "default_model"},
        {"models_dir", "models_directory"},
        {"models_directory", "models_directory"},
        {"coreml", "use_coreml"},
        {"use_coreml", "use_coreml"},
        {"coreml_no_ane", "coreml_no_ane"},
        {"coreml_gpu_only", "coreml_no_ane"},
        {"no_ane", "coreml_no_ane"},
        {"coreml_model", "coreml_model"},
        {"capture", "capture_device"},
        {"capture_device", "capture_device"},
        {"step", "step_ms"},
        {"step_ms", "step_ms"},
        {"length", "length_ms"},
        {"length_ms", "length_ms"},
        {"keep", "keep_ms"},
        {"keep_ms", "keep_ms"},
        {"vad", "vad_threshold"},
        {"vad_threshold", "vad_threshold"},
        {"freq", "freq_threshold"},
        {"freq_threshold", "freq_threshold"},
        {"threads", "threads"},
        {"tokens", "max_tokens"},
        {"max_tokens", "max_tokens"},
        {"beam", "beam_size"},
        {"beam_size", "beam_size"},
        {"language", "language"},
        {"lang", "language"},
        {"translate", "translate"},
        {"timestamps", "no_timestamps"},
        {"no_timestamps", "no_timestamps"},
        {"special", "print_special"},
        {"print_special", "print_special"},
        {"colors", "print_colors"},
        {"print_colors", "print_colors"},
        {"save_audio", "save_audio"},
        {"tinydiarize", "tinydiarize"},
        {"speaker_segmentation", "tinydiarize"},
        {"output", "output_file"},
        {"output_file", "output_file"},
        {"format", "output_format"},
        {"output_format", "output_format"},
        {"mode", "output_mode"},
        {"output_mode", "output_mode"},
        
        // Auto-copy configuration keys
        {"auto_copy", "auto_copy_enabled"},
        {"auto_copy_enabled", "auto_copy_enabled"},
        {"auto_copy_max_duration", "auto_copy_max_duration_hours"},
        {"auto_copy_max_duration_hours", "auto_copy_max_duration_hours"},
        {"auto_copy_max_size", "auto_copy_max_size_bytes"},
        {"auto_copy_max_size_bytes", "auto_copy_max_size_bytes"},

        // Meeting configuration keys
        {"meeting", "meeting_mode"},
        {"meeting_mode", "meeting_mode"},
        {"meeting_prompt", "meeting_prompt"},
        {"meeting_name", "meeting_name"},
        {"meeting_initial_prompt", "meeting_initial_prompt"},
        {"meeting_timeout", "meeting_timeout"},
        {"meeting_max_single_pass", "meeting_max_single_pass"},
        {"silence_timeout", "silence_timeout"},

        // Push-to-talk configuration keys
        {"ptt", "ptt_mode"},
        {"ptt_mode", "ptt_mode"},
        {"ptt_key", "ptt_key"},

        // Refinement configuration keys
        {"refine", "refine"},

        // History configuration keys
        {"history", "history_enabled"},
        {"history_enabled", "history_enabled"},

        // Accuracy configuration keys
        {"entropy", "entropy_thold"},
        {"entropy_thold", "entropy_thold"},
        {"logprob", "logprob_thold"},
        {"logprob_thold", "logprob_thold"},
        {"no_speech", "no_speech_thold"},
        {"no_speech_thold", "no_speech_thold"},
        {"length_penalty", "length_penalty"},
        {"best_of", "best_of"},
        {"suppress_nst", "suppress_nst"},
        {"carry_prompt", "carry_initial_prompt"},
        {"carry_initial_prompt", "carry_initial_prompt"},
        {"normalize", "normalize_audio"},
        {"normalize_audio", "normalize_audio"}
    };
    return key_map;
}

bool ConfigManager::set_config(const std::string& key, const std::string& value) {
    auto key_map = get_config_key_map();
    auto it = key_map.find(key);
    if (it == key_map.end()) {
        std::cerr << "Unknown config key: " << key << std::endl;
        return false;
    }
    
    return set_config_value(user_config_, it->second, value);
}

std::optional<std::string> ConfigManager::get_config(const std::string& key) const {
    auto key_map = get_config_key_map();
    auto it = key_map.find(key);
    if (it == key_map.end()) {
        return std::nullopt;
    }
    
    ConfigData effective = get_effective_config();
    return get_config_value(effective, it->second);
}

bool ConfigManager::unset_config(const std::string& key) {
    auto key_map = get_config_key_map();
    auto it = key_map.find(key);
    if (it == key_map.end()) {
        return false;
    }
    
    return set_config_value(user_config_, it->second, ""); // Set to empty to unset
}

void ConfigManager::list_config() const {
    ConfigData effective = get_effective_config();
    
    std::cout << "Current Configuration:\n";
    std::cout << "======================\n\n";
    
    // Show system paths first
    std::cout << "System Paths (configurable):\n";
    std::cout << "  models_location: " << (effective.models_directory ? *effective.models_directory : "models/") << " (set via: config set models_dir <path>)\n";
    std::cout << "  user_config: " << (user_config_path_.empty() ? "not available" : user_config_path_) << "\n";
    std::cout << "  project_config: " << project_config_path_ << "\n";
    std::cout << "\n";
    
    // Show all available configuration options with descriptions
    std::cout << "Available Configuration Options:\n";
    std::cout << "================================\n\n";
    
    // Model Settings
    std::cout << "📦 Model Settings:\n";
    std::cout << "  model (default_model)        : " << (effective.default_model ? *effective.default_model : "(auto-select)") << " - Default model to use\n";
    std::cout << "  models_dir                   : " << (effective.models_directory ? *effective.models_directory : "models/") << " - Directory to store models\n";
    std::cout << "  use_coreml                   : " << (effective.use_coreml ? (*effective.use_coreml ? "true" : "false") : "true") << " - Enable CoreML acceleration\n";
    std::cout << "  coreml_no_ane                : " << (effective.coreml_no_ane ? (*effective.coreml_no_ane ? "true" : "false") : "false") << " - Skip ANE (fast startup, ~3x slower inference)\n";
    std::cout << "  coreml_model                 : " << (effective.coreml_model ? *effective.coreml_model : "(auto)") << " - Specific CoreML model path\n";
    std::cout << "\n";
    
    // Audio Settings  
    std::cout << "🎙️  Audio Settings:\n";
    std::cout << "  capture (capture_device)     : " << (effective.capture_device ? std::to_string(*effective.capture_device) : "-1") << " - Audio capture device ID (-1 = default)\n";
    std::cout << "  step (step_ms)               : " << (effective.step_ms ? std::to_string(*effective.step_ms) : "3000") << " - Audio step size in ms (0 = VAD mode)\n";
    std::cout << "  length (length_ms)           : " << (effective.length_ms ? std::to_string(*effective.length_ms) : "10000") << " - Audio length in ms\n";
    std::cout << "  keep (keep_ms)               : " << (effective.keep_ms ? std::to_string(*effective.keep_ms) : "200") << " - Audio to keep from previous step\n";
    std::cout << "  vad (vad_threshold)          : " << (effective.vad_threshold ? std::to_string(*effective.vad_threshold) : "0.6") << " - Voice activity detection threshold (0.0-1.0)\n";
    std::cout << "  freq (freq_threshold)        : " << (effective.freq_threshold ? std::to_string(*effective.freq_threshold) : "100.0") << " - High-pass frequency cutoff\n";
    std::cout << "\n";
    
    // Processing Settings
    std::cout << "⚡ Processing Settings:\n";
    std::cout << "  threads                      : " << (effective.threads ? std::to_string(*effective.threads) : "4") << " - Number of processing threads\n";
    std::cout << "  tokens (max_tokens)          : " << (effective.max_tokens ? std::to_string(*effective.max_tokens) : "32") << " - Maximum tokens per chunk\n";
    std::cout << "  beam (beam_size)             : " << (effective.beam_size ? std::to_string(*effective.beam_size) : "-1") << " - Beam search size (-1 = auto)\n";
    std::cout << "  language (lang)              : " << (effective.language ? *effective.language : "en") << " - Source language code\n";
    std::cout << "  translate                    : " << (effective.translate ? (*effective.translate ? "true" : "false") : "false") << " - Translate to English\n";
    std::cout << "\n";
    
    // Accuracy Settings
    std::cout << "🎯 Accuracy Settings:\n";
    std::cout << "  entropy_thold                : " << (effective.entropy_thold ? std::to_string(*effective.entropy_thold) : "2.4") << " - Entropy threshold for decoder fallback\n";
    std::cout << "  logprob_thold                : " << (effective.logprob_thold ? std::to_string(*effective.logprob_thold) : "-1.0") << " - Avg log probability threshold (-1.0 = disabled)\n";
    std::cout << "  no_speech_thold              : " << (effective.no_speech_thold ? std::to_string(*effective.no_speech_thold) : "0.6") << " - No-speech probability threshold\n";
    std::cout << "  length_penalty               : " << (effective.length_penalty ? std::to_string(*effective.length_penalty) : "-1.0") << " - Length penalty for beam search (-1.0 = disabled)\n";
    std::cout << "  best_of                      : " << (effective.best_of ? std::to_string(*effective.best_of) : "-1") << " - Best candidates for greedy decoding (-1 = default)\n";
    std::cout << "  suppress_nst                 : " << (effective.suppress_nst ? (*effective.suppress_nst ? "true" : "false") : "false") << " - Suppress non-speech tokens\n";
    std::cout << "  carry_prompt                 : " << (effective.carry_initial_prompt ? (*effective.carry_initial_prompt ? "true" : "false") : "false") << " - Keep initial prompt across decode windows\n";
    std::cout << "  normalize (normalize_audio)  : " << (effective.normalize_audio ? (*effective.normalize_audio ? "true" : "false") : "true") << " - RMS audio normalization\n";
    std::cout << "\n";

    // Output Settings
    std::cout << "📄 Output Settings:\n";
    std::cout << "  timestamps (no_timestamps)   : " << (effective.no_timestamps ? (*effective.no_timestamps ? "disabled" : "enabled") : "enabled") << " - Show timestamps in output\n";
    std::cout << "  special (print_special)      : " << (effective.print_special ? (*effective.print_special ? "true" : "false") : "false") << " - Print special tokens\n";
    std::cout << "  colors (print_colors)        : " << (effective.print_colors ? (*effective.print_colors ? "true" : "false") : "false") << " - Print colors based on confidence\n";
    std::cout << "  save_audio                   : " << (effective.save_audio ? (*effective.save_audio ? "true" : "false") : "false") << " - Save recorded audio to WAV\n";
    std::cout << "  tinydiarize                  : " << (effective.tinydiarize ? (*effective.tinydiarize ? "true" : "false") : "false") << " - Enable speaker segmentation (requires tdrz model)\n";
    std::cout << "  output (output_file)         : " << (effective.output_file ? *effective.output_file : "(none)") << " - Output file path\n";
    std::cout << "  format (output_format)       : " << (effective.output_format ? *effective.output_format : "plain") << " - Output format (json/plain/timestamped)\n";
    std::cout << "  mode (output_mode)           : " << (effective.output_mode ? *effective.output_mode : "original") << " - Output mode (original/english/bilingual)\n";
    std::cout << "\n";
    
    // Auto-copy Settings
    std::cout << "📋 Auto-copy Settings:\n";
    std::cout << "  auto_copy (auto_copy_enabled): " << (effective.auto_copy_enabled ? (*effective.auto_copy_enabled ? "true" : "false") : "false") << " - Auto-copy to clipboard when session ends\n";
    std::cout << "  auto_copy_max_duration       : " << (effective.auto_copy_max_duration_hours ? std::to_string(*effective.auto_copy_max_duration_hours) : "2") << " - Max session duration (hours) before skipping auto-copy\n";
    std::cout << "  auto_copy_max_size           : " << (effective.auto_copy_max_size_bytes ? std::to_string(*effective.auto_copy_max_size_bytes) : "1048576") << " - Max transcription size (bytes) before skipping auto-copy\n";
    std::cout << "\n";
    
    // Push-to-Talk Settings
    std::cout << "🎤 Push-to-Talk Settings:\n";
    std::cout << "  ptt (ptt_mode)               : " << (effective.ptt_mode ? (*effective.ptt_mode ? "true" : "false") : "false") << " - Enable push-to-talk mode\n";
    std::cout << "  ptt_key                      : " << (effective.ptt_key ? *effective.ptt_key : "space") << " - PTT key (space, right_option, right_ctrl, fn, f13)\n";
    std::cout << "  refine                       : " << (effective.refine ? (*effective.refine ? "true" : "false") : "false") << " - Refine transcript via Claude (ASR error correction)\n";
    std::cout << "\n";

    // History Settings
    std::cout << "History Settings:\n";
    std::cout << "  history (history_enabled)    : " << (effective.history_enabled ? (*effective.history_enabled ? "true" : "false") : "true") << " - Save transcripts to history\n";
    std::cout << "\n";

    // Meeting Settings
    std::cout << "🎯 Meeting Settings:\n";
    std::cout << "  meeting (meeting_mode)       : " << (effective.meeting_mode ? (*effective.meeting_mode ? "true" : "false") : "false") << " - Enable meeting mode (AI summarization)\n";
    std::cout << "  meeting_prompt               : " << (effective.meeting_prompt ? "(custom)" : "(default)") << " - Custom meeting prompt (text or file path)\n";
    std::cout << "  meeting_name                 : " << (effective.meeting_name ? *effective.meeting_name : "(none)") << " - Meeting output filename prefix\n";
    std::cout << "  meeting_initial_prompt        : " << (effective.meeting_initial_prompt ? "(custom)" : "(default)") << " - Initial whisper prompt for meeting mode\n";
    std::cout << "  meeting_timeout              : " << (effective.meeting_timeout ? std::to_string(*effective.meeting_timeout) : "120") << " - Claude CLI timeout in seconds\n";
    std::cout << "  meeting_max_single_pass      : " << (effective.meeting_max_single_pass ? std::to_string(*effective.meeting_max_single_pass) : "20000") << " - Max words before multi-pass summarization\n";
    std::cout << "  silence_timeout              : " << (effective.silence_timeout ? std::to_string(*effective.silence_timeout) : "0") << " - Auto-stop after N seconds of silence (0 = disabled)\n";
    std::cout << "\n";

    std::cout << "💡 Configuration Examples:\n";
    std::cout << "   config set model base.en               # Set default model\n";
    std::cout << "   config set threads 8                   # Set thread count\n";
    std::cout << "   config set vad_threshold 0.7           # Adjust voice detection\n";
    std::cout << "   config set step_ms 0                   # Enable VAD mode\n";
    std::cout << "   config set print_colors true           # Enable colored output\n";
    std::cout << "   config set auto_copy true              # Enable clipboard copy\n";
    std::cout << "   config set meeting true                # Enable meeting mode\n";
    std::cout << "   config set output_mode bilingual       # Show original + English\n";
    std::cout << "   config set models_dir /custom/path     # Change models location\n";
    std::cout << "   config get <key>                       # Get any setting\n";
    std::cout << "   config reset                           # Reset all to defaults\n";
    std::cout << "\n";
    std::cout << "🌍 Environment Variables (WHISPER_* prefix):\n";
    std::cout << "   All config keys can also be set via environment variables.\n";
    std::cout << "   Examples: WHISPER_MODEL, WHISPER_THREADS, WHISPER_VAD_THRESHOLD\n";
}

void ConfigManager::reset_config() {
    user_config_ = ConfigData{};
    project_config_ = ConfigData{};
    env_config_ = ConfigData{};
}

bool ConfigManager::save_user_config() const {
    if (user_config_path_.empty()) return false;
    return save_config_file(user_config_path_, user_config_);
}

bool ConfigManager::save_project_config() const {
    return save_config_file(project_config_path_, project_config_);
}

void ConfigManager::load_env_vars() {
    env_config_.default_model = get_env_var("WHISPER_MODEL");
    env_config_.models_directory = get_env_var("WHISPER_MODELS_DIR");
    env_config_.use_coreml = get_env_bool("WHISPER_COREML");
    env_config_.coreml_no_ane = get_env_bool("WHISPER_COREML_NO_ANE");
    env_config_.coreml_model = get_env_var("WHISPER_COREML_MODEL");
    env_config_.capture_device = get_env_int("WHISPER_CAPTURE_DEVICE");
    env_config_.step_ms = get_env_int("WHISPER_STEP_MS");
    env_config_.length_ms = get_env_int("WHISPER_LENGTH_MS");
    env_config_.keep_ms = get_env_int("WHISPER_KEEP_MS");
    env_config_.vad_threshold = get_env_float("WHISPER_VAD_THRESHOLD");
    env_config_.freq_threshold = get_env_float("WHISPER_FREQ_THRESHOLD");
    env_config_.threads = get_env_int("WHISPER_THREADS");
    env_config_.max_tokens = get_env_int("WHISPER_MAX_TOKENS");
    env_config_.beam_size = get_env_int("WHISPER_BEAM_SIZE");
    env_config_.language = get_env_var("WHISPER_LANGUAGE");
    env_config_.translate = get_env_bool("WHISPER_TRANSLATE");
    env_config_.no_timestamps = get_env_bool("WHISPER_NO_TIMESTAMPS");
    env_config_.print_special = get_env_bool("WHISPER_PRINT_SPECIAL");
    env_config_.print_colors = get_env_bool("WHISPER_PRINT_COLORS");
    env_config_.save_audio = get_env_bool("WHISPER_SAVE_AUDIO");
    env_config_.tinydiarize = get_env_bool("WHISPER_TINYDIARIZE");
    env_config_.output_file = get_env_var("WHISPER_OUTPUT_FILE");
    env_config_.output_format = get_env_var("WHISPER_OUTPUT_FORMAT");
    
    // Auto-copy environment variables
    env_config_.auto_copy_enabled = get_env_bool("WHISPER_AUTO_COPY");
    env_config_.auto_copy_max_duration_hours = get_env_int("WHISPER_AUTO_COPY_MAX_DURATION");
    env_config_.auto_copy_max_size_bytes = get_env_int("WHISPER_AUTO_COPY_MAX_SIZE");

    // Meeting environment variables
    env_config_.meeting_mode = get_env_bool("WHISPER_MEETING");
    env_config_.meeting_prompt = get_env_var("WHISPER_MEETING_PROMPT");
    env_config_.meeting_name = get_env_var("WHISPER_MEETING_NAME");
    env_config_.meeting_initial_prompt = get_env_var("WHISPER_MEETING_INITIAL_PROMPT");
    env_config_.meeting_timeout = get_env_int("WHISPER_MEETING_TIMEOUT");
    env_config_.meeting_max_single_pass = get_env_int("WHISPER_MEETING_MAX_SINGLE_PASS");
    env_config_.silence_timeout = get_env_float("WHISPER_SILENCE_TIMEOUT");

    // Push-to-talk environment variables
    env_config_.ptt_mode = get_env_bool("WHISPER_PTT");
    env_config_.ptt_key = get_env_var("WHISPER_PTT_KEY");

    // Refinement environment variables
    env_config_.refine = get_env_bool("WHISPER_REFINE");
    env_config_.history_enabled = get_env_bool("WHISPER_HISTORY");

    // Accuracy environment variables
    env_config_.entropy_thold = get_env_float("WHISPER_ENTROPY_THOLD");
    env_config_.logprob_thold = get_env_float("WHISPER_LOGPROB_THOLD");
    env_config_.no_speech_thold = get_env_float("WHISPER_NO_SPEECH_THOLD");
    env_config_.length_penalty = get_env_float("WHISPER_LENGTH_PENALTY");
    env_config_.best_of = get_env_int("WHISPER_BEST_OF");
    env_config_.suppress_nst = get_env_bool("WHISPER_SUPPRESS_NST");
    env_config_.carry_initial_prompt = get_env_bool("WHISPER_CARRY_PROMPT");
    env_config_.normalize_audio = get_env_bool("WHISPER_NORMALIZE_AUDIO");
}

bool ConfigManager::validate_config() const {
    ConfigData effective = get_effective_config();
    
    // Validate model if specified
    if (effective.default_model) {
        // Basic validation - could be enhanced with ModelManager integration
        const std::string& model = *effective.default_model;
        if (model.empty()) {
            std::cerr << "Model name cannot be empty\n";
            return false;
        }
    }
    
    // Validate numeric ranges
    if (effective.threads && (*effective.threads < 1 || *effective.threads > 64)) {
        std::cerr << "Threads must be between 1 and 64\n";
        return false;
    }
    
    if (effective.vad_threshold && (*effective.vad_threshold < 0.0f || *effective.vad_threshold > 1.0f)) {
        std::cerr << "VAD threshold must be between 0.0 and 1.0\n";
        return false;
    }
    
    return true;
}

ConfigData ConfigManager::get_effective_config() const {
    return merge_configs({user_config_, project_config_, env_config_});
}

ConfigData ConfigManager::load_config_file(const std::string& filepath) const {
    std::ifstream file(filepath);
    if (!file) return ConfigData{};
    
    std::string json_str((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    
    return json_to_config(json_str);
}

bool ConfigManager::save_config_file(const std::string& filepath, const ConfigData& config) const {
    // Ensure directory exists
    std::filesystem::path path(filepath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    
    std::ofstream file(filepath);
    if (!file) return false;
    
    file << config_to_json(config);
    return file.good();
}

ConfigData ConfigManager::merge_configs(const std::vector<ConfigData>& configs) const {
    ConfigData merged;
    
    for (const auto& config : configs) {
        if (config.default_model) merged.default_model = config.default_model;
        if (config.models_directory) merged.models_directory = config.models_directory;
        if (config.use_coreml) merged.use_coreml = config.use_coreml;
        if (config.coreml_no_ane) merged.coreml_no_ane = config.coreml_no_ane;
        if (config.coreml_model) merged.coreml_model = config.coreml_model;
        if (config.capture_device) merged.capture_device = config.capture_device;
        if (config.step_ms) merged.step_ms = config.step_ms;
        if (config.length_ms) merged.length_ms = config.length_ms;
        if (config.keep_ms) merged.keep_ms = config.keep_ms;
        if (config.vad_threshold) merged.vad_threshold = config.vad_threshold;
        if (config.freq_threshold) merged.freq_threshold = config.freq_threshold;
        if (config.threads) merged.threads = config.threads;
        if (config.max_tokens) merged.max_tokens = config.max_tokens;
        if (config.beam_size) merged.beam_size = config.beam_size;
        if (config.language) merged.language = config.language;
        if (config.translate) merged.translate = config.translate;
        if (config.no_timestamps) merged.no_timestamps = config.no_timestamps;
        if (config.print_special) merged.print_special = config.print_special;
        if (config.print_colors) merged.print_colors = config.print_colors;
        if (config.save_audio) merged.save_audio = config.save_audio;
        if (config.tinydiarize) merged.tinydiarize = config.tinydiarize;
        if (config.output_file) merged.output_file = config.output_file;
        if (config.output_format) merged.output_format = config.output_format;
        if (config.output_mode) merged.output_mode = config.output_mode;
        
        // Auto-copy settings
        if (config.auto_copy_enabled) merged.auto_copy_enabled = config.auto_copy_enabled;
        if (config.auto_copy_max_duration_hours) merged.auto_copy_max_duration_hours = config.auto_copy_max_duration_hours;
        if (config.auto_copy_max_size_bytes) merged.auto_copy_max_size_bytes = config.auto_copy_max_size_bytes;

        // Meeting settings
        if (config.meeting_mode) merged.meeting_mode = config.meeting_mode;
        if (config.meeting_prompt) merged.meeting_prompt = config.meeting_prompt;
        if (config.meeting_name) merged.meeting_name = config.meeting_name;
        if (config.meeting_initial_prompt) merged.meeting_initial_prompt = config.meeting_initial_prompt;
        if (config.meeting_timeout) merged.meeting_timeout = config.meeting_timeout;
        if (config.meeting_max_single_pass) merged.meeting_max_single_pass = config.meeting_max_single_pass;
        if (config.silence_timeout) merged.silence_timeout = config.silence_timeout;

        // Push-to-talk settings
        if (config.ptt_mode) merged.ptt_mode = config.ptt_mode;
        if (config.ptt_key) merged.ptt_key = config.ptt_key;

        // Refinement settings
        if (config.refine) merged.refine = config.refine;
        if (config.history_enabled) merged.history_enabled = config.history_enabled;

        // Accuracy settings
        if (config.entropy_thold) merged.entropy_thold = config.entropy_thold;
        if (config.logprob_thold) merged.logprob_thold = config.logprob_thold;
        if (config.no_speech_thold) merged.no_speech_thold = config.no_speech_thold;
        if (config.length_penalty) merged.length_penalty = config.length_penalty;
        if (config.best_of) merged.best_of = config.best_of;
        if (config.suppress_nst) merged.suppress_nst = config.suppress_nst;
        if (config.carry_initial_prompt) merged.carry_initial_prompt = config.carry_initial_prompt;
        if (config.normalize_audio) merged.normalize_audio = config.normalize_audio;
    }

    return merged;
}

std::string ConfigManager::config_to_json(const ConfigData& config) const {
    std::ostringstream json;
    json << "{\n";
    
    bool first = true;
    auto add_field = [&](const std::string& key, const std::string& value) {
        if (!first) json << ",\n";
        json << "  \"" << key << "\": \"" << escape_json_string(value) << "\"";
        first = false;
    };
    
    auto add_bool = [&](const std::string& key, bool value) {
        if (!first) json << ",\n";
        json << "  \"" << key << "\": " << (value ? "true" : "false");
        first = false;
    };
    
    auto add_int = [&](const std::string& key, int value) {
        if (!first) json << ",\n";
        json << "  \"" << key << "\": " << value;
        first = false;
    };
    
    auto add_float = [&](const std::string& key, float value) {
        if (!first) json << ",\n";
        json << "  \"" << key << "\": " << std::fixed << std::setprecision(2) << value;
        first = false;
    };
    
    if (config.default_model) add_field("default_model", *config.default_model);
    if (config.models_directory) add_field("models_directory", *config.models_directory);
    if (config.use_coreml) add_bool("use_coreml", *config.use_coreml);
    if (config.coreml_no_ane) add_bool("coreml_no_ane", *config.coreml_no_ane);
    if (config.coreml_model) add_field("coreml_model", *config.coreml_model);
    if (config.capture_device) add_int("capture_device", *config.capture_device);
    if (config.step_ms) add_int("step_ms", *config.step_ms);
    if (config.length_ms) add_int("length_ms", *config.length_ms);
    if (config.keep_ms) add_int("keep_ms", *config.keep_ms);
    if (config.vad_threshold) add_float("vad_threshold", *config.vad_threshold);
    if (config.freq_threshold) add_float("freq_threshold", *config.freq_threshold);
    if (config.threads) add_int("threads", *config.threads);
    if (config.max_tokens) add_int("max_tokens", *config.max_tokens);
    if (config.beam_size) add_int("beam_size", *config.beam_size);
    if (config.language) add_field("language", *config.language);
    if (config.translate) add_bool("translate", *config.translate);
    if (config.no_timestamps) add_bool("no_timestamps", *config.no_timestamps);
    if (config.print_special) add_bool("print_special", *config.print_special);
    if (config.print_colors) add_bool("print_colors", *config.print_colors);
    if (config.save_audio) add_bool("save_audio", *config.save_audio);
    if (config.tinydiarize) add_bool("tinydiarize", *config.tinydiarize);
    if (config.output_file) add_field("output_file", *config.output_file);
    if (config.output_format) add_field("output_format", *config.output_format);
    if (config.output_mode) add_field("output_mode", *config.output_mode);
    
    // Auto-copy settings
    if (config.auto_copy_enabled) add_bool("auto_copy_enabled", *config.auto_copy_enabled);
    if (config.auto_copy_max_duration_hours) add_int("auto_copy_max_duration_hours", *config.auto_copy_max_duration_hours);
    if (config.auto_copy_max_size_bytes) add_int("auto_copy_max_size_bytes", *config.auto_copy_max_size_bytes);

    // Meeting settings
    if (config.meeting_mode) add_bool("meeting_mode", *config.meeting_mode);
    if (config.meeting_prompt) add_field("meeting_prompt", *config.meeting_prompt);
    if (config.meeting_name) add_field("meeting_name", *config.meeting_name);
    if (config.meeting_initial_prompt) add_field("meeting_initial_prompt", *config.meeting_initial_prompt);
    if (config.meeting_timeout) add_int("meeting_timeout", *config.meeting_timeout);
    if (config.meeting_max_single_pass) add_int("meeting_max_single_pass", *config.meeting_max_single_pass);
    if (config.silence_timeout) add_float("silence_timeout", *config.silence_timeout);

    // Push-to-talk settings
    if (config.ptt_mode) add_bool("ptt_mode", *config.ptt_mode);
    if (config.ptt_key) add_field("ptt_key", *config.ptt_key);

    // Refinement settings
    if (config.refine) add_bool("refine", *config.refine);
    if (config.history_enabled) add_bool("history_enabled", *config.history_enabled);

    // Accuracy settings
    if (config.entropy_thold) add_float("entropy_thold", *config.entropy_thold);
    if (config.logprob_thold) add_float("logprob_thold", *config.logprob_thold);
    if (config.no_speech_thold) add_float("no_speech_thold", *config.no_speech_thold);
    if (config.length_penalty) add_float("length_penalty", *config.length_penalty);
    if (config.best_of) add_int("best_of", *config.best_of);
    if (config.suppress_nst) add_bool("suppress_nst", *config.suppress_nst);
    if (config.carry_initial_prompt) add_bool("carry_initial_prompt", *config.carry_initial_prompt);
    if (config.normalize_audio) add_bool("normalize_audio", *config.normalize_audio);

    json << "\n}\n";
    return json.str();
}

ConfigData ConfigManager::json_to_config(const std::string& json_str) const {
    ConfigData config;
    
    auto get_string = [&](const std::string& key) -> std::optional<std::string> {
        std::string value = extract_json_value(json_str, key);
        return value.empty() ? std::nullopt : std::make_optional(value);
    };
    
    auto get_bool = [&](const std::string& key) -> std::optional<bool> {
        std::string value = extract_json_value(json_str, key);
        if (value == "true") return true;
        if (value == "false") return false;
        return std::nullopt;
    };
    
    auto get_int = [&](const std::string& key) -> std::optional<int> {
        std::string value = extract_json_value(json_str, key);
        if (value.empty()) return std::nullopt;
        try {
            return std::stoi(value);
        } catch (...) {
            return std::nullopt;
        }
    };
    
    auto get_float = [&](const std::string& key) -> std::optional<float> {
        std::string value = extract_json_value(json_str, key);
        if (value.empty()) return std::nullopt;
        try {
            return std::stof(value);
        } catch (...) {
            return std::nullopt;
        }
    };
    
    config.default_model = get_string("default_model");
    config.models_directory = get_string("models_directory");
    config.use_coreml = get_bool("use_coreml");
    config.coreml_no_ane = get_bool("coreml_no_ane");
    config.coreml_model = get_string("coreml_model");
    config.capture_device = get_int("capture_device");
    config.step_ms = get_int("step_ms");
    config.length_ms = get_int("length_ms");
    config.keep_ms = get_int("keep_ms");
    config.vad_threshold = get_float("vad_threshold");
    config.freq_threshold = get_float("freq_threshold");
    config.threads = get_int("threads");
    config.max_tokens = get_int("max_tokens");
    config.beam_size = get_int("beam_size");
    config.language = get_string("language");
    config.translate = get_bool("translate");
    config.no_timestamps = get_bool("no_timestamps");
    config.print_special = get_bool("print_special");
    config.print_colors = get_bool("print_colors");
    config.save_audio = get_bool("save_audio");
    config.tinydiarize = get_bool("tinydiarize");
    config.output_file = get_string("output_file");
    config.output_format = get_string("output_format");
    config.output_mode = get_string("output_mode");
    
    // Auto-copy settings
    config.auto_copy_enabled = get_bool("auto_copy_enabled");
    config.auto_copy_max_duration_hours = get_int("auto_copy_max_duration_hours");
    config.auto_copy_max_size_bytes = get_int("auto_copy_max_size_bytes");

    // Meeting settings
    config.meeting_mode = get_bool("meeting_mode");
    config.meeting_prompt = get_string("meeting_prompt");
    config.meeting_name = get_string("meeting_name");
    config.meeting_initial_prompt = get_string("meeting_initial_prompt");
    config.meeting_timeout = get_int("meeting_timeout");
    config.meeting_max_single_pass = get_int("meeting_max_single_pass");
    config.silence_timeout = get_float("silence_timeout");

    // Push-to-talk settings
    config.ptt_mode = get_bool("ptt_mode");
    config.ptt_key = get_string("ptt_key");

    // Refinement settings
    config.refine = get_bool("refine");
    config.history_enabled = get_bool("history_enabled");

    // Accuracy settings
    config.entropy_thold = get_float("entropy_thold");
    config.logprob_thold = get_float("logprob_thold");
    config.no_speech_thold = get_float("no_speech_thold");
    config.length_penalty = get_float("length_penalty");
    config.best_of = get_int("best_of");
    config.suppress_nst = get_bool("suppress_nst");
    config.carry_initial_prompt = get_bool("carry_initial_prompt");
    config.normalize_audio = get_bool("normalize_audio");

    return config;
}

std::optional<std::string> ConfigManager::get_env_var(const std::string& name) const {
    const char* value = getenv(name.c_str());
    return value ? std::make_optional(std::string(value)) : std::nullopt;
}

std::optional<bool> ConfigManager::get_env_bool(const std::string& name) const {
    auto value = get_env_var(name);
    if (!value) return std::nullopt;
    
    std::string lower = *value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
        return true;
    } else if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
        return false;
    }
    
    return std::nullopt;
}

std::optional<int> ConfigManager::get_env_int(const std::string& name) const {
    auto value = get_env_var(name);
    if (!value) return std::nullopt;
    
    try {
        return std::stoi(*value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<float> ConfigManager::get_env_float(const std::string& name) const {
    auto value = get_env_var(name);
    if (!value) return std::nullopt;
    
    try {
        return std::stof(*value);
    } catch (...) {
        return std::nullopt;
    }
}

bool ConfigManager::set_config_value(ConfigData& config, const std::string& key, const std::string& value) {
    if (value.empty()) {
        // Unset the value
        if (key == "default_model") config.default_model = std::nullopt;
        else if (key == "models_directory") config.models_directory = std::nullopt;
        else if (key == "use_coreml") config.use_coreml = std::nullopt;
        else if (key == "coreml_no_ane") config.coreml_no_ane = std::nullopt;
        else if (key == "coreml_model") config.coreml_model = std::nullopt;
        else if (key == "capture_device") config.capture_device = std::nullopt;
        else if (key == "step_ms") config.step_ms = std::nullopt;
        else if (key == "length_ms") config.length_ms = std::nullopt;
        else if (key == "keep_ms") config.keep_ms = std::nullopt;
        else if (key == "vad_threshold") config.vad_threshold = std::nullopt;
        else if (key == "freq_threshold") config.freq_threshold = std::nullopt;
        else if (key == "threads") config.threads = std::nullopt;
        else if (key == "max_tokens") config.max_tokens = std::nullopt;
        else if (key == "beam_size") config.beam_size = std::nullopt;
        else if (key == "language") config.language = std::nullopt;
        else if (key == "translate") config.translate = std::nullopt;
        else if (key == "no_timestamps") config.no_timestamps = std::nullopt;
        else if (key == "print_special") config.print_special = std::nullopt;
        else if (key == "print_colors") config.print_colors = std::nullopt;
        else if (key == "save_audio") config.save_audio = std::nullopt;
        else if (key == "tinydiarize") config.tinydiarize = std::nullopt;
        else if (key == "output_file") config.output_file = std::nullopt;
        else if (key == "output_format") config.output_format = std::nullopt;
        else if (key == "output_mode") config.output_mode = std::nullopt;
        else if (key == "auto_copy_enabled") config.auto_copy_enabled = std::nullopt;
        else if (key == "auto_copy_max_duration_hours") config.auto_copy_max_duration_hours = std::nullopt;
        else if (key == "auto_copy_max_size_bytes") config.auto_copy_max_size_bytes = std::nullopt;
        else if (key == "meeting_mode") config.meeting_mode = std::nullopt;
        else if (key == "meeting_prompt") config.meeting_prompt = std::nullopt;
        else if (key == "meeting_name") config.meeting_name = std::nullopt;
        else if (key == "meeting_initial_prompt") config.meeting_initial_prompt = std::nullopt;
        else if (key == "meeting_timeout") config.meeting_timeout = std::nullopt;
        else if (key == "meeting_max_single_pass") config.meeting_max_single_pass = std::nullopt;
        else if (key == "silence_timeout") config.silence_timeout = std::nullopt;
        else if (key == "ptt_mode") config.ptt_mode = std::nullopt;
        else if (key == "ptt_key") config.ptt_key = std::nullopt;
        else if (key == "refine") config.refine = std::nullopt;
        else if (key == "history_enabled") config.history_enabled = std::nullopt;
        else if (key == "entropy_thold") config.entropy_thold = std::nullopt;
        else if (key == "logprob_thold") config.logprob_thold = std::nullopt;
        else if (key == "no_speech_thold") config.no_speech_thold = std::nullopt;
        else if (key == "length_penalty") config.length_penalty = std::nullopt;
        else if (key == "best_of") config.best_of = std::nullopt;
        else if (key == "suppress_nst") config.suppress_nst = std::nullopt;
        else if (key == "carry_initial_prompt") config.carry_initial_prompt = std::nullopt;
        else if (key == "normalize_audio") config.normalize_audio = std::nullopt;
        else return false;
        return true;
    }
    
    try {
        if (key == "default_model") config.default_model = value;
        else if (key == "models_directory") config.models_directory = value;
        else if (key == "use_coreml") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.use_coreml = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.use_coreml = false;
            else return false;
        }
        else if (key == "coreml_no_ane") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.coreml_no_ane = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.coreml_no_ane = false;
            else return false;
        }
        else if (key == "coreml_model") config.coreml_model = value;
        else if (key == "capture_device") config.capture_device = std::stoi(value);
        else if (key == "step_ms") config.step_ms = std::stoi(value);
        else if (key == "length_ms") config.length_ms = std::stoi(value);
        else if (key == "keep_ms") config.keep_ms = std::stoi(value);
        else if (key == "vad_threshold") config.vad_threshold = std::stof(value);
        else if (key == "freq_threshold") config.freq_threshold = std::stof(value);
        else if (key == "threads") config.threads = std::stoi(value);
        else if (key == "max_tokens") config.max_tokens = std::stoi(value);
        else if (key == "beam_size") config.beam_size = std::stoi(value);
        else if (key == "language") config.language = value;
        else if (key == "translate") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.translate = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.translate = false;
            else return false;
        }
        else if (key == "no_timestamps") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.no_timestamps = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.no_timestamps = false;
            else return false;
        }
        else if (key == "print_special") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.print_special = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.print_special = false;
            else return false;
        }
        else if (key == "print_colors") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.print_colors = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.print_colors = false;
            else return false;
        }
        else if (key == "save_audio") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.save_audio = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.save_audio = false;
            else return false;
        }
        else if (key == "tinydiarize") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.tinydiarize = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.tinydiarize = false;
            else return false;
        }
        else if (key == "output_file") config.output_file = value;
        else if (key == "output_format") config.output_format = value;
        else if (key == "output_mode") config.output_mode = value;
        else if (key == "auto_copy_enabled") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.auto_copy_enabled = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.auto_copy_enabled = false;
            else return false;
        }
        else if (key == "auto_copy_max_duration_hours") config.auto_copy_max_duration_hours = std::stoi(value);
        else if (key == "auto_copy_max_size_bytes") config.auto_copy_max_size_bytes = std::stoi(value);
        else if (key == "meeting_mode") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.meeting_mode = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.meeting_mode = false;
            else return false;
        }
        else if (key == "meeting_prompt") config.meeting_prompt = value;
        else if (key == "meeting_name") config.meeting_name = value;
        else if (key == "meeting_initial_prompt") config.meeting_initial_prompt = value;
        else if (key == "meeting_timeout") config.meeting_timeout = std::stoi(value);
        else if (key == "meeting_max_single_pass") config.meeting_max_single_pass = std::stoi(value);
        else if (key == "silence_timeout") config.silence_timeout = std::stof(value);
        else if (key == "ptt_mode") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.ptt_mode = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.ptt_mode = false;
            else return false;
        }
        else if (key == "ptt_key") config.ptt_key = value;
        else if (key == "refine") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.refine = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.refine = false;
            else return false;
        }
        else if (key == "history_enabled") {
            config.history_enabled = (value == "true" || value == "1" || value == "yes");
        }
        else if (key == "entropy_thold") config.entropy_thold = std::stof(value);
        else if (key == "logprob_thold") config.logprob_thold = std::stof(value);
        else if (key == "no_speech_thold") config.no_speech_thold = std::stof(value);
        else if (key == "length_penalty") config.length_penalty = std::stof(value);
        else if (key == "best_of") config.best_of = std::stoi(value);
        else if (key == "suppress_nst") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.suppress_nst = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.suppress_nst = false;
            else return false;
        }
        else if (key == "carry_initial_prompt") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.carry_initial_prompt = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.carry_initial_prompt = false;
            else return false;
        }
        else if (key == "normalize_audio") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.normalize_audio = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.normalize_audio = false;
            else return false;
        }
        else return false;
    } catch (...) {
        return false;
    }
    
    return true;
}

std::optional<std::string> ConfigManager::get_config_value(const ConfigData& config, const std::string& key) const {
    if (key == "default_model") return config.default_model;
    else if (key == "models_directory") return config.models_directory;
    else if (key == "use_coreml") return config.use_coreml ? std::make_optional(*config.use_coreml ? "true" : "false") : std::nullopt;
    else if (key == "coreml_no_ane") return config.coreml_no_ane ? std::make_optional(*config.coreml_no_ane ? "true" : "false") : std::nullopt;
    else if (key == "coreml_model") return config.coreml_model;
    else if (key == "capture_device") return config.capture_device ? std::make_optional(std::to_string(*config.capture_device)) : std::nullopt;
    else if (key == "step_ms") return config.step_ms ? std::make_optional(std::to_string(*config.step_ms)) : std::nullopt;
    else if (key == "length_ms") return config.length_ms ? std::make_optional(std::to_string(*config.length_ms)) : std::nullopt;
    else if (key == "keep_ms") return config.keep_ms ? std::make_optional(std::to_string(*config.keep_ms)) : std::nullopt;
    else if (key == "vad_threshold") return config.vad_threshold ? std::make_optional(std::to_string(*config.vad_threshold)) : std::nullopt;
    else if (key == "freq_threshold") return config.freq_threshold ? std::make_optional(std::to_string(*config.freq_threshold)) : std::nullopt;
    else if (key == "threads") return config.threads ? std::make_optional(std::to_string(*config.threads)) : std::nullopt;
    else if (key == "max_tokens") return config.max_tokens ? std::make_optional(std::to_string(*config.max_tokens)) : std::nullopt;
    else if (key == "beam_size") return config.beam_size ? std::make_optional(std::to_string(*config.beam_size)) : std::nullopt;
    else if (key == "language") return config.language;
    else if (key == "translate") return config.translate ? std::make_optional(*config.translate ? "true" : "false") : std::nullopt;
    else if (key == "no_timestamps") return config.no_timestamps ? std::make_optional(*config.no_timestamps ? "true" : "false") : std::nullopt;
    else if (key == "print_special") return config.print_special ? std::make_optional(*config.print_special ? "true" : "false") : std::nullopt;
    else if (key == "print_colors") return config.print_colors ? std::make_optional(*config.print_colors ? "true" : "false") : std::nullopt;
    else if (key == "save_audio") return config.save_audio ? std::make_optional(*config.save_audio ? "true" : "false") : std::nullopt;
    else if (key == "tinydiarize") return config.tinydiarize ? std::make_optional(*config.tinydiarize ? "true" : "false") : std::nullopt;
    else if (key == "output_file") return config.output_file;
    else if (key == "output_format") return config.output_format;
    else if (key == "output_mode") return config.output_mode;
    else if (key == "auto_copy_enabled") return config.auto_copy_enabled ? std::make_optional(*config.auto_copy_enabled ? "true" : "false") : std::nullopt;
    else if (key == "auto_copy_max_duration_hours") return config.auto_copy_max_duration_hours ? std::make_optional(std::to_string(*config.auto_copy_max_duration_hours)) : std::nullopt;
    else if (key == "auto_copy_max_size_bytes") return config.auto_copy_max_size_bytes ? std::make_optional(std::to_string(*config.auto_copy_max_size_bytes)) : std::nullopt;
    else if (key == "meeting_mode") return config.meeting_mode ? std::make_optional(*config.meeting_mode ? "true" : "false") : std::nullopt;
    else if (key == "meeting_prompt") return config.meeting_prompt;
    else if (key == "meeting_name") return config.meeting_name;
    else if (key == "meeting_initial_prompt") return config.meeting_initial_prompt;
    else if (key == "meeting_timeout") return config.meeting_timeout ? std::make_optional(std::to_string(*config.meeting_timeout)) : std::nullopt;
    else if (key == "meeting_max_single_pass") return config.meeting_max_single_pass ? std::make_optional(std::to_string(*config.meeting_max_single_pass)) : std::nullopt;
    else if (key == "silence_timeout") return config.silence_timeout ? std::make_optional(std::to_string(*config.silence_timeout)) : std::nullopt;
    else if (key == "ptt_mode") return config.ptt_mode ? std::make_optional(*config.ptt_mode ? "true" : "false") : std::nullopt;
    else if (key == "ptt_key") return config.ptt_key;
    else if (key == "refine") return config.refine ? std::make_optional(*config.refine ? "true" : "false") : std::nullopt;
    else if (key == "history_enabled") {
        return config.history_enabled ?
            std::make_optional(*config.history_enabled ? "true" : "false") : std::nullopt;
    }
    else if (key == "entropy_thold") return config.entropy_thold ? std::make_optional(std::to_string(*config.entropy_thold)) : std::nullopt;
    else if (key == "logprob_thold") return config.logprob_thold ? std::make_optional(std::to_string(*config.logprob_thold)) : std::nullopt;
    else if (key == "no_speech_thold") return config.no_speech_thold ? std::make_optional(std::to_string(*config.no_speech_thold)) : std::nullopt;
    else if (key == "length_penalty") return config.length_penalty ? std::make_optional(std::to_string(*config.length_penalty)) : std::nullopt;
    else if (key == "best_of") return config.best_of ? std::make_optional(std::to_string(*config.best_of)) : std::nullopt;
    else if (key == "suppress_nst") return config.suppress_nst ? std::make_optional(*config.suppress_nst ? "true" : "false") : std::nullopt;
    else if (key == "carry_initial_prompt") return config.carry_initial_prompt ? std::make_optional(*config.carry_initial_prompt ? "true" : "false") : std::nullopt;
    else if (key == "normalize_audio") return config.normalize_audio ? std::make_optional(*config.normalize_audio ? "true" : "false") : std::nullopt;

    return std::nullopt;
}
