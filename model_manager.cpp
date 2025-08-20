#include "model_manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <set>

#ifdef __APPLE__
#include <sys/stat.h>
#include <unistd.h>
#endif

ModelManager::ModelManager() {
    // Use global models directory if installed system-wide
    const char* home = getenv("HOME");
    if (home && std::filesystem::exists("/usr/local/bin/recognize")) {
        models_dir_ = std::string(home) + "/.recognize/models";
    } else {
        models_dir_ = "models";
    }
    init_model_registry();
    ensure_models_directory();
}

void ModelManager::init_model_registry() {
    // English-only models (recommended for most users)
    models_["tiny.en"] = {
        "tiny.en",
        "Tiny English model (39 MB) - Fastest processing, lower accuracy",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en-encoder.mlmodelc.zip",
        "ggml-tiny.en.bin",
        "ggml-tiny.en-encoder.mlmodelc",
        39,
        false
    };
    
    models_["base.en"] = {
        "base.en",
        "Base English model (148 MB) - Good balance of speed and accuracy",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-encoder.mlmodelc.zip",
        "ggml-base.en.bin",
        "ggml-base.en-encoder.mlmodelc",
        148,
        false
    };
    
    models_["small.en"] = {
        "small.en",
        "Small English model (488 MB) - Higher accuracy than base",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en-encoder.mlmodelc.zip",
        "ggml-small.en.bin",
        "ggml-small.en-encoder.mlmodelc",
        488,
        false
    };
    
    models_["medium.en"] = {
        "medium.en",
        "Medium English model (1.5 GB) - Very high accuracy, slower",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.en.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.en-encoder.mlmodelc.zip",
        "ggml-medium.en.bin",
        "ggml-medium.en-encoder.mlmodelc",
        1540,
        false
    };
    
    models_["large"] = {
        "large",
        "Large English model (3.1 GB) - Highest accuracy, slowest",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-encoder.mlmodelc.zip",
        "ggml-large-v3.bin",
        "ggml-large-v3-encoder.mlmodelc",
        3100,
        false
    };
    
    // Multilingual models
    models_["tiny"] = {
        "tiny",
        "Tiny multilingual model (39 MB) - 99 languages, lower accuracy",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny-encoder.mlmodelc.zip",
        "ggml-tiny.bin",
        "ggml-tiny-encoder.mlmodelc",
        39,
        true
    };
    
    models_["base"] = {
        "base",
        "Base multilingual model (148 MB) - 99 languages, good balance",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base-encoder.mlmodelc.zip",
        "ggml-base.bin",
        "ggml-base-encoder.mlmodelc",
        148,
        true
    };
    
    models_["small"] = {
        "small",
        "Small multilingual model (488 MB) - 99 languages, higher accuracy",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-encoder.mlmodelc.zip",
        "ggml-small.bin",
        "ggml-small-encoder.mlmodelc",
        488,
        true
    };
    
    models_["medium"] = {
        "medium",
        "Medium multilingual model (1.5 GB) - 99 languages, very high accuracy",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium-encoder.mlmodelc.zip",
        "ggml-medium.bin",
        "ggml-medium-encoder.mlmodelc",
        1540,
        true
    };
    
    models_["large-v3"] = {
        "large-v3",
        "Large multilingual model (3.1 GB) - 99 languages, highest accuracy",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin",
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-encoder.mlmodelc.zip",
        "ggml-large-v3.bin",
        "ggml-large-v3-encoder.mlmodelc",
        3100,
        true
    };
}

void ModelManager::ensure_models_directory() {
    std::filesystem::create_directories(models_dir_);
}

void ModelManager::set_models_directory(const std::string& models_dir) {
    models_dir_ = models_dir;
    ensure_models_directory();
}

std::string ModelManager::get_models_directory() const {
    return models_dir_;
}

bool ModelManager::model_exists(const std::string& model_name) {
    if (models_.find(model_name) == models_.end()) {
        return false;
    }
    
    std::string path = get_model_path(model_name);
    return std::filesystem::exists(path);
}

bool ModelManager::coreml_model_exists(const std::string& model_name) {
    if (models_.find(model_name) == models_.end()) {
        return false;
    }
    
    std::string path = get_coreml_model_path(model_name);
    return std::filesystem::exists(path);
}

std::string ModelManager::get_model_path(const std::string& model_name) {
    if (models_.find(model_name) == models_.end()) {
        return "";
    }
    
    return models_dir_ + "/" + models_[model_name].filename;
}

std::string ModelManager::get_coreml_model_path(const std::string& model_name) {
    if (models_.find(model_name) == models_.end()) {
        return "";
    }
    
    return models_dir_ + "/" + models_[model_name].coreml_filename;
}

void ModelManager::list_available_models() {
    std::cout << "\n🤖 Available Whisper Models:\n\n";
    
    std::cout << "📱 English-only models (recommended for English speech):\n";
    for (const auto& [name, info] : models_) {
        if (!info.multilingual) {
            std::string status = model_exists(name) ? "✅ Downloaded" : "⬇️  Available";
            std::cout << "  " << name << " - " << info.description << " [" << status << "]\n";
        }
    }
    
    std::cout << "\n🌍 Multilingual models (99 languages):\n";
    for (const auto& [name, info] : models_) {
        if (info.multilingual) {
            std::string status = model_exists(name) ? "✅ Downloaded" : "⬇️  Available";
            std::cout << "  " << name << " - " << info.description << " [" << status << "]\n";
        }
    }
    
    std::cout << "\n💡 Recommendation: Start with 'base.en' for English speech (good speed/accuracy balance)\n";
}

std::vector<std::string> ModelManager::get_model_names() {
    std::vector<std::string> names;
    for (const auto& [name, info] : models_) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> ModelManager::get_downloaded_models() {
    std::vector<std::string> downloaded;
    for (const auto& [name, info] : models_) {
        if (model_exists(name)) {
            downloaded.push_back(name);
        }
    }
    return downloaded;
}

void ModelManager::list_downloaded_models() {
    auto downloaded = get_downloaded_models();
    
    if (downloaded.empty()) {
        std::cout << "\n📁 No models downloaded yet.\n";
        std::cout << "💡 Run 'recognize --list-models' to see available models for download.\n\n";
        return;
    }
    
    std::cout << "\n📁 Downloaded Models:\n\n";
    
    size_t total_size = 0;
    
    for (const auto& name : downloaded) {
        ModelInfo info = get_model_info(name);
        std::string model_path = get_model_path(name);
        std::string coreml_path = get_coreml_model_path(name);
        
        // Calculate actual file sizes
        size_t model_size = 0;
        size_t coreml_size = 0;
        
        if (std::filesystem::exists(model_path)) {
            model_size = std::filesystem::file_size(model_path);
        }
        
        if (coreml_model_exists(name)) {
            // For CoreML models, we need to calculate directory size
            if (std::filesystem::exists(coreml_path) && std::filesystem::is_directory(coreml_path)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(coreml_path)) {
                    if (entry.is_regular_file()) {
                        coreml_size += entry.file_size();
                    }
                }
            }
        }
        
        total_size += model_size + coreml_size;
        
        std::cout << "  ✅ " << name << " - " << info.description << "\n";
        std::cout << "     📊 Size: " << (model_size / 1024 / 1024) << " MB";
        if (coreml_size > 0) {
            std::cout << " + " << (coreml_size / 1024 / 1024) << " MB CoreML";
        }
        std::cout << "\n";
        std::cout << "     📂 Path: " << model_path << "\n";
        if (coreml_size > 0) {
            std::cout << "     🚀 CoreML: " << coreml_path << "\n";
        }
        std::cout << "\n";
    }
    
    std::cout << "📊 Total storage used: " << (total_size / 1024 / 1024) << " MB\n\n";
}

void ModelManager::show_storage_usage() {
    std::cout << "\n📊 Model Storage Usage:\n\n";
    
    if (!std::filesystem::exists(models_dir_)) {
        std::cout << "📁 Models directory doesn't exist yet: " << models_dir_ << "\n\n";
        return;
    }
    
    size_t total_size = 0;
    size_t model_files = 0;
    size_t coreml_files = 0;
    size_t other_files = 0;
    
    std::cout << "📂 Directory: " << models_dir_ << "\n\n";
    
    for (const auto& entry : std::filesystem::directory_iterator(models_dir_)) {
        if (entry.is_regular_file()) {
            size_t file_size = entry.file_size();
            total_size += file_size;
            
            std::string filename = entry.path().filename().string();
            if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".bin") {
                model_files++;
                std::cout << "  📄 " << filename << " - " << (file_size / 1024 / 1024) << " MB\n";
            } else if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".zip") {
                coreml_files++;
                std::cout << "  📦 " << filename << " - " << (file_size / 1024 / 1024) << " MB (CoreML zip)\n";
            } else {
                other_files++;
                std::cout << "  📄 " << filename << " - " << (file_size / 1024 / 1024) << " MB\n";
            }
        } else if (entry.is_directory()) {
            std::string dirname = entry.path().filename().string();
            if (dirname.length() >= 9 && dirname.substr(dirname.length() - 9) == ".mlmodelc") {
                size_t dir_size = 0;
                for (const auto& subentry : std::filesystem::recursive_directory_iterator(entry.path())) {
                    if (subentry.is_regular_file()) {
                        dir_size += subentry.file_size();
                    }
                }
                total_size += dir_size;
                coreml_files++;
                std::cout << "  🚀 " << dirname << "/ - " << (dir_size / 1024 / 1024) << " MB (CoreML)\n";
            }
        }
    }
    
    std::cout << "\n📈 Summary:\n";
    std::cout << "  📄 Model files: " << model_files << "\n";
    std::cout << "  🚀 CoreML files: " << coreml_files << "\n";
    if (other_files > 0) {
        std::cout << "  📁 Other files: " << other_files << "\n";
    }
    std::cout << "  📊 Total size: " << (total_size / 1024 / 1024) << " MB\n\n";
}

std::string ModelManager::prompt_model_selection() {
    std::cout << "\n🤔 No model specified. Let's choose one!\n";
    list_available_models();
    
    std::cout << "\nWhich model would you like to use? ";
    std::cout << "(or 'q' to quit): ";
    
    std::string choice;
    std::getline(std::cin, choice);
    
    if (choice == "q" || choice == "quit") {
        return "";
    }
    
    if (models_.find(choice) != models_.end()) {
        return choice;
    }
    
    std::cout << "❌ Invalid model name. Please choose from the list above.\n";
    return prompt_model_selection();
}

bool ModelManager::prompt_download_confirmation(const std::string& model_name) {
    ModelInfo info = get_model_info(model_name);
    
    std::cout << "\n📦 Model '" << model_name << "' not found locally.\n";
    std::cout << "📄 " << info.description << "\n";
    std::cout << "📁 Size: " << info.size_mb << " MB\n";
    
#ifdef __APPLE__
    std::cout << "🚀 CoreML acceleration: Available\n";
#endif
    
    std::cout << "\nChoose an option:\n";
    std::cout << "  1. Download '" << model_name << "' (" << info.size_mb << " MB)\n";
    std::cout << "  2. Choose a different model\n";
    std::cout << "  3. Cancel\n";
    std::cout << "\nEnter choice [1-3]: ";
    
    std::string response;
    std::getline(std::cin, response);
    
    if (response == "1" || response.empty()) {
        return true;  // Download the requested model
    } else if (response == "2") {
        return false; // This will be handled differently - we'll trigger model selection
    } else {
        return false; // Cancel
    }
}

std::string ModelManager::prompt_model_not_found(const std::string& model_name, bool use_coreml) {
    ModelInfo info = get_model_info(model_name);
    
    std::cout << "\n📦 Model '" << model_name << "' not found locally.\n";
    std::cout << "📄 " << info.description << "\n";
    std::cout << "📁 Size: " << info.size_mb << " MB\n";
    
#ifdef __APPLE__
    std::cout << "🚀 CoreML acceleration: Available\n";
#endif
    
    std::cout << "\nChoose an option:\n";
    std::cout << "  1. Download '" << model_name << "' (" << info.size_mb << " MB)\n";
    std::cout << "  2. Choose a different model\n";
    std::cout << "  3. Cancel\n";
    std::cout << "\nEnter choice [1-3]: ";
    
    std::string response;
    std::getline(std::cin, response);
    
    if (response == "1" || response.empty()) {
        // Download the requested model
        std::cout << "\n🚀 Starting download...\n";
        
        if (!download_model(model_name)) {
            return "";
        }
        
        #ifdef __APPLE__
        if (use_coreml) {
            std::cout << "\n🤖 Downloading CoreML acceleration model...\n";
            if (!download_coreml_model(model_name)) {
                std::cout << "⚠️  CoreML download failed, will use regular model\n";
            }
        }
        #endif
        
        show_usage_examples(model_name);
        return get_model_path(model_name);
        
    } else if (response == "2") {
        // Let user choose a different model
        std::string selected_model = prompt_model_selection();
        if (selected_model.empty()) {
            return "";
        }
        
        // Recursively resolve the selected model
        return resolve_model(selected_model, use_coreml);
        
    } else {
        std::cout << "\n❌ Operation cancelled.\n";
        return "";
    }
}

ModelInfo ModelManager::get_model_info(const std::string& model_name) {
    if (models_.find(model_name) != models_.end()) {
        return models_[model_name];
    }
    return {};
}

bool ModelManager::download_file(const std::string& url, const std::string& filepath, bool show_progress) {
    std::cout << "⬇️  Downloading: " << std::filesystem::path(filepath).filename().string() << "\n";
    std::cout << "🔗 From: " << url << "\n";
    
    // Use curl to download with progress
    std::string command = "curl -L --progress-bar \"" + url + "\" -o \"" + filepath + "\"";
    
    if (show_progress) {
        std::cout << "📊 Progress:\n";
    }
    
    int result = std::system(command.c_str());
    
    if (result == 0 && std::filesystem::exists(filepath)) {
        std::cout << "✅ Download completed: " << filepath << "\n";
        return true;
    } else {
        std::cout << "❌ Download failed for: " << filepath << "\n";
        return false;
    }
}

bool ModelManager::extract_coreml_model(const std::string& zip_path, const std::string& extract_dir) {
    std::cout << "📦 Extracting CoreML model...\n";
    
    // Check if zip file exists
    if (!std::filesystem::exists(zip_path)) {
        std::cout << "❌ Zip file not found: " << zip_path << "\n";
        return false;
    }
    
    // Get absolute paths
    std::filesystem::path abs_zip_path = std::filesystem::absolute(zip_path);
    std::filesystem::path abs_extract_dir = std::filesystem::absolute(extract_dir);
    
    // Ensure extract directory exists
    std::filesystem::create_directories(abs_extract_dir);
    
    std::string command = "cd \"" + abs_extract_dir.string() + "\" && unzip -q \"" + abs_zip_path.string() + "\"";
    
    // Debug output
    std::cout << "Extracting: " << abs_zip_path.string() << "\n";
    std::cout << "To: " << abs_extract_dir.string() << "\n";
    
    int result = std::system(command.c_str());
    
    if (result == 0) {
        std::cout << "✅ CoreML model extracted successfully\n";
        // Remove the zip file to save space
        std::filesystem::remove(zip_path);
        return true;
    } else {
        std::cout << "❌ Failed to extract CoreML model (exit code: " << result << ")\n";
        std::cout << "Command: " << command << "\n";
        return false;
    }
}

bool ModelManager::download_model(const std::string& model_name, bool show_progress) {
    if (models_.find(model_name) == models_.end()) {
        std::cout << "❌ Unknown model: " << model_name << "\n";
        return false;
    }
    
    ModelInfo info = models_[model_name];
    std::string filepath = get_model_path(model_name);
    
    return download_file(info.url, filepath, show_progress);
}

bool ModelManager::download_coreml_model(const std::string& model_name, bool show_progress) {
#ifndef __APPLE__
    std::cout << "ℹ️  CoreML models are only available on macOS\n";
    return false;
#endif
    
    if (models_.find(model_name) == models_.end()) {
        std::cout << "❌ Unknown model: " << model_name << "\n";
        return false;
    }
    
    ModelInfo info = models_[model_name];
    std::string zip_path = models_dir_ + "/" + info.coreml_filename + ".zip";
    
    // Download the zip file
    if (!download_file(info.coreml_url, zip_path, show_progress)) {
        return false;
    }
    
    // Extract it
    return extract_coreml_model(zip_path, models_dir_);
}

void ModelManager::show_usage_examples(const std::string& model_name) {
    std::cout << "\n🎉 Setup complete! Here's how to use your model:\n\n";
    
    std::cout << "🎤 Basic real-time transcription:\n";
    std::cout << "   recognize -m " << model_name << "\n\n";
    
    std::cout << "🎯 VAD mode (recommended - only transcribes when you speak):\n";
    std::cout << "   recognize -m " << model_name << " --step 0 --length 30000\n\n";
    
    std::cout << "⚡ Continuous mode (transcribes every 500ms):\n";
    std::cout << "   recognize -m " << model_name << " --step 500 --length 5000\n\n";
    
    std::cout << "💾 Save transcription to file:\n";
    std::cout << "   recognize -m " << model_name << " -f transcript.txt\n\n";
    
    std::cout << "🎛️  Use specific microphone:\n";
    std::cout << "   recognize -m " << model_name << " -c 3\n\n";
    
    if (models_[model_name].multilingual) {
        std::cout << "🌍 Transcribe other languages:\n";
        std::cout << "   recognize -m " << model_name << " -l es  # Spanish\n";
        std::cout << "   recognize -m " << model_name << " -l fr  # French\n\n";
        
        std::cout << "🔄 Translate to English:\n";
        std::cout << "   recognize -m " << model_name << " -l es --translate\n\n";
    }
    
    std::cout << "📚 For more options: recognize --help\n\n";
    std::cout << "🚀 Ready to start? Try the VAD mode command above!\n";
}

std::string ModelManager::resolve_model(const std::string& model_arg, bool use_coreml) {
    std::string model_name = model_arg;
    
    // If no model specified, prompt user
    if (model_name.empty()) {
        model_name = prompt_model_selection();
        if (model_name.empty()) {
            return "";
        }
    }
    
    // Check if it's a direct file path
    if (std::filesystem::exists(model_name)) {
        std::cout << "✅ Using existing model file: " << model_name << "\n";
        return model_name;
    }
    
    // Check if it's a known model name
    if (models_.find(model_name) == models_.end()) {
        std::cout << "❌ Unknown model: " << model_name << "\n";
        std::cout << "Available models:\n";
        for (const auto& name : get_model_names()) {
            std::cout << "  - " << name << "\n";
        }
        return "";
    }
    
    // Check if model exists locally
    if (model_exists(model_name)) {
        std::cout << "✅ Using cached model: " << model_name << "\n";
        
        // Also check/download CoreML model if requested and not exists
        #ifdef __APPLE__
        if (use_coreml && !coreml_model_exists(model_name)) {
            std::cout << "🚀 CoreML acceleration requested but CoreML model not found.\n";
            std::cout << "Would you like to download the CoreML version? [Y/n]: ";
            std::string response;
            std::getline(std::cin, response);
            
            if (response.empty() || response == "y" || response == "Y") {
                download_coreml_model(model_name);
            }
        }
        #endif
        
        return get_model_path(model_name);
    }
    
    // Model doesn't exist, use enhanced prompt with model selection option
    return prompt_model_not_found(model_name, use_coreml);
}

bool ModelManager::delete_model(const std::string& model_name, bool confirm) {
    if (models_.find(model_name) == models_.end()) {
        std::cout << "❌ Unknown model: " << model_name << "\n";
        std::cout << "💡 Run 'recognize --list-models' to see available models.\n\n";
        return false;
    }
    
    if (!model_exists(model_name)) {
        std::cout << "❌ Model '" << model_name << "' is not downloaded.\n\n";
        return false;
    }
    
    ModelInfo info = get_model_info(model_name);
    std::string model_path = get_model_path(model_name);
    std::string coreml_path = get_coreml_model_path(model_name);
    bool has_coreml = coreml_model_exists(model_name);
    
    // Calculate total size
    size_t total_size = 0;
    if (std::filesystem::exists(model_path)) {
        total_size += std::filesystem::file_size(model_path);
    }
    if (has_coreml && std::filesystem::exists(coreml_path)) {
        if (std::filesystem::is_directory(coreml_path)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(coreml_path)) {
                if (entry.is_regular_file()) {
                    total_size += entry.file_size();
                }
            }
        }
    }
    
    if (confirm) {
        std::cout << "\n🗑️  Delete Model: " << model_name << "\n";
        std::cout << "📄 " << info.description << "\n";
        std::cout << "📁 Size: " << (total_size / 1024 / 1024) << " MB\n";
        std::cout << "📂 Path: " << model_path << "\n";
        if (has_coreml) {
            std::cout << "🚀 CoreML: " << coreml_path << "\n";
        }
        std::cout << "\n⚠️  This action cannot be undone.\n";
        std::cout << "Are you sure you want to delete this model? [y/N]: ";
        
        std::string response;
        std::getline(std::cin, response);
        
        if (response != "y" && response != "Y") {
            std::cout << "❌ Deletion cancelled.\n\n";
            return false;
        }
    }
    
    bool success = true;
    
    // Delete main model file
    try {
        if (std::filesystem::exists(model_path)) {
            std::filesystem::remove(model_path);
            std::cout << "✅ Deleted: " << model_path << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "❌ Failed to delete " << model_path << ": " << e.what() << "\n";
        success = false;
    }
    
    // Delete CoreML model
    if (has_coreml) {
        try {
            if (std::filesystem::exists(coreml_path)) {
                if (std::filesystem::is_directory(coreml_path)) {
                    std::filesystem::remove_all(coreml_path);
                } else {
                    std::filesystem::remove(coreml_path);
                }
                std::cout << "✅ Deleted CoreML: " << coreml_path << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "❌ Failed to delete CoreML " << coreml_path << ": " << e.what() << "\n";
            success = false;
        }
    }
    
    // Also clean up any zip files
    std::string zip_path = models_dir_ + "/" + info.coreml_filename + ".zip";
    if (std::filesystem::exists(zip_path)) {
        try {
            std::filesystem::remove(zip_path);
            std::cout << "✅ Deleted zip: " << zip_path << "\n";
        } catch (const std::exception& e) {
            std::cout << "❌ Failed to delete zip " << zip_path << ": " << e.what() << "\n";
        }
    }
    
    if (success) {
        std::cout << "✅ Model '" << model_name << "' deleted successfully.\n";
        std::cout << "💾 Freed " << (total_size / 1024 / 1024) << " MB of storage.\n\n";
    }
    
    return success;
}

bool ModelManager::delete_all_models(bool confirm) {
    auto downloaded = get_downloaded_models();
    
    if (downloaded.empty()) {
        std::cout << "📁 No models to delete.\n\n";
        return true;
    }
    
    if (confirm) {
        std::cout << "\n🗑️  Delete All Models\n\n";
        std::cout << "This will delete the following models:\n";
        
        size_t total_size = 0;
        for (const auto& name : downloaded) {
            ModelInfo info = get_model_info(name);
            std::string model_path = get_model_path(name);
            std::string coreml_path = get_coreml_model_path(name);
            
            size_t model_size = 0;
            if (std::filesystem::exists(model_path)) {
                model_size += std::filesystem::file_size(model_path);
            }
            if (coreml_model_exists(name) && std::filesystem::exists(coreml_path)) {
                if (std::filesystem::is_directory(coreml_path)) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(coreml_path)) {
                        if (entry.is_regular_file()) {
                            model_size += entry.file_size();
                        }
                    }
                }
            }
            
            total_size += model_size;
            std::cout << "  ❌ " << name << " - " << (model_size / 1024 / 1024) << " MB\n";
        }
        
        std::cout << "\n📊 Total size: " << (total_size / 1024 / 1024) << " MB\n";
        std::cout << "⚠️  This action cannot be undone.\n";
        std::cout << "Are you sure you want to delete ALL models? [y/N]: ";
        
        std::string response;
        std::getline(std::cin, response);
        
        if (response != "y" && response != "Y") {
            std::cout << "❌ Deletion cancelled.\n\n";
            return false;
        }
    }
    
    bool all_success = true;
    for (const auto& name : downloaded) {
        if (!delete_model(name, false)) {  // Skip individual confirmations
            all_success = false;
        }
    }
    
    if (all_success) {
        std::cout << "\n✅ All models deleted successfully.\n\n";
    } else {
        std::cout << "\n⚠️  Some models could not be deleted.\n\n";
    }
    
    return all_success;
}

void ModelManager::cleanup_orphaned_files() {
    std::cout << "\n🧹 Cleaning up orphaned files...\n\n";
    
    if (!std::filesystem::exists(models_dir_)) {
        std::cout << "📁 Models directory doesn't exist.\n\n";
        return;
    }
    
    std::vector<std::string> orphaned_files;
    std::set<std::string> known_files;
    
    // Collect all known filenames
    for (const auto& [name, info] : models_) {
        known_files.insert(info.filename);
        known_files.insert(info.coreml_filename);
        known_files.insert(info.coreml_filename + ".zip");
    }
    
    // Find orphaned files
    for (const auto& entry : std::filesystem::directory_iterator(models_dir_)) {
        std::string filename = entry.path().filename().string();
        
        if (entry.is_regular_file()) {
            if (known_files.find(filename) == known_files.end()) {
                orphaned_files.push_back(entry.path().string());
            }
        } else if (entry.is_directory()) {
            if (known_files.find(filename) == known_files.end()) {
                orphaned_files.push_back(entry.path().string());
            }
        }
    }
    
    if (orphaned_files.empty()) {
        std::cout << "✅ No orphaned files found.\n\n";
        return;
    }
    
    std::cout << "🗑️  Found orphaned files:\n";
    size_t total_size = 0;
    for (const auto& file_path : orphaned_files) {
        if (std::filesystem::is_regular_file(file_path)) {
            size_t size = std::filesystem::file_size(file_path);
            total_size += size;
            std::cout << "  📄 " << std::filesystem::path(file_path).filename().string() 
                      << " - " << (size / 1024 / 1024) << " MB\n";
        } else if (std::filesystem::is_directory(file_path)) {
            size_t size = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(file_path)) {
                if (entry.is_regular_file()) {
                    size += entry.file_size();
                }
            }
            total_size += size;
            std::cout << "  📁 " << std::filesystem::path(file_path).filename().string() 
                      << "/ - " << (size / 1024 / 1024) << " MB\n";
        }
    }
    
    std::cout << "\n📊 Total size: " << (total_size / 1024 / 1024) << " MB\n";
    std::cout << "Delete these orphaned files? [y/N]: ";
    
    std::string response;
    std::getline(std::cin, response);
    
    if (response != "y" && response != "Y") {
        std::cout << "❌ Cleanup cancelled.\n\n";
        return;
    }
    
    for (const auto& file_path : orphaned_files) {
        try {
            if (std::filesystem::is_directory(file_path)) {
                std::filesystem::remove_all(file_path);
            } else {
                std::filesystem::remove(file_path);
            }
            std::cout << "✅ Deleted: " << std::filesystem::path(file_path).filename().string() << "\n";
        } catch (const std::exception& e) {
            std::cout << "❌ Failed to delete " << file_path << ": " << e.what() << "\n";
        }
    }
    
    std::cout << "\n✅ Cleanup completed.\n\n";
}