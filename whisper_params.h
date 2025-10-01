#pragma once

#include <string>
#include <thread>
#include <algorithm>

// Command-line parameters with CoreML specific options
struct whisper_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t step_ms    = 3000;
    int32_t length_ms  = 10000;
    int32_t keep_ms    = 200;
    int32_t capture_id = -1;
    int32_t max_tokens = 32;
    int32_t audio_ctx  = 0;
    int32_t beam_size  = -1;

    float vad_thold    = 0.6f;
    float freq_thold   = 100.0f;

    bool translate     = false;
    bool no_fallback   = false;
    bool print_special = false;
    bool print_colors  = false;
    bool no_context    = true;
    bool no_timestamps = false;
    bool tinydiarize   = false;
    bool save_audio    = false;
    bool use_coreml    = true;   // Enable CoreML by default on macOS
    bool use_gpu       = true;   // Keep GPU support for fallback
    bool flash_attn    = false;

    std::string language  = "en";
    std::string model     = ""; // Will be auto-resolved by ModelManager
    std::string coreml_model = ""; // Optional CoreML model path
    std::string fname_out;
    std::string output_mode = "original"; // original, english, bilingual
    bool list_models = false; // Flag to list available models
    
    // Model management options
    bool list_downloaded = false;
    bool show_storage = false;
    bool delete_model_flag = false;
    bool delete_all_models_flag = false;
    bool cleanup_models = false;
    std::string model_to_delete = "";
    
    // Auto-copy settings
    bool auto_copy_enabled = false;
    int32_t auto_copy_max_duration_hours = 2; // Default: 2 hours
    int32_t auto_copy_max_size_bytes = 1024 * 1024; // Default: 1MB
    
    // Export settings
    bool export_enabled = false;
    std::string export_format = "txt";
    std::string export_file = "";
    bool export_auto_filename = true;
    bool export_include_metadata = true;
    bool export_include_timestamps = true;
    bool export_include_confidence = false;

    // Meeting settings
    bool meeting_mode = false;
    std::string meeting_prompt = "";
    std::string meeting_name = "";
};