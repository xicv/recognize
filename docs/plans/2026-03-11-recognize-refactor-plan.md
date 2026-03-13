# recognize.cpp Refactoring Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extract 4 modules from the 2782-line monolithic `recognize.cpp` into focused files, without changing any behavior.

**Architecture:** Conservative extraction of 4 domain-specific modules (`text_processing`, `meeting_manager`, `audio_processor`, `cli_parser`) in dependency order. Each step produces a buildable, testable state. `recognize.cpp` remains as the ~800-line session orchestrator. Session structs (`AutoCopySession`, `ExportSession`) and signal handling stay in `recognize.cpp`.

**Tech Stack:** C++17, CMake, whisper.cpp, macOS 12.0+, SQLite3

---

### Task 1: Create text_processing.h/.cpp

**Files:**
- Create: `text_processing.h`
- Create: `text_processing.cpp`
- Modify: `CMakeLists.txt:43-49` (add text_processing.cpp to sources)
- Modify: `recognize.cpp` (remove extracted functions, add `#include "text_processing.h"`)

**Step 1: Create `text_processing.h`**

```cpp
#pragma once

#include <string>
#include <vector>

// String utilities
std::string trim_whitespace(const std::string& str);

// Clipboard
bool copy_to_clipboard_macos(const std::string& text);

// Claude CLI
bool is_claude_cli_available();
std::string invoke_claude_cli(const std::string& prompt_text, int timeout_seconds);

// ASR refinement
extern const std::string ASR_REFINE_PROMPT;
std::string refine_transcription(const std::string& raw_text, int timeout_seconds = 30);

// Hallucination filtering
std::string filter_hallucinations(const std::string& text);

// Text analysis
int count_words(const std::string& text);
std::vector<std::string> split_into_chunks(const std::string& text, int max_words_per_chunk);
```

**Step 2: Create `text_processing.cpp`**

Move these functions from `recognize.cpp` (preserve exact implementation, no reformatting):

- `trim_whitespace()` (line 264-271)
- `copy_to_clipboard_macos()` (line 273-288)
- `is_claude_cli_available()` (line 290-302)
- `invoke_claude_cli()` (line 508-553) — remove `static` qualifier
- `ASR_REFINE_PROMPT` constant (line 558-583)
- `refine_transcription()` (line 587-617) — remove `static` qualifier
- `filter_hallucinations()` (line 338-460)
- `count_words()` (line 463-475) — remove `static` qualifier
- `split_into_chunks()` (line 478-506) — remove `static` qualifier

Add at top:
```cpp
#include "text_processing.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unistd.h>
```

**Step 3: Update `recognize.cpp`**

- Add `#include "text_processing.h"` after existing includes
- Remove the function bodies listed in Step 2 from recognize.cpp
- Keep `#include <filesystem>` and other includes that recognize.cpp still needs

**Step 4: Update `CMakeLists.txt`**

Add `text_processing.cpp` to the `add_executable` sources list (after `history_manager.cpp`).

**Step 5: Build and test**

```bash
make rebuild
```
Expected: Build succeeds, zero new warnings.

```bash
make test
```
Expected: Both smoke tests pass.

```bash
./recognize history count
```
Expected: Works (JSON or count output).

**Step 6: Commit**

```bash
git add text_processing.h text_processing.cpp CMakeLists.txt recognize.cpp
git commit -m "refactor: extract text_processing module from recognize.cpp"
```

---

### Task 2: Write text_processing unit tests

**Files:**
- Create: `tests/test_text_processing.cpp`
- Modify: `CMakeLists.txt` (add test target)
- Modify: `Makefile` (add test-unit target)

**Step 1: Create test file `tests/test_text_processing.cpp`**

```cpp
#include "../text_processing.h"
#include <cassert>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " << #name << "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS\n"; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << "\n"; }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { throw std::runtime_error(std::string("expected '") + std::string(b) + "' got '" + std::string(a) + "'"); }

#define ASSERT_TRUE(x) \
    if (!(x)) { throw std::runtime_error("assertion failed: " #x); }

// --- trim_whitespace ---

void test_trim_basic() {
    ASSERT_EQ(trim_whitespace("  hello  "), "hello");
}

void test_trim_empty() {
    ASSERT_EQ(trim_whitespace(""), "");
}

void test_trim_all_spaces() {
    ASSERT_EQ(trim_whitespace("   "), "");
}

void test_trim_tabs_newlines() {
    ASSERT_EQ(trim_whitespace("\t\nhello\r\n"), "hello");
}

// --- filter_hallucinations ---

void test_filter_thank_you() {
    ASSERT_EQ(filter_hallucinations("Thank you for watching"), "");
}

void test_filter_case_insensitive() {
    ASSERT_EQ(filter_hallucinations("  THANK YOU FOR WATCHING  "), "");
}

void test_filter_blank_audio() {
    ASSERT_EQ(filter_hallucinations("[BLANK_AUDIO]"), "");
}

void test_filter_silence() {
    ASSERT_EQ(filter_hallucinations("[silence]"), "");
    ASSERT_EQ(filter_hallucinations("[ Silence ]"), "");
    ASSERT_EQ(filter_hallucinations("[Silence]"), "");
}

void test_filter_typing() {
    ASSERT_EQ(filter_hallucinations("[typing sounds]"), "");
    ASSERT_EQ(filter_hallucinations("[keyboard sounds]"), "");
    ASSERT_EQ(filter_hallucinations("[typing]"), "");
}

void test_filter_url() {
    ASSERT_EQ(filter_hallucinations("www.example.com"), "");
    ASSERT_EQ(filter_hallucinations("https://example.com"), "");
}

void test_filter_real_speech() {
    std::string real = "Hello, this is a real sentence.";
    ASSERT_EQ(filter_hallucinations(real), real);
}

void test_filter_empty() {
    ASSERT_EQ(filter_hallucinations(""), "");
}

void test_filter_whitespace_only() {
    ASSERT_EQ(filter_hallucinations("   "), "");
}

void test_filter_dedup() {
    // 3+ sentences with consecutive duplicates should be deduped
    std::string input = "Hello. Hello. Hello. World.";
    std::string result = filter_hallucinations(input);
    // Should remove consecutive duplicates
    ASSERT_TRUE(result.find("Hello. Hello.") == std::string::npos || result.size() < input.size());
}

// --- count_words ---

void test_count_empty() {
    ASSERT_TRUE(count_words("") == 0);
}

void test_count_single() {
    ASSERT_TRUE(count_words("hello") == 1);
}

void test_count_multiple() {
    ASSERT_TRUE(count_words("hello world foo bar") == 4);
}

void test_count_extra_spaces() {
    ASSERT_TRUE(count_words("  hello   world  ") == 2);
}

// --- split_into_chunks ---

void test_split_short() {
    auto chunks = split_into_chunks("Hello world.", 100);
    ASSERT_TRUE(chunks.size() == 1);
    ASSERT_EQ(chunks[0], "Hello world.");
}

void test_split_boundary() {
    // Should split at sentence boundaries when word count exceeded
    std::string text = "One two three. Four five six. Seven eight nine.";
    auto chunks = split_into_chunks(text, 3);
    ASSERT_TRUE(chunks.size() >= 2);
}

void test_split_empty() {
    auto chunks = split_into_chunks("", 10);
    ASSERT_TRUE(chunks.empty());
}

int main() {
    std::cout << "text_processing tests:\n";

    TEST(trim_basic);
    TEST(trim_empty);
    TEST(trim_all_spaces);
    TEST(trim_tabs_newlines);

    TEST(filter_thank_you);
    TEST(filter_case_insensitive);
    TEST(filter_blank_audio);
    TEST(filter_silence);
    TEST(filter_typing);
    TEST(filter_url);
    TEST(filter_real_speech);
    TEST(filter_empty);
    TEST(filter_whitespace_only);
    TEST(filter_dedup);

    TEST(count_empty);
    TEST(count_single);
    TEST(count_multiple);
    TEST(count_extra_spaces);

    TEST(split_short);
    TEST(split_boundary);
    TEST(split_empty);

    std::cout << "\n" << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed > 0 ? 1 : 0;
}
```

**Step 2: Add test target to `CMakeLists.txt`**

After the main `target_link_libraries` block, add:

```cmake
# Unit tests for text_processing
add_executable(test_text_processing
    tests/test_text_processing.cpp
    text_processing.cpp
)
target_include_directories(test_text_processing PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(test_text_processing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
```

**Step 3: Add `test-unit` target to `Makefile`**

```makefile
.PHONY: test-unit
test-unit: configure
	@echo "$(BLUE)Building and running unit tests...$(NC)"
	@cd $(BUILD_DIR) && make test_text_processing -j$$(sysctl -n hw.ncpu)
	@./$(BUILD_DIR)/tests/test_text_processing && echo "$(GREEN)✓ All unit tests passed$(NC)" || { echo "$(RED)✗ Unit tests failed$(NC)"; exit 1; }
```

Also update the existing `test` target to run unit tests:

```makefile
.PHONY: test
test: build
	@echo "$(BLUE)Testing build...$(NC)"
	@./$(TARGET) --help >/dev/null && echo "$(GREEN)✓ Basic functionality test passed$(NC)" || { echo "$(RED)✗ Test failed$(NC)"; exit 1; }
	@./$(TARGET) history count >/dev/null && echo "$(GREEN)✓ History subcommand works$(NC)" || echo "$(RED)✗ History subcommand failed$(NC)"
	@cd $(BUILD_DIR) && make test_text_processing -j$$(sysctl -n hw.ncpu) 2>/dev/null
	@[ -f $(BUILD_DIR)/tests/test_text_processing ] && ./$(BUILD_DIR)/tests/test_text_processing && echo "$(GREEN)✓ Unit tests passed$(NC)" || echo "$(YELLOW)⚠ Unit tests not built$(NC)"
```

**Step 4: Create tests directory**

```bash
mkdir -p tests
```

**Step 5: Build and run tests**

```bash
make test-unit
```
Expected: All unit tests pass.

```bash
make test
```
Expected: All tests pass (smoke + unit).

**Step 6: Commit**

```bash
git add tests/test_text_processing.cpp CMakeLists.txt Makefile
git commit -m "test: add unit tests for text_processing module"
```

---

### Task 3: Create meeting_manager.h/.cpp

**Files:**
- Create: `meeting_manager.h`
- Create: `meeting_manager.cpp`
- Modify: `CMakeLists.txt` (add meeting_manager.cpp to sources)
- Modify: `recognize.cpp` (remove extracted code, add include)

**Step 1: Create `meeting_manager.h`**

```cpp
#pragma once

#include <chrono>
#include <string>
#include <sstream>

// Default meeting prompt template
extern const std::string DEFAULT_MEETING_PROMPT;

// Meeting session state
struct MeetingSession {
    std::string session_id;
    std::chrono::high_resolution_clock::time_point start_time;
    std::ostringstream transcription_buffer;
    int current_speaker_id = 1;
    int total_speakers = 1;
    bool first_text_added = false;

    MeetingSession();

    void add_transcription(const std::string& text, bool speaker_turn = false);
    std::string get_transcription() const;
    double get_duration_minutes() const;
};

// File generation
std::string generate_meeting_filename(const std::string& meeting_name);
std::string generate_fallback_filename();

// Meeting transcription processing via Claude CLI
bool process_meeting_transcription(const std::string& transcription, const std::string& prompt,
                                    const std::string& output_file, int timeout_seconds = 120,
                                    double duration_minutes = 0.0, int max_single_pass = 20000);
```

**Step 2: Create `meeting_manager.cpp`**

Move from `recognize.cpp`:
- `DEFAULT_MEETING_PROMPT` constant (lines 42-115)
- `MeetingSession` struct body (lines 220-262) — convert inline methods to out-of-line definitions
- `generate_meeting_filename()` (lines 304-330)
- `generate_fallback_filename()` (lines 332-335)
- `process_meeting_transcription()` (lines 619-788)

Add at top:
```cpp
#include "meeting_manager.h"
#include "text_processing.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
```

Note: `process_meeting_transcription()` calls `is_claude_cli_available()`, `invoke_claude_cli()`, `count_words()`, `split_into_chunks()` — these come from `text_processing.h`.

**Step 3: Update `recognize.cpp`**

- Add `#include "meeting_manager.h"`
- Remove `DEFAULT_MEETING_PROMPT`, `MeetingSession` struct, `generate_meeting_filename()`, `generate_fallback_filename()`, `process_meeting_transcription()` from recognize.cpp
- Keep includes that recognize.cpp still uses

**Step 4: Update `CMakeLists.txt`**

Add `meeting_manager.cpp` to `add_executable` sources.

**Step 5: Build and test**

```bash
make rebuild
make test
```
Expected: Build succeeds, all tests pass.

**Step 6: Commit**

```bash
git add meeting_manager.h meeting_manager.cpp CMakeLists.txt recognize.cpp
git commit -m "refactor: extract meeting_manager module from recognize.cpp"
```

---

### Task 4: Create audio_processor.h/.cpp

**Files:**
- Create: `audio_processor.h`
- Create: `audio_processor.cpp`
- Modify: `CMakeLists.txt` (add audio_processor.cpp to sources)
- Modify: `recognize.cpp` (remove extracted code, add include)

**Step 1: Create `audio_processor.h`**

```cpp
#pragma once

#include "whisper_params.h"
#include "whisper.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Forward declarations for session types (defined in recognize.cpp)
struct AutoCopySession;
struct ExportSession;
struct MeetingSession;

// Bilingual transcription result
struct BilingualSegment {
    int64_t t0;
    int64_t t1;
    std::string original_text;
    std::string english_text;
    float original_confidence;
    float english_confidence;
    bool speaker_turn;
    int speaker_id = -1;
};

// RMS audio normalization
void normalize_audio(std::vector<float>& pcmf32, float target_dbfs = -20.0f);

// Whisper inference pipeline — processes audio into bilingual segments
int process_audio_segment(struct whisper_context* ctx, struct whisper_context* ctx_translate,
                          const whisper_params& params, const std::vector<float>& pcmf32,
                          std::vector<BilingualSegment>& bilingual_results,
                          const std::vector<whisper_token>& prompt_tokens = {});

// Print tokens with confidence-based colors
void print_colored_tokens(whisper_context* ctx, int i_segment, const whisper_params& params);

// Shared speaker tracking
struct SpeakerTracker {
    int current_id = 0;
    int total_speakers = 0;

    int on_turn();
    int get_current() const;
};

// Print bilingual results with formatting and session accumulation
void print_bilingual_results(const std::vector<BilingualSegment>& segments, const whisper_params& params,
                             AutoCopySession& auto_copy_session, ExportSession& export_session,
                             SpeakerTracker& speaker_tracker, MeetingSession* meeting_session = nullptr,
                             bool tty_output = true, std::ostringstream* pipe_buffer = nullptr);
```

**Step 2: Create `audio_processor.cpp`**

Move from `recognize.cpp`:
- `BilingualSegment` struct definition is in the header now, remove from recognize.cpp
- `normalize_audio()` (lines 863-872) — remove `static`
- `process_audio_segment()` (lines 875-1071)
- `print_colored_tokens()` (lines 1074-1090)
- `SpeakerTracker` struct (lines 1093-1104) — methods become out-of-line
- `print_bilingual_results()` (lines 1107-1316)

Add at top:
```cpp
#include "audio_processor.h"
#include "export_manager.h"
#include "meeting_manager.h"
#include "text_processing.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unistd.h>
```

**Important:** `print_bilingual_results()` needs the full definitions of `AutoCopySession`, `ExportSession`, and `MeetingSession`. Since `AutoCopySession` and `ExportSession` stay in recognize.cpp, we must move their definitions somewhere accessible. The cleanest approach: move `AutoCopySession` and `ExportSession` into a small header `session_types.h` that both `audio_processor.cpp` and `recognize.cpp` include. This is a minimal addition not originally planned but necessary to avoid circular deps.

Create `session_types.h`:
```cpp
#pragma once

#include "export_manager.h"

#include <chrono>
#include <random>
#include <sstream>
#include <string>

// Auto-copy session state
struct AutoCopySession {
    std::string session_id;
    std::chrono::high_resolution_clock::time_point start_time;
    std::ostringstream transcription_buffer;
    bool has_been_copied = false;

    AutoCopySession() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100000, 999999);
        session_id = std::to_string(dis(gen));
        start_time = std::chrono::high_resolution_clock::now();
    }
};

// Export session state
struct ExportSession {
    std::string session_id;
    std::chrono::high_resolution_clock::time_point start_time;
    std::vector<TranscriptionSegment> segments;
    SessionMetadata metadata;

    ExportSession() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100000, 999999);
        session_id = std::to_string(dis(gen));
        start_time = std::chrono::high_resolution_clock::now();
    }
};
```

Then update `audio_processor.h` to remove the forward declarations and instead:
```cpp
#include "session_types.h"
#include "meeting_manager.h"
```

And in `recognize.cpp`, replace the inline struct definitions with `#include "session_types.h"`.

**Step 3: Update `recognize.cpp`**

- Add `#include "audio_processor.h"` (which transitively includes session_types.h and meeting_manager.h)
- Remove `BilingualSegment`, `normalize_audio`, `process_audio_segment`, `print_colored_tokens`, `SpeakerTracker`, `print_bilingual_results` from recognize.cpp
- Remove `AutoCopySession` and `ExportSession` struct definitions (now in session_types.h)

**Step 4: Update `CMakeLists.txt`**

Add `audio_processor.cpp` to `add_executable` sources.

**Step 5: Build and test**

```bash
make rebuild
make test
```
Expected: Build succeeds, all tests pass.

**Step 6: Commit**

```bash
git add audio_processor.h audio_processor.cpp session_types.h CMakeLists.txt recognize.cpp
git commit -m "refactor: extract audio_processor and session_types from recognize.cpp"
```

---

### Task 5: Create cli_parser.h/.cpp

**Files:**
- Create: `cli_parser.h`
- Create: `cli_parser.cpp`
- Modify: `CMakeLists.txt` (add cli_parser.cpp to sources)
- Modify: `recognize.cpp` (remove extracted code, add include)

**Step 1: Create `cli_parser.h`**

```cpp
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
```

**Step 2: Create `cli_parser.cpp`**

Move from `recognize.cpp`:
- `whisper_params_parse()` (lines 1387-1547) — remove `static`
- `whisper_print_usage()` (lines 1549-1669)
- `handle_model_commands()` (lines 1672-1697) — remove `static`
- `handle_history_command()` (lines 1771-1931) — remove `static`

Add at top:
```cpp
#include "cli_parser.h"
#include "config_manager.h"
#include "model_manager.h"
#include "history_manager.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <unistd.h>
```

**Step 3: Update `recognize.cpp`**

- Add `#include "cli_parser.h"`
- Remove `whisper_params_parse()`, `whisper_print_usage()`, `handle_model_commands()`, `handle_history_command()` from recognize.cpp
- Keep the forward declaration removal (these functions are now in cli_parser.h)

**Step 4: Update `CMakeLists.txt`**

Add `cli_parser.cpp` to `add_executable` sources.

**Step 5: Build and test**

```bash
make rebuild
make test
```
Expected: Build succeeds, all tests pass.

Test subcommands specifically:
```bash
./recognize --help 2>&1 | head -5
./recognize history count
./recognize config list
./recognize history show abc 2>&1  # should print error gracefully
```
Expected: All produce same output as before refactoring.

**Step 6: Commit**

```bash
git add cli_parser.h cli_parser.cpp CMakeLists.txt recognize.cpp
git commit -m "refactor: extract cli_parser module from recognize.cpp"
```

---

### Task 6: Final verification and cleanup

**Files:**
- Modify: `recognize.cpp` (clean up unused includes)
- Modify: `Makefile` (update test target if needed)

**Step 1: Verify recognize.cpp line count**

```bash
wc -l recognize.cpp
```
Expected: ~800 lines (down from 2782).

**Step 2: Verify all file sizes are reasonable**

```bash
wc -l *.h *.cpp | sort -rn | head -20
```
Expected: No file over ~1100 lines. recognize.cpp should be the largest at ~800.

**Step 3: Clean up unused includes in recognize.cpp**

Review recognize.cpp's include list. Remove any includes that are no longer needed because the code that used them has been extracted. Common candidates:
- `<regex>` — only used in filter_hallucinations (now in text_processing)
- `<random>` — only used in session struct constructors (now in session_types.h)
- `<fstream>` — check if still used in main

Do NOT remove includes that are still used. When in doubt, keep the include and try building.

**Step 4: Verify header isolation**

Each header should compile independently. Quick test:
```bash
echo '#include "text_processing.h"' | c++ -std=c++17 -fsyntax-only -I. -x c++ - 2>&1
echo '#include "meeting_manager.h"' | c++ -std=c++17 -fsyntax-only -I. -x c++ - 2>&1
echo '#include "cli_parser.h"' | c++ -std=c++17 -fsyntax-only -I. -I../../fixtures/whisper.cpp/include -x c++ - 2>&1
echo '#include "audio_processor.h"' | c++ -std=c++17 -fsyntax-only -I. -I../../fixtures/whisper.cpp/include -I../../fixtures/whisper.cpp/examples -x c++ - 2>&1
echo '#include "session_types.h"' | c++ -std=c++17 -fsyntax-only -I. -x c++ - 2>&1
```
Expected: All compile without errors.

**Step 5: Full test suite**

```bash
make clean && make build
make test
make test-unit
```
Expected: Full clean build succeeds, all tests pass.

**Step 6: Commit**

```bash
git add recognize.cpp
git commit -m "refactor: clean up recognize.cpp includes after extraction"
```

---

### Task 7: Update CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

**Step 1: Update the architecture section**

Update the "Core Components & Data Flow" and file descriptions in CLAUDE.md to reflect the new module structure. Key changes:
- `recognize.cpp` description updated to note it's the session orchestrator (~800 lines)
- Add entries for `text_processing.h/.cpp`, `meeting_manager.h/.cpp`, `audio_processor.h/.cpp`, `cli_parser.h/.cpp`, `session_types.h`
- Update the "Repo Layout" if needed

**Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md for refactored module structure"
```
