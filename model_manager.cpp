#include "model_manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstdlib>
#include <thread>
#include <chrono>

#ifdef __APPLE__
#include <sys/stat.h>
#include <unistd.h>
#endif

ModelManager::ModelManager() {
    // Use global models directory if installed system-wide
    const char* home = getenv("HOME");
    if (home && std::filesystem::exists("/usr/local/bin/whisper-stream-coreml")) {
        models_dir_ = std::string(home) + "/.whisper-stream-coreml/models";
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
    std::cout << "   ./whisper-stream-coreml -m " << model_name << "\n\n";
    
    std::cout << "🎯 VAD mode (recommended - only transcribes when you speak):\n";
    std::cout << "   ./whisper-stream-coreml -m " << model_name << " --step 0 --length 30000\n\n";
    
    std::cout << "⚡ Continuous mode (transcribes every 500ms):\n";
    std::cout << "   ./whisper-stream-coreml -m " << model_name << " --step 500 --length 5000\n\n";
    
    std::cout << "💾 Save transcription to file:\n";
    std::cout << "   ./whisper-stream-coreml -m " << model_name << " -f transcript.txt\n\n";
    
    std::cout << "🎛️  Use specific microphone:\n";
    std::cout << "   ./whisper-stream-coreml -m " << model_name << " -c 3\n\n";
    
    if (models_[model_name].multilingual) {
        std::cout << "🌍 Transcribe other languages:\n";
        std::cout << "   ./whisper-stream-coreml -m " << model_name << " -l es  # Spanish\n";
        std::cout << "   ./whisper-stream-coreml -m " << model_name << " -l fr  # French\n\n";
        
        std::cout << "🔄 Translate to English:\n";
        std::cout << "   ./whisper-stream-coreml -m " << model_name << " -l es --translate\n\n";
    }
    
    std::cout << "📚 For more options: ./whisper-stream-coreml --help\n\n";
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