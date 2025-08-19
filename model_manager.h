#pragma once

#include <string>
#include <vector>
#include <map>

struct ModelInfo {
    std::string name;
    std::string description;
    std::string url;
    std::string coreml_url;
    std::string filename;
    std::string coreml_filename;
    size_t size_mb;
    bool multilingual;
};

class ModelManager {
public:
    ModelManager();
    
    // Check if model exists locally
    bool model_exists(const std::string& model_name);
    bool coreml_model_exists(const std::string& model_name);
    
    // Get model path
    std::string get_model_path(const std::string& model_name);
    std::string get_coreml_model_path(const std::string& model_name);
    
    // Download models
    bool download_model(const std::string& model_name, bool show_progress = true);
    bool download_coreml_model(const std::string& model_name, bool show_progress = true);
    
    // List available models
    void list_available_models();
    std::vector<std::string> get_model_names();
    
    // Interactive model selection
    std::string prompt_model_selection();
    bool prompt_download_confirmation(const std::string& model_name);
    std::string prompt_model_not_found(const std::string& model_name, bool use_coreml);
    
    // Get model info
    ModelInfo get_model_info(const std::string& model_name);
    
    // Setup models directory
    void ensure_models_directory();
    
    // Auto-resolve model (main entry point)
    std::string resolve_model(const std::string& model_arg, bool use_coreml);

private:
    std::map<std::string, ModelInfo> models_;
    std::string models_dir_;
    
    void init_model_registry();
    bool download_file(const std::string& url, const std::string& filepath, bool show_progress);
    bool extract_coreml_model(const std::string& zip_path, const std::string& extract_dir);
    void show_usage_examples(const std::string& model_name);
    void show_download_progress(const std::string& filename, size_t downloaded, size_t total);
};