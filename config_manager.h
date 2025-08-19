#pragma once

#include <string>
#include <map>
#include <optional>
#include <vector>

// Forward declaration for whisper_params
struct whisper_params;

struct ConfigData {
    // Model settings
    std::optional<std::string> default_model;
    std::optional<std::string> models_directory;
    std::optional<bool> use_coreml;
    std::optional<std::string> coreml_model;
    
    // Audio settings
    std::optional<int> capture_device;
    std::optional<int> step_ms;
    std::optional<int> length_ms;
    std::optional<int> keep_ms;
    std::optional<float> vad_threshold;
    std::optional<float> freq_threshold;
    
    // Processing settings
    std::optional<int> threads;
    std::optional<int> max_tokens;
    std::optional<int> beam_size;
    std::optional<std::string> language;
    std::optional<bool> translate;
    std::optional<bool> no_timestamps;
    std::optional<bool> print_special;
    std::optional<bool> save_audio;
    
    // Output settings
    std::optional<std::string> output_file;
    std::optional<std::string> output_format; // json, plain, timestamped
};

class ConfigManager {
public:
    ConfigManager();
    
    // Load configuration from files and environment
    void load_config();
    
    // Apply configuration to whisper_params
    void apply_to_params(whisper_params& params) const;
    
    // Configuration management
    bool set_config(const std::string& key, const std::string& value);
    std::optional<std::string> get_config(const std::string& key) const;
    bool unset_config(const std::string& key);
    void list_config() const;
    void reset_config();
    
    // File operations
    bool save_user_config() const;
    bool save_project_config() const;
    
    // Environment variable loading
    void load_env_vars();
    
    // Validation
    bool validate_config() const;
    
    // Get effective configuration (merged from all sources)
    ConfigData get_effective_config() const;

private:
    ConfigData user_config_;
    ConfigData project_config_;
    ConfigData env_config_;
    
    std::string user_config_path_;
    std::string project_config_path_;
    
    // Helper methods
    void init_config_paths();
    ConfigData load_config_file(const std::string& filepath) const;
    bool save_config_file(const std::string& filepath, const ConfigData& config) const;
    ConfigData merge_configs(const std::vector<ConfigData>& configs) const;
    
    // JSON serialization
    std::string config_to_json(const ConfigData& config) const;
    ConfigData json_to_config(const std::string& json_str) const;
    
    // Environment variable helpers
    std::optional<std::string> get_env_var(const std::string& name) const;
    std::optional<bool> get_env_bool(const std::string& name) const;
    std::optional<int> get_env_int(const std::string& name) const;
    std::optional<float> get_env_float(const std::string& name) const;
    
    // Config key mapping
    std::map<std::string, std::string> get_config_key_map() const;
    bool set_config_value(ConfigData& config, const std::string& key, const std::string& value);
    std::optional<std::string> get_config_value(const ConfigData& config, const std::string& key) const;
};