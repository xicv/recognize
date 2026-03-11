#include "cli_parser.h"
#include "config_manager.h"
#include "model_manager.h"
#include "history_manager.h"

#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>

bool whisper_params_parse(int argc, char ** argv, whisper_params & params) {
    // Helper to check that a required argument value exists
    auto require_arg = [&](int idx, const std::string& flag) -> bool {
        if (idx + 1 >= argc) {
            fprintf(stderr, "error: '%s' requires an argument\n", flag.c_str());
            return false;
        }
        return true;
    };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            whisper_print_usage(argc, argv, params);
            exit(0);
        }
        else if (arg == "-t"    || arg == "--threads")       { if (!require_arg(i, arg)) return false; params.n_threads     = std::stoi(argv[++i]); }
        else if (                  arg == "--step")          { if (!require_arg(i, arg)) return false; params.step_ms       = std::stoi(argv[++i]); }
        else if (                  arg == "--length")        { if (!require_arg(i, arg)) return false; params.length_ms     = std::stoi(argv[++i]); }
        else if (                  arg == "--keep")          { if (!require_arg(i, arg)) return false; params.keep_ms       = std::stoi(argv[++i]); }
        else if (arg == "-c"    || arg == "--capture")       { if (!require_arg(i, arg)) return false; params.capture_id    = std::stoi(argv[++i]); }
        else if (arg == "-mt"   || arg == "--max-tokens")    { if (!require_arg(i, arg)) return false; params.max_tokens    = std::stoi(argv[++i]); }
        else if (arg == "-ac"   || arg == "--audio-ctx")     { if (!require_arg(i, arg)) return false; params.audio_ctx     = std::stoi(argv[++i]); }
        else if (arg == "-bs"   || arg == "--beam-size")     { if (!require_arg(i, arg)) return false; params.beam_size     = std::stoi(argv[++i]); }
        else if (arg == "-vth"  || arg == "--vad-thold")     { if (!require_arg(i, arg)) return false; params.vad_thold     = std::stof(argv[++i]); }
        else if (arg == "-fth"  || arg == "--freq-thold")    { if (!require_arg(i, arg)) return false; params.freq_thold    = std::stof(argv[++i]); }
        else if (arg == "-tr"   || arg == "--translate")     { params.translate     = true; }
        else if (arg == "-nf"   || arg == "--no-fallback")   { params.no_fallback   = true; }
        else if (arg == "-ps"   || arg == "--print-special") { params.print_special = true; }
        else if (arg == "-pc"   || arg == "--print-colors")  { params.print_colors  = true; }
        else if (arg == "-kc"   || arg == "--keep-context")  { params.no_context    = false; }
        else if (arg == "-l"    || arg == "--language")      { if (!require_arg(i, arg)) return false; params.language      = argv[++i]; }
        else if (arg == "-m"    || arg == "--model")         { if (!require_arg(i, arg)) return false; params.model         = argv[++i]; }
        else if (arg == "-f"    || arg == "--file")          { if (!require_arg(i, arg)) return false; params.fname_out     = argv[++i]; }
        else if (arg == "-om"   || arg == "--output-mode")   { if (!require_arg(i, arg)) return false; params.output_mode   = argv[++i]; }
        else if (arg == "-tdrz" || arg == "--tinydiarize")   { params.tinydiarize   = true; }
        else if (arg == "-sa"   || arg == "--save-audio")    { params.save_audio    = true; }
        else if (arg == "-ng"   || arg == "--no-gpu")        { params.use_gpu       = false; }
        else if (arg == "-fa"   || arg == "--flash-attn")    { params.flash_attn    = true; }
        // CoreML specific options
        else if (arg == "-coreml" || arg == "--coreml")      { params.use_coreml    = true; }
        else if (arg == "-ncoreml" || arg == "--no-coreml")  { params.use_coreml    = false; }
        else if (arg == "-cm"   || arg == "--coreml-model")  { if (!require_arg(i, arg)) return false; params.coreml_model  = argv[++i]; }
        // Model management options
        else if (arg == "--list-models")                     { params.list_models   = true; }
        else if (arg == "--list-downloaded")                 { params.list_downloaded = true; }
        else if (arg == "--show-storage")                    { params.show_storage  = true; }
        else if (arg == "--delete-model")                    { if (!require_arg(i, arg)) return false; params.delete_model_flag = true; params.model_to_delete = argv[++i]; }
        else if (arg == "--delete-all-models")               { params.delete_all_models_flag = true; }
        else if (arg == "--cleanup")                         { params.cleanup_models = true; }
        // Auto-copy options
        else if (arg == "--auto-copy")                       { params.auto_copy_enabled = true; }
        else if (arg == "--no-auto-copy")                    { params.auto_copy_enabled = false; }
        else if (arg == "--auto-copy-max-duration")          { if (!require_arg(i, arg)) return false; params.auto_copy_max_duration_hours = std::stoi(argv[++i]); }
        else if (arg == "--auto-copy-max-size")              { if (!require_arg(i, arg)) return false; params.auto_copy_max_size_bytes = std::stoi(argv[++i]); }
        // Export options
        else if (arg == "--export")                          { params.export_enabled = true; }
        else if (arg == "--no-export")                       { params.export_enabled = false; }
        else if (arg == "--export-format")                   { if (!require_arg(i, arg)) return false; params.export_format = argv[++i]; }
        else if (arg == "--export-file")                     { if (!require_arg(i, arg)) return false; params.export_file = argv[++i]; params.export_auto_filename = false; }
        else if (arg == "--export-auto-filename")            { params.export_auto_filename = true; }
        else if (arg == "--export-no-metadata")              { params.export_include_metadata = false; }
        else if (arg == "--export-no-timestamps")            { params.export_include_timestamps = false; }
        else if (arg == "--export-include-confidence")       { params.export_include_confidence = true; }
        // Meeting options
        else if (arg == "--meeting")                         { params.meeting_mode = true; }
        else if (arg == "--no-meeting")                      { params.meeting_mode = false; }
        else if (arg == "--prompt")                          { if (!require_arg(i, arg)) return false; params.meeting_prompt = argv[++i]; }
        else if (arg == "--name")                            { if (!require_arg(i, arg)) return false; params.meeting_name = argv[++i]; }
        else if (arg == "--meeting-timeout")                  { if (!require_arg(i, arg)) return false; params.meeting_timeout = std::stoi(argv[++i]); }
        else if (arg == "--meeting-max-single-pass")          { if (!require_arg(i, arg)) return false; params.meeting_max_single_pass = std::stoi(argv[++i]); }
        // Accuracy options
        else if (arg == "--initial-prompt")                   { if (!require_arg(i, arg)) return false; params.initial_prompt = argv[++i]; }
        else if (arg == "--suppress-regex")                   { if (!require_arg(i, arg)) return false; params.suppress_regex = argv[++i]; }
        else if (arg == "--entropy-thold")                    { if (!require_arg(i, arg)) return false; params.entropy_thold = std::stof(argv[++i]); }
        else if (arg == "--logprob-thold")                    { if (!require_arg(i, arg)) return false; params.logprob_thold = std::stof(argv[++i]); }
        else if (arg == "--no-speech-thold")                  { if (!require_arg(i, arg)) return false; params.no_speech_thold = std::stof(argv[++i]); }
        else if (arg == "--length-penalty")                   { if (!require_arg(i, arg)) return false; params.length_penalty = std::stof(argv[++i]); }
        else if (arg == "--best-of")                          { if (!require_arg(i, arg)) return false; params.best_of = std::stoi(argv[++i]); }
        else if (arg == "--suppress-nst")                     { params.suppress_nst = true; }
        else if (arg == "--carry-prompt")                     { params.carry_initial_prompt = true; }
        else if (arg == "--no-normalize")                     { params.normalize_audio = false; }
        // VAD model
        else if (arg == "--vad-model")                        { if (!require_arg(i, arg)) return false; params.vad_model_path = argv[++i]; }
        // Silence timeout
        else if (arg == "--silence-timeout")                  { if (!require_arg(i, arg)) return false; params.silence_timeout = std::stof(argv[++i]); }
        // Push-to-talk options
        else if (arg == "--ptt")                              { params.ptt_mode = true; }
        else if (arg == "--ptt-key")                          { if (!require_arg(i, arg)) return false; params.ptt_key = argv[++i]; }
        // Refinement
        else if (arg == "-r"    || arg == "--refine")         { params.refine = true; }
        // History
        else if (arg == "--no-history")                       { params.history_enabled = false; }
        // Config management options
        else if (arg == "config") {
            if (i + 1 < argc) {
                std::string config_cmd = argv[++i];
                ConfigManager config_manager;
                config_manager.load_config();

                if (config_cmd == "list") {
                    config_manager.list_config();
                    exit(0);
                } else if (config_cmd == "set" && i + 2 < argc) {
                    std::string key = argv[++i];
                    std::string value = argv[++i];
                    if (config_manager.set_config(key, value)) {
                        config_manager.save_user_config();
                        std::cout << "Set " << key << " = " << value << std::endl;
                        exit(0);
                    } else {
                        std::cerr << "Failed to set config: " << key << std::endl;
                        exit(1);
                    }
                } else if (config_cmd == "get" && i + 1 < argc) {
                    std::string key = argv[++i];
                    auto value = config_manager.get_config(key);
                    if (value) {
                        std::cout << key << " = " << *value << std::endl;
                    } else {
                        std::cout << key << " is not set" << std::endl;
                    }
                    exit(0);
                } else if (config_cmd == "unset" && i + 1 < argc) {
                    std::string key = argv[++i];
                    if (config_manager.unset_config(key)) {
                        config_manager.save_user_config();
                        std::cout << "Unset " << key << std::endl;
                        exit(0);
                    } else {
                        std::cerr << "Failed to unset config: " << key << std::endl;
                        exit(1);
                    }
                } else if (config_cmd == "reset") {
                    config_manager.reset_config();
                    config_manager.save_user_config();
                    std::cout << "Configuration reset to defaults" << std::endl;
                    exit(0);
                } else {
                    std::cerr << "Unknown config command: " << config_cmd << std::endl;
                    std::cerr << "Available commands: list, set <key> <value>, get <key>, unset <key>, reset" << std::endl;
                    exit(1);
                }
            } else {
                std::cerr << "Config command requires a subcommand" << std::endl;
                std::cerr << "Available commands: list, set <key> <value>, get <key>, unset <key>, reset" << std::endl;
                exit(1);
            }
        }
        else if (arg == "--no-timestamps")                   { params.no_timestamps = true; }

        else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            whisper_print_usage(argc, argv, params);
            exit(0);
        }
    }

    return true;
}

void whisper_print_usage(int /*argc*/, char ** argv, const whisper_params & params) {
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -h,       --help          [default] show this help message and exit\n");
    fprintf(stderr, "  -t N,     --threads N     [%-7d] number of threads to use during computation\n",    params.n_threads);
    fprintf(stderr, "            --step N        [%-7d] audio step size in milliseconds\n",                params.step_ms);
    fprintf(stderr, "            --length N      [%-7d] audio length in milliseconds\n",                   params.length_ms);
    fprintf(stderr, "            --keep N        [%-7d] audio to keep from previous step in ms\n",         params.keep_ms);
    fprintf(stderr, "  -c ID,    --capture ID    [%-7d] capture device ID\n",                              params.capture_id);
    fprintf(stderr, "  -mt N,    --max-tokens N  [%-7d] maximum number of tokens per audio chunk\n",       params.max_tokens);
    fprintf(stderr, "  -ac N,    --audio-ctx N   [%-7d] audio context size (0 - all)\n",                   params.audio_ctx);
    fprintf(stderr, "  -bs N,    --beam-size N   [%-7d] beam size for beam search\n",                      params.beam_size);
    fprintf(stderr, "  -vth N,   --vad-thold N   [%-7.2f] voice activity detection threshold\n",           params.vad_thold);
    fprintf(stderr, "  -fth N,   --freq-thold N  [%-7.2f] high-pass frequency cutoff\n",                   params.freq_thold);
    fprintf(stderr, "  -tr,      --translate     [%-7s] translate from source language to english\n",      params.translate ? "true" : "false");
    fprintf(stderr, "  -nf,      --no-fallback   [%-7s] do not use temperature fallback while decoding\n", params.no_fallback ? "true" : "false");
    fprintf(stderr, "  -ps,      --print-special [%-7s] print special tokens\n",                           params.print_special ? "true" : "false");
    fprintf(stderr, "  -pc,      --print-colors  [%-7s] print colors based on token confidence\n",         params.print_colors ? "true" : "false");
    fprintf(stderr, "  -kc,      --keep-context  [%-7s] keep context between audio chunks\n",              params.no_context ? "false" : "true");
    fprintf(stderr, "  -l LANG,  --language LANG [%-7s] spoken language\n",                                params.language.c_str());
    fprintf(stderr, "  -m FNAME, --model FNAME   [%-7s] model path\n",                                     params.model.c_str());
    fprintf(stderr, "  -f FNAME, --file FNAME    [%-7s] text output file name\n",                          params.fname_out.c_str());
    fprintf(stderr, "  -om MODE, --output-mode MODE [%-7s] output mode: original, english, bilingual\n",    params.output_mode.c_str());
    fprintf(stderr, "  -tdrz,    --tinydiarize   [%-7s] enable speaker segmentation (requires tdrz model)\n", params.tinydiarize ? "true" : "false");
    fprintf(stderr, "  -sa,      --save-audio    [%-7s] save the recorded audio to a file\n",              params.save_audio ? "true" : "false");
    fprintf(stderr, "  -ng,      --no-gpu        [%-7s] disable GPU inference\n",                          params.use_gpu ? "false" : "true");
    fprintf(stderr, "  -fa,      --flash-attn    [%-7s] flash attention during inference\n",               params.flash_attn ? "true" : "false");
    // CoreML specific help
    fprintf(stderr, "  -coreml,  --coreml        [%-7s] enable CoreML acceleration (macOS)\n",             params.use_coreml ? "true" : "false");
    fprintf(stderr, "  -ncoreml, --no-coreml     [%-7s] disable CoreML acceleration\n",                    params.use_coreml ? "false" : "true");
    fprintf(stderr, "  -cm FNAME,--coreml-model FNAME [%-7s] CoreML model path\n",                        params.coreml_model.c_str());
    fprintf(stderr, "\n");
    fprintf(stderr, "auto-copy options:\n");
    fprintf(stderr, "            --auto-copy     [%-7s] automatically copy transcription to clipboard when session ends\n", params.auto_copy_enabled ? "true" : "false");
    fprintf(stderr, "            --no-auto-copy  [%-7s] disable auto-copy functionality\n",                params.auto_copy_enabled ? "false" : "true");
    fprintf(stderr, "            --auto-copy-max-duration N [%-7d] max session duration in hours before skipping auto-copy\n", params.auto_copy_max_duration_hours);
    fprintf(stderr, "            --auto-copy-max-size N     [%-7d] max transcription size in bytes before skipping auto-copy\n", params.auto_copy_max_size_bytes);
    fprintf(stderr, "\n");
    fprintf(stderr, "export options:\n");
    fprintf(stderr, "            --export        [%-7s] enable transcription export when session ends\n", params.export_enabled ? "true" : "false");
    fprintf(stderr, "            --no-export     [%-7s] disable transcription export\n",                params.export_enabled ? "false" : "true");
    fprintf(stderr, "            --export-format FORMAT [%-7s] export format: txt, md, json, csv, srt, vtt, xml\n", params.export_format.c_str());
    fprintf(stderr, "            --export-file FILE      [%-7s] export to specific file (default: auto-generated)\n", params.export_file.c_str());
    fprintf(stderr, "            --export-auto-filename  [%-7s] generate automatic filename\n",          params.export_auto_filename ? "true" : "false");
    fprintf(stderr, "            --export-no-metadata    [%-7s] exclude session metadata from export\n", params.export_include_metadata ? "false" : "true");
    fprintf(stderr, "            --export-no-timestamps  [%-7s] exclude timestamps from export\n",     params.export_include_timestamps ? "false" : "true");
    fprintf(stderr, "            --export-include-confidence [%-7s] include confidence scores in export\n", params.export_include_confidence ? "true" : "false");
    fprintf(stderr, "\n");
    fprintf(stderr, "meeting organization:\n");
    fprintf(stderr, "            --meeting        [%-7s] enable meeting transcription mode\n", params.meeting_mode ? "true" : "false");
    fprintf(stderr, "            --no-meeting     [%-7s] disable meeting mode (when enabled via config)\n", params.meeting_mode ? "false" : "true");
    fprintf(stderr, "            --prompt PROMPT  [%-7s] custom prompt text or file path for meeting organization\n", params.meeting_prompt.empty() ? "default" : "custom");
    fprintf(stderr, "            --name NAME      [%-7s] meeting output filename prefix\n", params.meeting_name.empty() ? "auto" : params.meeting_name.c_str());
    fprintf(stderr, "            --meeting-timeout N [%-4d] timeout for Claude CLI in seconds\n", params.meeting_timeout);
    fprintf(stderr, "            --meeting-max-single-pass N [%-4d] max words before multi-pass\n", params.meeting_max_single_pass);
    fprintf(stderr, "\n");
    fprintf(stderr, "accuracy options:\n");
    fprintf(stderr, "            --initial-prompt TEXT [%-7s] initial prompt for conditioning\n", params.initial_prompt.empty() ? "none" : "set");
    fprintf(stderr, "            --suppress-regex PAT  [%-7s] regex pattern to suppress from output\n", params.suppress_regex.empty() ? "none" : "set");
    fprintf(stderr, "            --entropy-thold N     [%-7.1f] entropy threshold for decoder fallback\n", params.entropy_thold);
    fprintf(stderr, "            --logprob-thold N     [%-7.1f] avg log probability threshold for fallback (-1.0 = disabled)\n", params.logprob_thold);
    fprintf(stderr, "            --no-speech-thold N   [%-7.1f] no-speech probability threshold\n", params.no_speech_thold);
    fprintf(stderr, "            --length-penalty N    [%-7.1f] length penalty for beam search scoring (-1.0 = disabled)\n", params.length_penalty);
    fprintf(stderr, "            --best-of N           [%-7d] number of best candidates for greedy decoding (-1 = default)\n", params.best_of);
    fprintf(stderr, "            --suppress-nst        [%-7s] suppress non-speech tokens\n", params.suppress_nst ? "true" : "false");
    fprintf(stderr, "            --carry-prompt        [%-7s] keep initial prompt across all decode windows\n", params.carry_initial_prompt ? "true" : "false");
    fprintf(stderr, "            --no-normalize        [%-7s] disable audio normalization\n", params.normalize_audio ? "false" : "true");
    fprintf(stderr, "            --vad-model PATH      [%-7s] Silero VAD model path for speech detection\n", params.vad_model_path.empty() ? "none" : "set");
    fprintf(stderr, "            --silence-timeout N   [%-7.1f] auto-stop after N seconds of silence (0 = disabled)\n", params.silence_timeout);
    fprintf(stderr, "\n");
    fprintf(stderr, "push-to-talk:\n");
    fprintf(stderr, "            --ptt              [%-7s] enable push-to-talk mode (hold key to record)\n", params.ptt_mode ? "true" : "false");
    fprintf(stderr, "            --ptt-key KEY      [%-7s] PTT key: space, right_option, right_ctrl, fn, f13\n", params.ptt_key.c_str());
    fprintf(stderr, "\n");
    fprintf(stderr, "refinement:\n");
    fprintf(stderr, "  -r,       --refine           [%-7s] refine transcript via Claude CLI (ASR error correction)\n", params.refine ? "true" : "false");
    fprintf(stderr, "  --no-history                  [%-7s] do not save transcript to history\n", "false");
    fprintf(stderr, "\n");
    fprintf(stderr, "model management:\n");
    fprintf(stderr, "            --list-models      list available models for download\n");
    fprintf(stderr, "            --list-downloaded  list downloaded models with sizes and paths\n");
    fprintf(stderr, "            --show-storage     show detailed storage usage\n");
    fprintf(stderr, "            --delete-model MODEL  delete a specific model\n");
    fprintf(stderr, "            --delete-all-models    delete all downloaded models\n");
    fprintf(stderr, "            --cleanup          remove orphaned model files\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "configuration:\n");
    fprintf(stderr, "  config list                 show current configuration\n");
    fprintf(stderr, "  config set <key> <value>    set configuration value\n");
    fprintf(stderr, "  config get <key>            get configuration value\n");
    fprintf(stderr, "  config unset <key>          unset configuration value\n");
    fprintf(stderr, "  config reset                reset all configuration to defaults\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "examples:\n");
    fprintf(stderr, "  %s                                    # interactive model selection\n", argv[0]);
    fprintf(stderr, "  %s -m base.en                        # download and use base.en model\n", argv[0]);
    fprintf(stderr, "  %s -m tiny.en --step 0 --length 30000 # VAD mode with tiny model\n", argv[0]);
    fprintf(stderr, "  %s --list-models                      # show available models\n", argv[0]);
    fprintf(stderr, "  %s --list-downloaded                  # show downloaded models\n", argv[0]);
    fprintf(stderr, "  %s --show-storage                     # show storage usage\n", argv[0]);
    fprintf(stderr, "  %s --delete-model base.en             # delete specific model\n", argv[0]);
    fprintf(stderr, "  %s --cleanup                          # remove orphaned files\n", argv[0]);
    fprintf(stderr, "  %s --export --export-format json      # export transcription to JSON\n", argv[0]);
    fprintf(stderr, "  %s --export --export-format md --export-file meeting.md # export to Markdown\n", argv[0]);
    fprintf(stderr, "  %s --export --export-format srt       # generate SRT subtitles\n", argv[0]);
    fprintf(stderr, "  %s --auto-copy                        # auto-copy to clipboard\n", argv[0]);
    fprintf(stderr, "  %s --output-mode bilingual            # show original + English translation\n", argv[0]);
    fprintf(stderr, "  %s --output-mode english -l zh        # translate Chinese to English only\n", argv[0]);
    fprintf(stderr, "  %s config set model base.en           # set default model\n", argv[0]);
    fprintf(stderr, "  %s config set export_enabled true     # enable auto-export\n", argv[0]);
    fprintf(stderr, "  %s config list                        # show current config\n", argv[0]);
    fprintf(stderr, "  %s --meeting                         # organize meeting transcription (saves to [YYYY]-[MM]-[DD].md)\n", argv[0]);
    fprintf(stderr, "  %s --meeting --prompt custom.txt      # use custom prompt file\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "subcommands:\n");
    fprintf(stderr, "  config [list|set|get|unset|reset]        manage configuration\n");
    fprintf(stderr, "  history [list|search|show|clear|count]   transcription history\n");
    fprintf(stderr, "\n");
}

// Handle model management commands that exit early (list, delete, cleanup, etc.)
// Returns: -1 if no command matched (continue to main flow), 0+ for exit code
int handle_model_commands(const whisper_params& params, ModelManager& model_manager) {
    if (params.list_models) {
        model_manager.list_available_models();
        return 0;
    }
    if (params.list_downloaded) {
        model_manager.list_downloaded_models();
        return 0;
    }
    if (params.show_storage) {
        model_manager.show_storage_usage();
        return 0;
    }
    if (params.delete_model_flag) {
        return model_manager.delete_model(params.model_to_delete, true) ? 0 : 1;
    }
    if (params.delete_all_models_flag) {
        return model_manager.delete_all_models(true) ? 0 : 1;
    }
    if (params.cleanup_models) {
        model_manager.cleanup_orphaned_files();
        return 0;
    }
    return -1; // No command matched
}

int handle_history_command(int argc, char** argv) {
    std::string subcmd = (argc >= 1) ? argv[0] : "list";
    bool json_output = false;
    int limit = 20;
    int offset = 0;

    // Safe integer parsing — returns default_val on invalid input
    auto safe_stoi = [](const char* s, int default_val = 0) -> int {
        try { return std::stoi(s); } catch (const std::exception&) { return default_val; }
    };
    auto safe_stoll = [](const char* s, int64_t default_val = 0) -> int64_t {
        try { return std::stoll(s); } catch (const std::exception&) { return default_val; }
    };

    // Auto-detect: JSON output when stdout is not a TTY
    if (!isatty(STDOUT_FILENO)) json_output = true;

    // Parse common flags
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--json") json_output = true;
    }

    HistoryManager history;
    if (!history.open()) {
        std::cerr << "error: failed to open history database\n";
        return 1;
    }

    if (subcmd == "list" || subcmd == "--json" || subcmd == "--limit" || subcmd == "--offset") {
        // "recognize history" with just flags defaults to list
        for (int i = 0; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--limit" && i + 1 < argc) limit = safe_stoi(argv[++i], 20);
            else if (arg == "--offset" && i + 1 < argc) offset = safe_stoi(argv[++i], 0);
        }
        auto entries = history.list(limit, offset);
        std::cout << (json_output ? HistoryManager::format_json(entries) : HistoryManager::format_table(entries));
        return 0;
    }

    if (subcmd == "search") {
        std::string query;
        std::string since;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--json") { json_output = true; continue; }
            if (arg == "--limit" && i + 1 < argc) { limit = safe_stoi(argv[++i], 20); continue; }
            if (arg == "--since" && i + 1 < argc) {
                std::string val = argv[++i];
                if (!val.empty() && val.back() == 'd') {
                    int days = safe_stoi(val.substr(0, val.size() - 1).c_str(), 0);
                    time_t now = time(nullptr);
                    now -= days * 86400;
                    std::tm tm_buf;
                    localtime_r(&now, &tm_buf);
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
                    since = buf;
                } else {
                    since = val;
                }
                continue;
            }
            if (!query.empty()) query += " ";
            query += arg;
        }
        if (query.empty()) {
            std::cerr << "error: search requires a query\n"
                      << "usage: recognize history search <query> [--limit N] [--since Nd] [--json]\n";
            return 1;
        }
        auto entries = history.search(query, limit, since);
        std::cout << (json_output ? HistoryManager::format_json(entries) : HistoryManager::format_table(entries));
        return 0;
    }

    if (subcmd == "show") {
        if (argc < 2) {
            std::cerr << "error: show requires an ID\n"
                      << "usage: recognize history show <id> [--json]\n";
            return 1;
        }
        int64_t id = safe_stoll(argv[1], -1);
        if (id < 0) {
            std::cerr << "error: invalid ID '" << argv[1] << "'\n";
            return 1;
        }
        auto entry = history.get(id);
        if (!entry) {
            std::cerr << "error: transcript #" << id << " not found\n";
            return 1;
        }
        if (json_output) {
            std::cout << HistoryManager::format_entry_json(*entry) << "\n";
        } else {
            std::cout << "Transcript #" << entry->id << "\n"
                      << "  Time:     " << entry->timestamp << "\n"
                      << "  Duration: " << std::fixed << std::setprecision(1) << entry->duration_s << "s\n"
                      << "  Model:    " << entry->model << "\n"
                      << "  Mode:     " << entry->mode << "\n"
                      << "  Words:    " << entry->word_count << "\n"
                      << "  ---\n"
                      << entry->text << "\n";
        }
        return 0;
    }

    if (subcmd == "clear") {
        bool do_clear_all = false;
        int older_than = 0;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--all") do_clear_all = true;
            else if (arg == "--older-than" && i + 1 < argc) {
                std::string val = argv[++i];
                if (!val.empty() && val.back() == 'd') {
                    older_than = safe_stoi(val.substr(0, val.size() - 1).c_str(), 0);
                } else {
                    older_than = safe_stoi(val.c_str(), 0);
                }
            }
        }
        if (!do_clear_all && older_than <= 0) {
            std::cerr << "error: specify --all or --older-than Nd\n"
                      << "usage: recognize history clear [--older-than Nd] [--all]\n";
            return 1;
        }
        int deleted;
        if (do_clear_all) {
            if (isatty(STDIN_FILENO)) {
                std::cerr << "Delete ALL transcription history? (y/N): ";
                char c = 'n';
                std::cin >> c;
                if (c != 'y' && c != 'Y') {
                    std::cerr << "Cancelled.\n";
                    return 0;
                }
            }
            deleted = history.clear_all();
        } else {
            deleted = history.clear_older_than(older_than);
        }
        std::cerr << "Deleted " << deleted << " entries.\n";
        return 0;
    }

    if (subcmd == "count") {
        int total = history.count();
        if (json_output) {
            std::cout << "{\"count\":" << total << "}\n";
        } else {
            std::cout << total << " transcriptions in history\n";
        }
        return 0;
    }

    std::cerr << "error: unknown history command: " << subcmd << "\n"
              << "usage: recognize history [list|search|show|clear|count]\n";
    return 1;
}
