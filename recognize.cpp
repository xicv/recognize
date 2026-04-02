// Real-time speech recognition with CoreML support for macOS
// Based on whisper.cpp/examples/stream/stream.cpp with CoreML optimizations

#include "common-sdl.h"
#include "common.h"
#include "common-whisper.h"
#include "whisper.h"
#include "model_manager.h"
#include "config_manager.h"
#include "export_manager.h"
#include "ptt_manager.h"
#include "history_manager.h"
#include "text_processing.h"
#include "meeting_manager.h"
#include "audio_processor.h"
#include "cli_parser.h"
#include "whisper_params.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <future>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#ifdef __APPLE__
#include <pthread.h>
#endif

// Global state for signal handling
std::atomic<bool> g_interrupt_received(false);
std::atomic<bool> g_is_recording(false);

// Signal handler for graceful shutdown — only sets atomic flag (async-signal-safe)
void signal_handler(int signal) {
    if (signal == SIGINT) {
        g_interrupt_received.store(true);
    }
}

// Suppress stderr output during initialization (SDL device listing, whisper model info, etc.)
// Returns saved fd for later restoration, or -1 on failure
static int suppress_stderr() {
    fflush(stderr);
    int saved_fd = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
    return saved_fd;
}

static void restore_stderr(int saved_fd) {
    if (saved_fd >= 0) {
        fflush(stderr);
        dup2(saved_fd, STDERR_FILENO);
        close(saved_fd);
    }
}

// Silent log callback for whisper/ggml during initialization
static void silent_log_callback(enum ggml_log_level /*level*/, const char * /*text*/, void * /*user_data*/) {
    // Intentionally empty — suppress all init chatter
}

// Check for interrupt in main loop and handle confirmation dialog safely
static bool check_interrupt_with_confirmation() {
    if (!g_interrupt_received.load()) return false;

    if (g_is_recording.load()) {
        // If no TTY attached (background process), exit immediately
        if (!isatty(STDIN_FILENO)) {
            return true;
        }

        std::cout << "\n\n Recording in progress! Are you sure you want to quit? (y/N): " << std::flush;

        struct termios old_termios, new_termios;
        tcgetattr(STDIN_FILENO, &old_termios);
        new_termios = old_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

        char c = getchar();

        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);

        if (c == 'y' || c == 'Y') {
            std::cout << "\n Stopping recording and exiting...\n" << std::endl;
            g_is_recording.store(false);  // prevent re-prompting on subsequent checks
            return true;
        } else {
            std::cout << "\n Continuing recording...\n" << std::endl;
            g_interrupt_received.store(false);
            return false;
        }
    }
    // Not recording — exit immediately
    return true;
}

void perform_auto_copy(AutoCopySession& session, const whisper_params& params) {
    if (!should_auto_copy(session, params)) {
        return;
    }
    
    std::string content = session.transcription_buffer.str();
    content = trim_whitespace(content);
    
    if (content.empty()) {
        fprintf(stderr, "Auto-copy skipped: no content to copy.\n");
        return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(now - session.start_time);

    // Check duration limit
    if (duration.count() > params.auto_copy_max_duration_hours) {
        fprintf(stderr, "Auto-copy skipped: session duration (%ld hours) exceeded limit (%d hours).\n",
               duration.count(), params.auto_copy_max_duration_hours);
        return;
    }
    
    // Check size limit
    if (content.size() > static_cast<size_t>(params.auto_copy_max_size_bytes)) {
        fprintf(stderr, "Auto-copy skipped: content size (%zu bytes) exceeded limit (%d bytes).\n",
               content.size(), params.auto_copy_max_size_bytes);
        return;
    }

    // Perform the copy
    if (copy_to_clipboard_macos(content)) {
        fprintf(stderr, "Transcription copied.\n");
        session.has_been_copied = true;
    } else {
        fprintf(stderr, "Auto-copy failed: unable to copy to clipboard.\n");
    }
}

void perform_export(ExportSession& session, const whisper_params& params) {
    if (!params.export_enabled || session.segments.empty()) {
        return;
    }
    
    // Validate export format
    auto supported_formats = ExportManager::get_supported_formats();
    bool format_valid = false;
    for (const auto& format : supported_formats) {
        if (format == params.export_format) {
            format_valid = true;
            break;
        }
    }
    
    if (!format_valid) {
        fprintf(stderr, "Export failed: unsupported format '%s'. Supported formats: ", params.export_format.c_str());
        for (size_t i = 0; i < supported_formats.size(); ++i) {
            fprintf(stderr, "%s", supported_formats[i].c_str());
            if (i < supported_formats.size() - 1) fprintf(stderr, ", ");
        }
        fprintf(stderr, "\n");
        return;
    }
    
    // Setup export manager
    ExportManager export_manager;
    
    // Set export format
    ExportFormat format = ExportManager::extension_to_format("." + params.export_format);
    export_manager.set_format(format);
    
    // Set output file
    if (!params.export_file.empty()) {
        export_manager.set_output_file(params.export_file);
    }
    export_manager.set_auto_filename(params.export_auto_filename);
    
    // Set export options
    export_manager.set_include_metadata(params.export_include_metadata);
    export_manager.set_include_timestamps(params.export_include_timestamps);
    export_manager.set_include_confidence(params.export_include_confidence);
    
    // Add all segments
    for (const auto& segment : session.segments) {
        export_manager.add_segment(segment);
    }
    
    // Set session metadata
    session.metadata.end_time = std::chrono::system_clock::now();
    session.metadata.total_segments = session.segments.size();
    
    if (!session.segments.empty()) {
        int64_t total_duration = session.segments.back().end_time_ms - session.segments.front().start_time_ms;
        session.metadata.total_duration_seconds = total_duration / 1000.0;
    }
    
    export_manager.set_metadata(session.metadata);
    
    // Perform export
    if (export_manager.export_transcription()) {
        fprintf(stderr, "Export completed successfully.\n");
    } else {
        fprintf(stderr, "Export failed.\n");
    }
}

// Finalize session: auto-copy, export, and meeting processing
static void finalize_session(const whisper_params& params,
                             AutoCopySession& auto_copy_session,
                             ExportSession& export_session,
                             SpeakerTracker& speaker_tracker,
                             MeetingSession& meeting_session,
                             const std::string& meeting_output_file,
                             const std::string& transcript_text,
                             std::chrono::high_resolution_clock::time_point session_start) {
    if (params.auto_copy_enabled) {
        perform_auto_copy(auto_copy_session, params);
    }

    if (params.export_enabled) {
        export_session.metadata.total_speakers = speaker_tracker.total_speakers;
        perform_export(export_session, params);
    }

    if (params.meeting_mode) {
        meeting_session.total_speakers = std::max(meeting_session.total_speakers, speaker_tracker.total_speakers);
        double duration_minutes = meeting_session.get_duration_minutes();
        std::cerr << "\nProcessing meeting transcription with Claude CLI..." << std::endl;
        std::cerr << "Duration: " << static_cast<int>(duration_minutes) << " minutes, "
                  << "Speakers detected: " << meeting_session.total_speakers << std::endl;

        bool success = process_meeting_transcription(
            meeting_session.get_transcription(),
            params.meeting_prompt,
            meeting_output_file,
            params.meeting_timeout,
            duration_minutes,
            params.meeting_max_single_pass
        );

        if (!success) {
            std::ofstream raw_file(meeting_output_file);
            if (raw_file.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm tm_buf;
                localtime_r(&time_t, &tm_buf);

                raw_file << "# Meeting Transcription\n\n";
                raw_file << "**Date**: " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M") << "\n";
                raw_file << "**Duration**: " << static_cast<int>(duration_minutes) << " minutes\n";
                raw_file << "**Speakers**: " << meeting_session.total_speakers << "\n";
                raw_file << "**Session ID**: " << meeting_session.session_id << "\n\n";
                raw_file << "---\n\n";
                raw_file << "## Raw Transcription\n\n";
                raw_file << meeting_session.get_transcription();
                raw_file.close();
                std::cerr << "Transcription saved to: " << meeting_output_file << std::endl;
            } else {
                std::cerr << "Failed to save transcription to file" << std::endl;
            }
        }
    }

    // Save to history
    if (params.history_enabled && !transcript_text.empty()) {
        HistoryManager history;
        if (history.open()) {
            auto now = std::chrono::high_resolution_clock::now();
            double duration_s = std::chrono::duration<double>(now - session_start).count();
            std::string mode = params.ptt_mode ? "ptt" :
                               params.meeting_mode ? "meeting" :
                               (params.silence_timeout > 0) ? "auto-stop" : "continuous";
            history.save(transcript_text, duration_s, params.model, mode);
        }
    }
}

int main(int argc, char ** argv) {
    // Handle "history" subcommand before any heavy initialization
    if (argc >= 2 && std::string(argv[1]) == "history") {
        return handle_history_command(argc - 2, argv + 2);
    }

    ggml_backend_load_all();

    // Register signal handler for graceful shutdown
    signal(SIGINT, signal_handler);

    whisper_params params;

    // Load configuration before parsing command line
    ConfigManager config_manager;
    config_manager.load_config();
    config_manager.apply_to_params(params);

    if (whisper_params_parse(argc, argv, params) == false) {
        return 1;
    }

    if (params.silence_timeout < 0.0f) {
        fprintf(stderr, "error: --silence-timeout must be non-negative\n");
        return 1;
    }

    // Validate PTT settings
    if (params.ptt_mode) {
        int ptt_key_code = PushToTalkManager::key_name_to_code(params.ptt_key);
        if (ptt_key_code < 0) {
            fprintf(stderr, "error: unknown PTT key '%s'. Valid keys: space, right_option, right_ctrl, fn, f13\n", params.ptt_key.c_str());
            return 1;
        }
        if (params.meeting_mode) {
            fprintf(stderr, "error: --ptt is incompatible with --meeting (meetings require continuous recording)\n");
            return 1;
        }
        if (params.silence_timeout > 0.0f) {
            fprintf(stderr, "note: --silence-timeout ignored in PTT mode (key release stops recording)\n");
            params.silence_timeout = 0.0f;
        }
        if (params.ptt_pre_roll_ms < 0 || params.ptt_pre_roll_ms > 2000) {
            fprintf(stderr, "error: --ptt-pre-roll must be 0-2000ms\n");
            return 1;
        }
    }

    // Validate refine: check claude CLI is available early
    if (params.refine && !is_claude_cli_available()) {
        fprintf(stderr, "error: --refine requires Claude CLI. Install from: https://claude.ai/code\n");
        return 1;
    }

    // Initialize model manager
    ModelManager model_manager;
    
    // Apply configured models directory if set
    ConfigData effective_config = config_manager.get_effective_config();
    if (effective_config.models_directory) {
        model_manager.set_models_directory(*effective_config.models_directory);
    }

    // Handle model management commands (exit early if matched)
    int cmd_result = handle_model_commands(params, model_manager);
    if (cmd_result >= 0) return cmd_result;

    // Show clean loading state
    const bool stderr_is_tty = isatty(STDERR_FILENO);

    // Resolve model (with auto-download if needed)
    std::string resolved_model = model_manager.resolve_model(params.model, params.use_coreml);
    if (resolved_model.empty()) {
        std::cerr << "\n❌ No model available. Exiting.\n";
        return 1;
    }
    
    // Update params with resolved model path
    params.model = resolved_model;

    // Extract short model name for display (e.g. "ggml-large-v3-turbo.bin" → "large-v3-turbo")
    std::string display_model = std::filesystem::path(resolved_model).stem().string();
    if (display_model.rfind("ggml-", 0) == 0) {
        display_model = display_model.substr(5);
    }
    if (stderr_is_tty) {
        fprintf(stderr, "[Loading %s...]\n", display_model.c_str());
    }
    
    // Auto-set CoreML model path if CoreML is enabled and not explicitly set
    if (params.use_coreml && params.coreml_model.empty()) {
        // Extract model name from the resolved path
        std::filesystem::path model_path(resolved_model);
        std::string model_filename = model_path.filename().string();
        
        // Try to find corresponding model name
        for (const auto& name : model_manager.get_model_names()) {
            if (model_manager.get_model_path(name) == resolved_model) {
                std::string coreml_path = model_manager.get_coreml_model_path(name);
                if (model_manager.coreml_model_exists(name)) {
                    params.coreml_model = coreml_path;
                } else {
                    params.use_coreml = false;  // Disable CoreML to prevent crashes
                }
                break;
            }
        }
    }

    // Adjust thread count based on hardware acceleration
    // When CoreML + Metal are active, encoder runs on ANE and decoder on GPU,
    // so very few CPU threads are needed (just orchestration overhead).
    {
        int default_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
        if (params.use_coreml && params.use_gpu) {
            // CoreML encoder on ANE + Metal decoder: minimal CPU threads
            if (params.n_threads == default_threads) {
                params.n_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
            }
        } else if (!params.use_coreml && params.use_gpu) {
            // Metal only: GPU handles most compute
            if (params.n_threads == default_threads) {
                params.n_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
            }
        } else if (params.n_threads <= 4) {
            // CPU only: use more threads
            params.n_threads = std::min(8, (int32_t)std::thread::hardware_concurrency());
        }
    }

    // Resolve VAD model path (supports "auto" for auto-download)
    if (!params.vad_model_path.empty()) {
        params.vad_model_path = model_manager.resolve_vad_model(params.vad_model_path);
    }

    // Meeting mode: apply optimized defaults (can still be overridden by explicit CLI args)
    // These are set after CLI parsing so they only apply if the user didn't explicitly set them
    if (params.meeting_mode) {
        // Auto-enable speaker turn detection for meetings
        if (!params.tinydiarize) params.tinydiarize = true;
        // Better accuracy defaults for meeting transcription
        if (params.keep_ms == 200) params.keep_ms = 1000;      // Research: 1000ms optimal overlap
        if (params.step_ms == 3000) params.step_ms = 5000;     // Longer processing windows
        if (params.length_ms == 10000) params.length_ms = 15000;
        if (params.beam_size <= 0) params.beam_size = 5;       // Beam search for meetings
        if (params.freq_thold == 100.0f) params.freq_thold = 200.0f; // Better high-pass filter
        if (params.initial_prompt.empty()) {
            params.initial_prompt = "Meeting transcription with proper punctuation and capitalization.";
        }
    }

    // PTT keyterm boosting: bias Whisper's decoder toward coding vocabulary.
    // Inspired by Claude Code's Deepgram keyterm boosting — Whisper's initial_prompt
    // conditions the decoder to favor these tokens, reducing misrecognition of
    // technical terms (e.g., "MCP" not "NCP", "CLI" not "see a lie").
    if (params.ptt_mode && params.initial_prompt.empty()) {
        params.initial_prompt =
            "CLI, API, SDK, MCP, OAuth, gRPC, WebSocket, TypeScript, JavaScript, "
            "React, Node.js, npm, pnpm, GitHub, Git, SSH, HTTPS, REST, GraphQL, "
            "JSON, YAML, TOML, regex, grep, curl, wget, Docker, Kubernetes, "
            "localhost, stderr, stdout, stdin, async, await, const, enum, struct, "
            "Rust, Python, Go, Swift, Homebrew, CoreML, whisper, Claude, Anthropic, "
            "Makefile, CMake, Dockerfile, symlink, sudo, chmod, Vim, Neovim, "
            "refactor, deploy, commit, merge, rebase, upstream, downstream, "
            "latency, throughput, endpoint, middleware, webhook, microservice.";
    }

    params.keep_ms   = std::min(params.keep_ms,   params.step_ms);
    params.length_ms = std::max(params.length_ms, params.step_ms);

    const int n_samples_step = (1e-3*params.step_ms  )*WHISPER_SAMPLE_RATE;
    const int n_samples_len  = (1e-3*params.length_ms)*WHISPER_SAMPLE_RATE;
    const int n_samples_keep = (1e-3*params.keep_ms  )*WHISPER_SAMPLE_RATE;
    const int n_samples_30s  = (1e-3*30000.0         )*WHISPER_SAMPLE_RATE;

    const bool use_vad = n_samples_step <= 0; // sliding window mode uses VAD

    const int n_new_line = !use_vad ? std::max(1, params.length_ms / params.step_ms - 1) : 1;

    params.no_timestamps  = !use_vad;
    // In meeting mode, keep context between chunks for better accuracy
    if (!params.meeting_mode) {
        params.no_context |= use_vad;
    }
    if (use_vad) params.max_tokens = 0;

    // Whisper init with CoreML support — configure before parallel init
    if (params.language != "auto" && whisper_lang_id(params.language.c_str()) == -1){
        fprintf(stderr, "error: unknown language '%s'\n", params.language.c_str());
        whisper_print_usage(argc, argv, params);
        exit(0);
    }

    struct whisper_context_params cparams = whisper_context_default_params();

    #ifdef WHISPER_COREML
    if (params.use_coreml && params.coreml_no_ane) {
        setenv("WHISPER_COREML_NO_ANE", "1", 1);
    }
    cparams.use_gpu = params.use_gpu;
    #else
    cparams.use_gpu = params.use_gpu;
    if (params.use_coreml) {
        fprintf(stderr, "warning: CoreML requested but not compiled with CoreML support\n");
    }
    #endif

    cparams.flash_attn = params.flash_attn;

    // Suppress whisper/ggml verbose logging during model load
    whisper_log_set(silent_log_callback, nullptr);
    ggml_log_set(silent_log_callback, nullptr);

    auto t_init_start = std::chrono::high_resolution_clock::now();

    // Parallel init: load model and init audio simultaneously.
    // Model load is I/O-bound (1.5GB mmap), audio init is device-bound —
    // running them in parallel saves 0.3-0.5s on cold start.
    int audio_buffer_ms = params.length_ms;
    if (params.ptt_mode) {
        audio_buffer_ms = std::max(audio_buffer_ms, 600000 + params.ptt_pre_roll_ms);
    }

    std::atomic<bool> audio_ok{false};
    audio_async audio(audio_buffer_ms);
    auto audio_future = std::async(std::launch::async, [&]() {
        int saved = suppress_stderr();
        audio_ok.store(audio.init(params.capture_id, WHISPER_SAMPLE_RATE));
        restore_stderr(saved);
    });

    struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);
    if (ctx == nullptr) {
        whisper_log_set(nullptr, nullptr);
        ggml_log_set(nullptr, nullptr);
        fprintf(stderr, "error: failed to initialize whisper context\n");
        return 2;
    }

    // Wait for audio init to complete
    audio_future.wait();
    if (!audio_ok.load()) {
        fprintf(stderr, "%s: audio.init() failed!\n", __func__);
        whisper_free(ctx);
        return 1;
    }

    audio.resume();

    if (stderr_is_tty) {
        auto t_model_loaded = std::chrono::high_resolution_clock::now();
        auto model_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_model_loaded - t_init_start).count();
        fprintf(stderr, "[Ready in %.1fs]\n", model_ms / 1000.0);
    }

    // Request P-core scheduling for the inference thread
    #ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    #endif

    // Set recording state for signal handler
    g_is_recording.store(true);

    // CoreML warm-up: first inference triggers ANE compilation
    // For large models this can take 30s+ on first run (ANE caches for subsequent runs)
    // Skip when coreml_no_ane — CPU+GPU mode has no ANE compilation overhead
    #ifdef WHISPER_COREML
    if (params.use_coreml && !params.coreml_no_ane) {
        if (stderr_is_tty) {
            fprintf(stderr, "[Warming up CoreML (first run may take a while)...]\n");
            fflush(stderr);
        }
        auto t_warmup_start = std::chrono::high_resolution_clock::now();

        std::vector<float> warmup(WHISPER_SAMPLE_RATE * 1, 0.0f); // 1 second of silence
        whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wp.print_realtime   = false;
        wp.print_progress   = false;
        wp.print_timestamps = false;
        wp.print_special    = false;
        wp.n_threads        = params.n_threads;
        wp.single_segment   = true;
        wp.max_tokens       = 1;
        whisper_full(ctx, wp, warmup.data(), warmup.size());

        if (stderr_is_tty) {
            auto t_warmup_end = std::chrono::high_resolution_clock::now();
            auto warmup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_warmup_end - t_warmup_start).count();
            fprintf(stderr, "[CoreML ready in %.1fs]\n", warmup_ms / 1000.0);
        }
    }
    #endif

    // Keep whisper/ggml logging suppressed for clean UX
    // (Metal kernel JIT compilation can log during first real inference)

    // Validate output mode
    if (params.output_mode != "original" && params.output_mode != "english" && params.output_mode != "bilingual") {
        fprintf(stderr, "error: invalid output mode '%s'. Valid modes: original, english, bilingual\n", params.output_mode.c_str());
        whisper_free(ctx);
        return 1;
    }
    
    // Check compatibility between translate flag and output mode
    if (params.translate) {
        // If --translate is used, default to "english" mode for compatibility
        if (params.output_mode == "original") {
            params.output_mode = "english";
            fprintf(stderr, "%s: --translate flag detected, switching to 'english' output mode\n", __func__);
        }
    }
    
    // Check if translation is supported for non-original modes
    bool needs_translation = (params.output_mode == "english" || params.output_mode == "bilingual");
    if (needs_translation && !whisper_is_multilingual(ctx)) {
        fprintf(stderr, "error: output mode '%s' requires a multilingual model, but current model is English-only\n", params.output_mode.c_str());
        whisper_free(ctx);
        return 1;
    }
    
    // For bilingual mode, we need a second context for translation
    struct whisper_context * ctx_translate = nullptr;
    if (params.output_mode == "bilingual") {
        ctx_translate = whisper_init_from_file_with_params(params.model.c_str(), cparams);
        if (ctx_translate == nullptr) {
            fprintf(stderr, "error: failed to initialize translation context for bilingual mode\n");
            whisper_free(ctx);
            return 2;
        }
    }

    std::vector<float> pcmf32    (n_samples_30s, 0.0f);
    std::vector<float> pcmf32_old;
    pcmf32_old.reserve(n_samples_30s);
    std::vector<float> pcmf32_new(n_samples_30s, 0.0f);

    std::vector<whisper_token> prompt_tokens;

    // Auto-correct language settings for non-multilingual models
    if (!whisper_is_multilingual(ctx)) {
        if (params.language != "en" || params.translate) {
            params.language = "en";
            params.translate = false;
        }
    }

    int n_iter = 0;
    bool is_running = true;

    const bool stdout_is_tty = isatty(STDOUT_FILENO);
    // When stdout is not a TTY (redirected to file/pipe), accumulate text
    // and dump it on exit instead of streaming with ANSI codes
    std::string pipe_finalized_text;
    std::ostringstream pipe_current_text;

    std::ofstream fout;
    if (params.fname_out.length() > 0) {
        fout.open(params.fname_out);
        if (!fout.is_open()) {
            fprintf(stderr, "%s: failed to open output file '%s'!\n", __func__, params.fname_out.c_str());
            whisper_free(ctx);
            if (ctx_translate) whisper_free(ctx_translate);
            return 1;
        }
    }

    wav_writer wavWriter;
    if (params.save_audio) {
        time_t now = time(0);
        char buffer[80];
        std::tm tm_buf;
        localtime_r(&now, &tm_buf);
        strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &tm_buf);
        std::string filename = std::string(buffer) + ".wav";
        wavWriter.open(filename, WHISPER_SAMPLE_RATE, 16, 1);
    }
    
    // Initialize auto-copy session
    AutoCopySession auto_copy_session;

    // Initialize export session
    ExportSession export_session;
    if (params.export_enabled) {
        // Setup session metadata
        export_session.metadata.session_id = export_session.session_id;
        export_session.metadata.start_time = std::chrono::system_clock::now();
        export_session.metadata.model_name = params.model;
        export_session.metadata.language = params.language;
        export_session.metadata.coreml_enabled = params.use_coreml;
        export_session.metadata.thread_count = params.n_threads;
        export_session.metadata.vad_threshold = params.vad_thold;
        export_session.metadata.step_ms = params.step_ms;
        export_session.metadata.length_ms = params.length_ms;
#ifdef RECOGNIZE_VERSION
        export_session.metadata.version = "recognize-" RECOGNIZE_VERSION;
#else
        export_session.metadata.version = "recognize-dev";
#endif
    }

    // Initialize speaker tracker and meeting session
    SpeakerTracker speaker_tracker;
    MeetingSession meeting_session;
    std::string meeting_output_file;
    if (params.meeting_mode) {
        meeting_output_file = generate_meeting_filename(params.meeting_name);
    }

    // ─── Push-to-Talk mode ───────────────────────────────────────────────
    if (params.ptt_mode) {
        // For English input, force transcription mode instead of translation.
        // Whisper's translate mode with language="en" is untested/unsupported —
        // the model was trained for X→English, not English→English. Using
        // translate=true with English audio produces empty segments and degraded
        // quality (whisper.cpp issues #2065, #2678, #3278).
        if (params.language == "en" && params.output_mode == "english") {
            params.output_mode = "original";
        }

        PushToTalkManager ptt;
        int ptt_key_code = PushToTalkManager::key_name_to_code(params.ptt_key);
        if (!ptt.start(ptt_key_code)) {
            fprintf(stderr, "Failed to start PTT. Check Input Monitoring permissions.\n");
            whisper_free(ctx);
            if (ctx_translate) whisper_free(ctx_translate);
            return 7;
        }

        // Signal readiness — PTT_READY on stderr for script detection (keeps stdout clean for transcript)
        fprintf(stderr, "PTT_READY\n");
        if (stderr_is_tty) {
            fprintf(stderr, "[Ready — hold %s to record, release to transcribe]\n",
                    params.ptt_key.c_str());
        }
        fflush(stderr);

        bool is_running_ptt = true;
        std::string ptt_pipe_text;
        const bool stderr_tty = isatty(STDERR_FILENO);

        // PTT loop — in single-shot mode, exits after first transcription.
        // In daemon mode (--ptt-loop), loops back to wait for next key press
        // with a 10-minute inactivity timeout to prevent orphaned daemons.
        auto t_last_activity = std::chrono::high_resolution_clock::now();
        const int daemon_timeout_ms = 600000; // 10 minutes

        while (is_running_ptt && !g_interrupt_received.load()) {
            // Wait for key press (with inactivity timeout in daemon mode)
            while (!ptt.is_key_held() && is_running_ptt && !g_interrupt_received.load()) {
                is_running_ptt = sdl_poll_events();

                if (params.ptt_loop) {
                    auto now = std::chrono::high_resolution_clock::now();
                    int idle_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - t_last_activity).count());
                    if (idle_ms > daemon_timeout_ms) {
                        fprintf(stderr, "PTT daemon idle timeout (10min), exiting\n");
                        is_running_ptt = false;
                        break;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!is_running_ptt || g_interrupt_received.load()) break;

            // Key pressed — start capture
            auto t_press = std::chrono::high_resolution_clock::now();
            auto t_last_level = t_press;
            if (stderr_tty) fprintf(stderr, "\r[Recording...] ");

            // Capture while key is held, with non-blocking async preview.
            // Preview inference runs on std::async so key-release is detected
            // immediately — no more multi-second jank from blocking inference.
            auto t_last_preview = t_press;
            std::string last_preview_text;
            std::future<std::string> preview_future;
            bool preview_running = false;

            while (is_running_ptt && !g_interrupt_received.load()) {
                is_running_ptt = sdl_poll_events();
                bool key_held = ptt.is_key_held();

                auto now = std::chrono::high_resolution_clock::now();
                int elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - t_press).count());

                // Check if async preview completed
                if (preview_running && preview_future.valid() &&
                    preview_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    preview_running = false;
                    std::string preview_text = preview_future.get();

                    if (!preview_text.empty() && preview_text != last_preview_text) {
                        std::string display = preview_text;
                        if (display.size() > 80) {
                            display = "..." + display.substr(display.size() - 77);
                        }
                        if (stderr_tty) {
                            fprintf(stderr, "\r\033[2K[%ds]%s", elapsed_ms / 1000, display.c_str());
                        }
                        fprintf(stderr, "\n[PREVIEW %ds]%s\n", elapsed_ms / 1000, display.c_str());
                        fflush(stderr);
                        last_preview_text = preview_text;
                    }
                    if (stderr_tty && key_held) fprintf(stderr, "\r[Recording...] ");
                }

                // Key released — break out (wait for in-flight preview first)
                if (!key_held) {
                    if (preview_running && preview_future.valid()) {
                        if (stderr_tty) fprintf(stderr, "\r\033[2K[Finishing preview...]");
                        preview_future.wait();
                        preview_running = false;
                    }
                    break;
                }

                // Launch new async preview every 2s, starting after 1.5s of recording.
                // Uses last 15s of audio (not all) for bounded inference time.
                int since_preview = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last_preview).count());
                if (!preview_running && elapsed_ms >= 1500 && since_preview >= 2000) {
                    t_last_preview = now;

                    // Cap preview window at 15s — keeps inference fast (~1-2s)
                    int preview_ms = std::min(elapsed_ms + params.ptt_pre_roll_ms, 15000);
                    std::vector<float> preview_audio;
                    audio.get(preview_ms, preview_audio);

                    if (params.normalize_audio) normalize_audio(preview_audio);

                    // Capture parameters by value for the async task.
                    // ctx is safe: only one whisper_full() runs at a time because
                    // we don't launch a new preview while one is in-flight.
                    whisper_params pp = params;
                    pp.beam_size = 1;
                    pp.no_fallback = true;
                    pp.no_context = true;
                    pp.max_tokens = 128;

                    preview_future = std::async(std::launch::async,
                        [ctx, pp, preview_audio = std::move(preview_audio), &prompt_tokens]() -> std::string {
                            std::vector<BilingualSegment> results;
                            if (process_audio_segment(ctx, nullptr, pp, preview_audio,
                                                      results, prompt_tokens) != 0) {
                                return "";
                            }
                            std::string text;
                            for (const auto& seg : results) {
                                if (!seg.original_text.empty()) text += seg.original_text;
                                else if (!seg.english_text.empty()) text += seg.english_text;
                            }
                            return filter_hallucinations(text);
                        });
                    preview_running = true;
                }

                // Audio level visualization: update every 150ms on TTY
                int since_level = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last_level).count());
                if (stderr_tty && !preview_running && since_level >= 150) {
                    t_last_level = now;
                    // Compute RMS of last 100ms of audio
                    std::vector<float> level_audio;
                    audio.get(100, level_audio);
                    if (!level_audio.empty()) {
                        float sum_sq = 0.0f;
                        for (const float s : level_audio) sum_sq += s * s;
                        float rms = std::sqrt(sum_sq / level_audio.size());
                        // Normalize to 0-1 via sqrt curve for perceptual evenness
                        float level = std::sqrt(std::min(rms / 0.05f, 1.0f));
                        // Map to 8-bar display using block elements
                        static const char* bars[] = {"▁","▂","▃","▄","▅","▆","▇","█"};
                        int bar_idx = static_cast<int>(level * 7.99f);
                        bar_idx = std::max(0, std::min(7, bar_idx));
                        fprintf(stderr, "\r\033[2K[Recording %ds %s%s%s] ",
                                elapsed_ms / 1000, bars[std::max(0, bar_idx-1)], bars[bar_idx], bars[std::max(0, bar_idx-1)]);
                        fflush(stderr);
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!is_running_ptt || g_interrupt_received.load()) break;

            // Key released — get audio and transcribe
            auto t_release = std::chrono::high_resolution_clock::now();
            int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                t_release - t_press).count());

            if (duration_ms < 200) {
                // Too short, likely accidental tap — wait for another press
                if (stderr_tty) fprintf(stderr, "\r[Too short, skipped]          \n");
                continue;
            }

            // Include pre-roll + lead-in as real audio from the circular buffer.
            // Using real ambient audio instead of synthetic zeros avoids whisper's
            // no-speech detection (trained on trailing silence, not leading zeros).
            const int lead_in_ms = 200;
            int total_ms = std::min(duration_ms + params.ptt_pre_roll_ms + lead_in_ms, audio_buffer_ms);

            if (stderr_tty && duration_ms + params.ptt_pre_roll_ms + lead_in_ms > audio_buffer_ms) {
                fprintf(stderr, "\r[Warning: held %.0fs but buffer holds %.0fs — beginning truncated]\n",
                        duration_ms / 1000.0, audio_buffer_ms / 1000.0);
            }

            std::vector<float> pcmf32_ptt;
            audio.get(total_ms, pcmf32_ptt);

            // Trim trailing silence to reduce inference time (RMS energy check)
            {
                const int samples_per_100ms = WHISPER_SAMPLE_RATE / 10;
                const int min_samples = WHISPER_SAMPLE_RATE / 2;  // keep at least 500ms
                const float silence_rms_threshold = 0.01f;
                while (static_cast<int>(pcmf32_ptt.size()) > min_samples + samples_per_100ms) {
                    float sum_sq = 0.0f;
                    for (size_t i = pcmf32_ptt.size() - samples_per_100ms; i < pcmf32_ptt.size(); ++i) {
                        sum_sq += pcmf32_ptt[i] * pcmf32_ptt[i];
                    }
                    float rms = std::sqrt(sum_sq / samples_per_100ms);
                    if (rms < silence_rms_threshold) {
                        pcmf32_ptt.resize(pcmf32_ptt.size() - samples_per_100ms);
                    } else {
                        break;
                    }
                }
            }

            if (params.normalize_audio) {
                normalize_audio(pcmf32_ptt);
            }

            float actual_duration_s = pcmf32_ptt.size() / static_cast<float>(WHISPER_SAMPLE_RATE);
            if (stderr_tty) fprintf(stderr, "\r[Transcribing %.1fs...]        ", actual_duration_s);

            // PTT-optimized inference: beam search for quality, no temperature
            // fallback (causes issues at chunk boundaries).
            whisper_params ptt_params = params;
            if (ptt_params.beam_size <= 0) ptt_params.beam_size = 5;
            ptt_params.no_fallback = true;
            ptt_params.max_tokens = 256;
            // PTT guarantees speech (user holds a button), so relax no-speech
            // detection to prevent whisper from skipping onset segments.
            ptt_params.no_speech_thold = 0.9f;

            // Process audio in ≤28-second chunks (whisper's 30s window minus margin).
            // Larger chunks = fewer boundary artifacts and better context.
            // Cross-chunk context is carried via prompt tokens from previous chunk
            // output, giving whisper decoder continuity across boundaries.
            std::vector<BilingualSegment> bilingual_results;
            const int chunk_samples = WHISPER_SAMPLE_RATE * 28;
            const int overlap_samples = WHISPER_SAMPLE_RATE / 2;  // 500ms overlap for continuity
            bool inference_failed = false;
            std::vector<whisper_token> ptt_prompt_tokens;

            for (size_t offset = 0; offset < pcmf32_ptt.size(); offset += chunk_samples) {
                size_t remaining = pcmf32_ptt.size() - offset;
                size_t chunk_size = std::min(remaining, static_cast<size_t>(chunk_samples));

                // Avoid a tiny trailing chunk (< 3s) — merge with previous
                if (remaining > static_cast<size_t>(chunk_samples) &&
                    remaining < static_cast<size_t>(chunk_samples + WHISPER_SAMPLE_RATE * 3)) {
                    chunk_size = remaining;
                }

                std::vector<float> chunk;
                if (offset == 0) {
                    chunk.assign(pcmf32_ptt.begin(),
                                 pcmf32_ptt.begin() + chunk_size);
                } else {
                    // Subsequent chunks: 500ms overlap from previous chunk's tail
                    size_t overlap = std::min(static_cast<size_t>(overlap_samples), offset);
                    chunk.resize(overlap + chunk_size);
                    std::copy(pcmf32_ptt.begin() + offset - overlap,
                              pcmf32_ptt.begin() + offset + chunk_size,
                              chunk.begin());
                }

                if (stderr_tty && offset > 0) {
                    fprintf(stderr, "\r[Transcribing %.1fs... chunk %zu/%zu]        ",
                            actual_duration_s, offset / chunk_samples + 1,
                            (pcmf32_ptt.size() + chunk_samples - 1) / chunk_samples);
                }

                // Carry context from previous chunk via prompt tokens
                // (first chunk uses initial_prompt, subsequent use decoder output)
                ptt_params.no_context = (offset == 0);

                std::vector<BilingualSegment> chunk_results;
                if (process_audio_segment(ctx, ctx_translate, ptt_params, chunk,
                                          chunk_results, ptt_prompt_tokens) != 0) {
                    fprintf(stderr, "\nfailed to process audio chunk\n");
                    inference_failed = true;
                    break;
                }

                // Extract prompt tokens from this chunk for next chunk's context
                int n_segments = whisper_full_n_segments(ctx);
                if (n_segments > 0) {
                    int last_seg = n_segments - 1;
                    int n_tokens = whisper_full_n_tokens(ctx, last_seg);
                    ptt_prompt_tokens.clear();
                    for (int t = 0; t < n_tokens; ++t) {
                        ptt_prompt_tokens.push_back(whisper_full_get_token_id(ctx, last_seg, t));
                    }
                }

                bilingual_results.insert(bilingual_results.end(),
                                         chunk_results.begin(), chunk_results.end());
            }
            if (inference_failed) break;

            // Apply hallucination filter
            for (auto& seg : bilingual_results) {
                if (!seg.original_text.empty())
                    seg.original_text = filter_hallucinations(seg.original_text);
                if (!seg.english_text.empty())
                    seg.english_text = filter_hallucinations(seg.english_text);
            }
            bilingual_results.erase(
                std::remove_if(bilingual_results.begin(), bilingual_results.end(),
                    [](const BilingualSegment& s) {
                        return s.original_text.empty() && s.english_text.empty();
                    }),
                bilingual_results.end());

            // Silent-drop detection: audio had signal but transcript is empty.
            // Retry once with relaxed no_speech_thold (inspired by Claude Code's
            // silent-drop recovery that replays audio on a fresh connection).
            if (bilingual_results.empty() && !pcmf32_ptt.empty()) {
                float audio_rms = 0.0f;
                for (const float s : pcmf32_ptt) audio_rms += s * s;
                audio_rms = std::sqrt(audio_rms / pcmf32_ptt.size());

                if (audio_rms > 0.005f) {
                    // Audio had signal but whisper returned nothing — retry with relaxed params
                    if (stderr_tty) fprintf(stderr, "\r[Silent drop detected, retrying...]       ");

                    whisper_params retry_params = ptt_params;
                    retry_params.no_speech_thold = 0.99f;   // Almost disable no-speech filter
                    retry_params.entropy_thold = 3.0f;      // More permissive entropy
                    retry_params.max_tokens = 512;           // Allow more output

                    for (size_t offset = 0; offset < pcmf32_ptt.size(); offset += chunk_samples) {
                        size_t remaining = pcmf32_ptt.size() - offset;
                        size_t chunk_size = std::min(remaining, static_cast<size_t>(chunk_samples));
                        if (remaining > static_cast<size_t>(chunk_samples) &&
                            remaining < static_cast<size_t>(chunk_samples + WHISPER_SAMPLE_RATE * 2)) {
                            chunk_size = remaining;
                        }
                        std::vector<float> chunk(pcmf32_ptt.begin() + offset,
                                                  pcmf32_ptt.begin() + offset + chunk_size);
                        std::vector<BilingualSegment> retry_results;
                        if (process_audio_segment(ctx, ctx_translate, retry_params, chunk,
                                                  retry_results, prompt_tokens) == 0) {
                            for (auto& seg : retry_results) {
                                if (!seg.original_text.empty())
                                    seg.original_text = filter_hallucinations(seg.original_text);
                                if (!seg.english_text.empty())
                                    seg.english_text = filter_hallucinations(seg.english_text);
                            }
                            retry_results.erase(
                                std::remove_if(retry_results.begin(), retry_results.end(),
                                    [](const BilingualSegment& s) {
                                        return s.original_text.empty() && s.english_text.empty();
                                    }),
                                retry_results.end());
                            bilingual_results.insert(bilingual_results.end(),
                                                     retry_results.begin(), retry_results.end());
                        }
                    }
                    if (bilingual_results.empty() && stderr_tty) {
                        fprintf(stderr, "\r[Warning: speech detected but transcription empty]\n");
                    }
                }
            }

            // Refine via Claude if enabled
            if (params.refine && !bilingual_results.empty()) {
                // Concatenate raw text for refinement
                std::string raw_text;
                for (const auto& seg : bilingual_results) {
                    if (!seg.original_text.empty()) raw_text += seg.original_text;
                    else if (!seg.english_text.empty()) raw_text += seg.english_text;
                }
                if (!raw_text.empty()) {
                    std::string refined = refine_transcription(raw_text);
                    // Replace all segments with a single refined segment
                    BilingualSegment refined_seg = bilingual_results[0];
                    if (!refined_seg.original_text.empty()) {
                        refined_seg.original_text = " " + refined;
                    } else {
                        refined_seg.english_text = " " + refined;
                    }
                    refined_seg.speaker_turn = false;
                    bilingual_results.clear();
                    bilingual_results.push_back(refined_seg);
                }
            }

            // Display results
            if (stderr_tty) fprintf(stderr, "\r                              \r");

            if (stdout_is_tty) {
                printf("\n");
            }

            std::ostringstream ptt_pipe_buf;
            std::ostringstream* pbuf = stdout_is_tty ? nullptr : &ptt_pipe_buf;
            print_bilingual_results(bilingual_results, params, auto_copy_session, export_session,
                                    speaker_tracker, nullptr, stdout_is_tty, pbuf);

            if (!stdout_is_tty) {
                ptt_pipe_text += ptt_pipe_buf.str();
            }

            if (stdout_is_tty) {
                printf("\n");
                fflush(stdout);
            }

            if (params.ptt_loop) {
                // Loop mode: dump transcript immediately, save history, signal done, continue
                if (!stdout_is_tty && !ptt_pipe_text.empty()) {
                    // Truncate and rewind stdout (redirected to file by launcher)
                    // so each iteration overwrites rather than appending.
                    // Uses raw fd ops after flushing stdio buffers — safe because
                    // stdio has no cached position after fflush.
                    fflush(stdout);
                    if (lseek(STDOUT_FILENO, 0, SEEK_SET) != -1) {
                        if (ftruncate(STDOUT_FILENO, 0) != 0) {
                            fprintf(stderr, "warning: ftruncate failed, output may be concatenated\n");
                        }
                    }
                    printf("%s\n", ptt_pipe_text.c_str());
                    fflush(stdout);
                }

                // Save to history per-iteration
                std::string iter_text = stdout_is_tty
                    ? auto_copy_session.transcription_buffer.str()
                    : ptt_pipe_text;
                if (params.history_enabled && !iter_text.empty()) {
                    HistoryManager history;
                    if (history.open()) {
                        auto now = std::chrono::high_resolution_clock::now();
                        double duration_s = std::chrono::duration<double>(now - t_press).count();
                        history.save(iter_text, duration_s, params.model, "ptt");
                    }
                }

                // Clear state for next iteration
                ptt_pipe_text.clear();
                auto_copy_session.transcription_buffer.str("");
                auto_copy_session.transcription_buffer.clear();
                export_session.segments.clear();
                speaker_tracker.current_id = 0;
                speaker_tracker.total_speakers = 0;
                t_last_activity = std::chrono::high_resolution_clock::now();

                // Signal completion via file + stderr for launcher detection.
                // File signal is checked first (fast path, no log grep needed).
                {
                    std::string done_path = std::string(getenv("HOME") ? getenv("HOME") : "") +
                                            "/.recognize/claude-session.done";
                    if (!done_path.empty()) {
                        std::ofstream done_file(done_path);
                        if (done_file.is_open()) done_file << "1\n";
                    }
                }
                fprintf(stderr, "TRANSCRIPT_DONE\n");
                fprintf(stderr, "PTT_WAITING\n");
                if (stderr_tty) {
                    fprintf(stderr, "[Ready — hold %s to record, release to transcribe]\n",
                            params.ptt_key.c_str());
                }
                fflush(stderr);
                continue;
            }

            // Single-shot: exit after first successful transcription
            break;
        }

        ptt.stop();

        // Dump accumulated text in pipe mode (single-shot only; loop mode dumps per-iteration)
        if (!params.ptt_loop && !stdout_is_tty && !ptt_pipe_text.empty()) {
            printf("%s\n", ptt_pipe_text.c_str());
            fflush(stdout);
        }

        audio.pause();

        // Gather final transcript text for history (single-shot only; loop mode saves per-iteration)
        if (!params.ptt_loop) {
            std::string history_text;
            if (params.meeting_mode) {
                history_text = meeting_session.get_transcription();
            } else if (!stdout_is_tty) {
                history_text = ptt_pipe_text;
            } else {
                history_text = auto_copy_session.transcription_buffer.str();
            }

            finalize_session(params, auto_copy_session, export_session, speaker_tracker,
                             meeting_session, meeting_output_file, history_text, auto_copy_session.start_time);
        }

        g_is_recording.store(false);
        whisper_free(ctx);
        if (ctx_translate) whisper_free(ctx_translate);
        return 0;
    }

    // ─── Standard (non-PTT) mode ────────────────────────────────────────

    // Clean ready state for standard mode
    if (stderr_is_tty) {
        fprintf(stderr, "[Start speaking]\n");
        fflush(stderr);
    }

    auto t_last  = std::chrono::high_resolution_clock::now();
    const auto t_start = t_last;

    // Silence timeout state
    auto t_last_speech = std::chrono::high_resolution_clock::now();
    bool has_spoken = false;
    const bool silence_timeout_enabled = params.silence_timeout > 0.0f && !params.meeting_mode;

    // Main audio processing loop
    while (is_running && !check_interrupt_with_confirmation()) {
        if (params.save_audio) {
            wavWriter.write(pcmf32_new.data(), pcmf32_new.size());
        }

        is_running = sdl_poll_events();
        if (!is_running || check_interrupt_with_confirmation()) {
            break;
        }

        // Process new audio
        if (!use_vad) {
            while (true) {
                is_running = sdl_poll_events();
                if (!is_running || check_interrupt_with_confirmation()) {
                    break;
                }
                
                audio.get(params.step_ms, pcmf32_new);

                if ((int) pcmf32_new.size() > 2*n_samples_step) {
                    fprintf(stderr, "\n\n%s: WARNING: cannot process audio fast enough, dropping audio ...\n\n", __func__);
                    audio.clear();
                    continue;
                }

                if ((int) pcmf32_new.size() >= n_samples_step) {
                    audio.clear();
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            // Silence timeout: check new audio for speech before inference
            // Use a copy because vad_simple() applies high_pass_filter() in-place
            if (silence_timeout_enabled) {
                std::vector<float> pcmf32_vad(pcmf32_new);
                bool new_audio_has_speech = ::vad_simple(pcmf32_vad, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, false);
                if (new_audio_has_speech) {
                    has_spoken = true;
                    t_last_speech = std::chrono::high_resolution_clock::now();
                } else if (has_spoken) {
                    const auto t_silence = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - t_last_speech).count();
                    if (t_silence >= static_cast<int64_t>(params.silence_timeout * 1000.0f)) {
                        fprintf(stderr, "[Silence timeout: %.1fs, auto-stopping]\n", params.silence_timeout);
                        is_running = false;
                        break;
                    }
                }
            }

            const int n_samples_new = pcmf32_new.size();
            const int n_samples_take = std::min((int) pcmf32_old.size(), std::max(0, n_samples_keep + n_samples_len - n_samples_new));

            pcmf32.resize(n_samples_new + n_samples_take);

            for (int i = 0; i < n_samples_take; i++) {
                pcmf32[i] = pcmf32_old[pcmf32_old.size() - n_samples_take + i];
            }

            memcpy(pcmf32.data() + n_samples_take, pcmf32_new.data(), n_samples_new*sizeof(float));
            pcmf32_old = pcmf32;
        } else {
            const auto t_now  = std::chrono::high_resolution_clock::now();
            const auto t_diff = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_last).count();

            if (t_diff < 2000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            audio.get(2000, pcmf32_new);

            if (::vad_simple(pcmf32_new, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, false)) {
                audio.get(params.length_ms, pcmf32);
                if (silence_timeout_enabled) {
                    has_spoken = true;
                    t_last_speech = std::chrono::high_resolution_clock::now();
                }
            } else {
                if (silence_timeout_enabled && has_spoken) {
                    const auto t_silence = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - t_last_speech).count();
                    if (t_silence >= static_cast<int64_t>(params.silence_timeout * 1000.0f)) {
                        fprintf(stderr, "[Silence timeout: %.1fs, auto-stopping]\n", params.silence_timeout);
                        is_running = false;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            t_last = t_now;
        }

        // Apply audio normalization before inference
        if (params.normalize_audio) {
            normalize_audio(pcmf32);
        }

        // Run inference via bilingual processing pipeline
        {
            // Note: whisper_full_params are configured inside process_audio_segment()
            std::vector<BilingualSegment> bilingual_results;
            if (process_audio_segment(ctx, ctx_translate, params, pcmf32, bilingual_results, prompt_tokens) != 0) {
                fprintf(stderr, "%s: failed to process audio\n", argv[0]);
                whisper_free(ctx);
                if (ctx_translate) whisper_free(ctx_translate);
                return 6;
            }

            // Apply hallucination filter
            for (auto& seg : bilingual_results) {
                if (!seg.original_text.empty()) {
                    seg.original_text = filter_hallucinations(seg.original_text);
                }
                if (!seg.english_text.empty()) {
                    seg.english_text = filter_hallucinations(seg.english_text);
                }
            }
            // Remove segments where both texts became empty after filtering
            bilingual_results.erase(
                std::remove_if(bilingual_results.begin(), bilingual_results.end(),
                    [](const BilingualSegment& s) {
                        return s.original_text.empty() && s.english_text.empty();
                    }),
                bilingual_results.end());

            // Print results
            {
                // In sliding window mode, only accumulate to meeting/auto-copy/export/fout
                // at finalization boundaries (when the window has the most complete text).
                // VAD mode: every iteration is independent, so always accumulate.
                const bool is_boundary = use_vad || (((n_iter + 1) % n_new_line) == 0);

                // For pipe mode: clear current buffer before re-rendering
                if (!stdout_is_tty && !use_vad) {
                    pipe_current_text.str("");
                    pipe_current_text.clear();
                }

                if (!use_vad) {
                    if (stdout_is_tty) {
                        printf("\33[2K\r");
                        printf("%s", std::string(100, ' ').c_str());
                        printf("\33[2K\r");
                    }
                } else {
                    const int64_t t1 = (t_last - t_start).count()/1000000;
                    const int64_t t0 = std::max(0.0, t1 - pcmf32.size()*1000.0/WHISPER_SAMPLE_RATE);
                    if (stdout_is_tty) {
                        printf("\n### Transcription %d START | t0 = %d ms | t1 = %d ms\n", n_iter, (int) t0, (int) t1);
                        printf("\n");
                    }
                }

                // Use colored token output if enabled, otherwise use segment-based output
                if (params.print_colors) {
                    // Print tokens directly from whisper context with colors
                    const int n_segments = whisper_full_n_segments(ctx);
                    for (int i = 0; i < n_segments; ++i) {
                        if (stdout_is_tty) {
                            if (!params.no_timestamps) {
                                const int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                                const int64_t t1 = whisper_full_get_segment_t1(ctx, i);
                                printf("[%s --> %s]  ", to_timestamp(t0).c_str(), to_timestamp(t1).c_str());
                            }
                            print_colored_tokens(ctx, i, params);
                            printf("\n");
                        }

                        // Accumulate text for pipe even in color mode (always for display)
                        const char* seg_text = whisper_full_get_segment_text(ctx, i);
                        bool speaker_turn = whisper_full_get_segment_speaker_turn_next(ctx, i);

                        // Use shared speaker tracker
                        int seg_speaker_id = speaker_tracker.get_current();
                        if (speaker_turn) {
                            seg_speaker_id = speaker_tracker.on_turn();
                        }

                        if (!stdout_is_tty) {
                            pipe_current_text << seg_text;
                        }
                        // Only accumulate to persistent buffers at finalization boundaries
                        if (is_boundary) {
                            if (params.meeting_mode) {
                                meeting_session.add_transcription(std::string(seg_text), speaker_turn);
                                meeting_session.add_transcription(" ");
                            }
                            if ((params.auto_copy_enabled && should_auto_copy(auto_copy_session, params)) || params.history_enabled) {
                                auto_copy_session.transcription_buffer << seg_text;
                            }
                            if (params.export_enabled) {
                                int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                                int64_t t1 = whisper_full_get_segment_t1(ctx, i);
                                export_session.segments.emplace_back(t0 / 10, t1 / 10, std::string(seg_text), 1.0f, speaker_turn, speaker_turn ? seg_speaker_id : -1);
                            }
                        }
                    }
                } else {
                    // Use segment-based bilingual output
                    std::ostringstream* pbuf = stdout_is_tty ? nullptr : &pipe_current_text;
                    print_bilingual_results(bilingual_results, params, auto_copy_session, export_session, speaker_tracker, &meeting_session,
                                            stdout_is_tty, pbuf, is_boundary);
                }

                if (is_boundary && params.fname_out.length() > 0) {
                    const int n_seg = whisper_full_n_segments(ctx);
                    for (int i = 0; i < n_seg; ++i) {
                        const char* seg_text = whisper_full_get_segment_text(ctx, i);
                        if (seg_text) {
                            fout << seg_text;
                        }
                    }
                    fout << std::endl;
                    fout.flush();
                }

                if (use_vad) {
                    if (stdout_is_tty) {
                        printf("\n### Transcription %d END\n", n_iter);
                    }
                    // In VAD mode, each iteration is independent — finalize immediately
                    if (!stdout_is_tty) {
                        pipe_finalized_text += pipe_current_text.str();
                        pipe_current_text.str("");
                        pipe_current_text.clear();
                    }
                }
            }

            ++n_iter;

            if (!use_vad && (n_iter % n_new_line) == 0) {
                if (stdout_is_tty) {
                    printf("\n");
                }
                // Finalize current group's text for pipe output
                if (!stdout_is_tty) {
                    pipe_finalized_text += pipe_current_text.str();
                    pipe_current_text.str("");
                    pipe_current_text.clear();
                }
                pcmf32_old = std::vector<float>(pcmf32.end() - n_samples_keep, pcmf32.end());

                if (!params.no_context) {
                    prompt_tokens.clear();
                    const int n_segments = whisper_full_n_segments(ctx);
                    for (int i = 0; i < n_segments; ++i) {
                        const int token_count = whisper_full_n_tokens(ctx, i);
                        for (int j = 0; j < token_count; ++j) {
                            prompt_tokens.push_back(whisper_full_get_token_id(ctx, i, j));
                        }
                    }
                }
            }
            fflush(stdout);
        }
    }

    audio.pause();

    // Refine accumulated text via Claude if enabled (standard mode)
    if (params.refine) {
        // Refine pipe output
        std::string raw_pipe = pipe_finalized_text + pipe_current_text.str();
        if (!raw_pipe.empty()) {
            std::string refined = refine_transcription(raw_pipe);
            pipe_finalized_text = refined;
            pipe_current_text.str("");
            pipe_current_text.clear();
        }
        // Refine auto-copy buffer
        if (params.auto_copy_enabled) {
            std::string raw_copy = auto_copy_session.transcription_buffer.str();
            if (!raw_copy.empty()) {
                std::string refined_copy = refine_transcription(raw_copy);
                auto_copy_session.transcription_buffer.str("");
                auto_copy_session.transcription_buffer.clear();
                auto_copy_session.transcription_buffer << refined_copy;
            }
        }
    }

    // Dump accumulated text to stdout when not a TTY (pipe/redirect mode)
    if (!stdout_is_tty) {
        std::string final_text = pipe_finalized_text + pipe_current_text.str();
        if (!final_text.empty()) {
            printf("%s\n", final_text.c_str());
            fflush(stdout);
        }
    }

    // Gather final transcript text for history
    std::string history_text;
    if (params.meeting_mode) {
        history_text = meeting_session.get_transcription();
    } else if (!stdout_is_tty) {
        history_text = pipe_finalized_text + pipe_current_text.str();
    } else {
        history_text = auto_copy_session.transcription_buffer.str();
    }

    // Finalize session: auto-copy, export, meeting processing
    finalize_session(params, auto_copy_session, export_session, speaker_tracker,
                     meeting_session, meeting_output_file, history_text, auto_copy_session.start_time);

    // Clear recording state
    g_is_recording.store(false);
    
    whisper_free(ctx);

    // Clean up translation context if it was created
    if (ctx_translate) {
        whisper_free(ctx_translate);
    }

    return 0;
}