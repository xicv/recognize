#pragma once

#include "whisper_params.h"

// Forward declarations
class ModelManager;

// Parse CLI arguments into params. Returns false on parse error.
bool whisper_params_parse(int argc, char** argv, whisper_params& params);

// Print CLI usage/help text.
void whisper_print_usage(int argc, char** argv, const whisper_params& params);

// Handle model management commands (list, delete, cleanup, etc.)
// Returns: -1 if no command matched (continue to main flow), 0+ for exit code
int handle_model_commands(const whisper_params& params, ModelManager& model_manager);

// Handle "recognize history ..." subcommand
int handle_history_command(int argc, char** argv);
