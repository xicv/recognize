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

// Include whisper_params definition
struct whisper_params {
    int32_t n_threads = 4;
    int32_t step_ms = 3000;
    int32_t length_ms = 10000;
    int32_t keep_ms = 200;
    int32_t capture_id = -1;
    int32_t max_tokens = 32;
    int32_t audio_ctx = 0;
    int32_t beam_size = -1;

    float vad_thold = 0.6f;
    float freq_thold = 100.0f;

    bool translate = false;
    bool no_fallback = false;
    bool print_special = false;
    bool no_context = true;
    bool no_timestamps = false;
    bool tinydiarize = false;
    bool save_audio = false;
    bool use_coreml = true;
    bool use_gpu = true;
    bool flash_attn = false;

    std::string language = "en";
    std::string model = "";
    std::string coreml_model = "";
    std::string fname_out;
    bool list_models = false;
};

ConfigManager::ConfigManager() {
    init_config_paths();
}

void ConfigManager::init_config_paths() {
    // User config: ~/.whisper-stream-coreml/config.json
    const char* home = getenv("HOME");
    if (home) {
        std::string config_dir = std::string(home) + "/.whisper-stream-coreml";
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
    if (effective.save_audio) params.save_audio = *effective.save_audio;
    if (effective.output_file) params.fname_out = *effective.output_file;
}

std::map<std::string, std::string> ConfigManager::get_config_key_map() const {
    return {
        {"model", "default_model"},
        {"default_model", "default_model"},
        {"models_dir", "models_directory"},
        {"models_directory", "models_directory"},
        {"coreml", "use_coreml"},
        {"use_coreml", "use_coreml"},
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
        {"save_audio", "save_audio"},
        {"output", "output_file"},
        {"output_file", "output_file"},
        {"format", "output_format"},
        {"output_format", "output_format"}
    };
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
    
    auto print_config = [](const std::string& section, const ConfigData& config) {
        std::cout << section << ":\n";
        
        if (config.default_model) 
            std::cout << "  default_model: " << *config.default_model << "\n";
        if (config.models_directory) 
            std::cout << "  models_directory: " << *config.models_directory << "\n";
        if (config.use_coreml) 
            std::cout << "  use_coreml: " << (*config.use_coreml ? "true" : "false") << "\n";
        if (config.coreml_model) 
            std::cout << "  coreml_model: " << *config.coreml_model << "\n";
        if (config.capture_device) 
            std::cout << "  capture_device: " << *config.capture_device << "\n";
        if (config.step_ms) 
            std::cout << "  step_ms: " << *config.step_ms << "\n";
        if (config.length_ms) 
            std::cout << "  length_ms: " << *config.length_ms << "\n";
        if (config.keep_ms) 
            std::cout << "  keep_ms: " << *config.keep_ms << "\n";
        if (config.vad_threshold) 
            std::cout << "  vad_threshold: " << *config.vad_threshold << "\n";
        if (config.freq_threshold) 
            std::cout << "  freq_threshold: " << *config.freq_threshold << "\n";
        if (config.threads) 
            std::cout << "  threads: " << *config.threads << "\n";
        if (config.max_tokens) 
            std::cout << "  max_tokens: " << *config.max_tokens << "\n";
        if (config.beam_size) 
            std::cout << "  beam_size: " << *config.beam_size << "\n";
        if (config.language) 
            std::cout << "  language: " << *config.language << "\n";
        if (config.translate) 
            std::cout << "  translate: " << (*config.translate ? "true" : "false") << "\n";
        if (config.no_timestamps) 
            std::cout << "  no_timestamps: " << (*config.no_timestamps ? "true" : "false") << "\n";
        if (config.print_special) 
            std::cout << "  print_special: " << (*config.print_special ? "true" : "false") << "\n";
        if (config.save_audio) 
            std::cout << "  save_audio: " << (*config.save_audio ? "true" : "false") << "\n";
        if (config.output_file) 
            std::cout << "  output_file: " << *config.output_file << "\n";
        if (config.output_format) 
            std::cout << "  output_format: " << *config.output_format << "\n";
        
        std::cout << "\n";
    };
    
    print_config("Effective Configuration", effective);
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
    env_config_.save_audio = get_env_bool("WHISPER_SAVE_AUDIO");
    env_config_.output_file = get_env_var("WHISPER_OUTPUT_FILE");
    env_config_.output_format = get_env_var("WHISPER_OUTPUT_FORMAT");
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
        if (config.save_audio) merged.save_audio = config.save_audio;
        if (config.output_file) merged.output_file = config.output_file;
        if (config.output_format) merged.output_format = config.output_format;
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
    if (config.save_audio) add_bool("save_audio", *config.save_audio);
    if (config.output_file) add_field("output_file", *config.output_file);
    if (config.output_format) add_field("output_format", *config.output_format);
    
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
    config.save_audio = get_bool("save_audio");
    config.output_file = get_string("output_file");
    config.output_format = get_string("output_format");
    
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
        else if (key == "save_audio") config.save_audio = std::nullopt;
        else if (key == "output_file") config.output_file = std::nullopt;
        else if (key == "output_format") config.output_format = std::nullopt;
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
        else if (key == "save_audio") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "true" || lower == "1" || lower == "yes") config.save_audio = true;
            else if (lower == "false" || lower == "0" || lower == "no") config.save_audio = false;
            else return false;
        }
        else if (key == "output_file") config.output_file = value;
        else if (key == "output_format") config.output_format = value;
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
    else if (key == "save_audio") return config.save_audio ? std::make_optional(*config.save_audio ? "true" : "false") : std::nullopt;
    else if (key == "output_file") return config.output_file;
    else if (key == "output_format") return config.output_format;
    
    return std::nullopt;
}