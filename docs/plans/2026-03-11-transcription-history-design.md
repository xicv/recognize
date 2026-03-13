# Transcription History Feature Design

**Date:** 2026-03-11
**Status:** Approved
**Scope:** CLI history feature + Claude Code skill

## Problem

Voice transcriptions in the `recognize` CLI are ephemeral. Session files (`~/.recognize/claude-session.txt`) are deleted after each `/rs` invocation. Users cannot retrieve, search, or reuse past transcriptions.

## Solution

Add a persistent history feature with three layers:

1. **CLI `history_manager`** (C++) — SQLite + FTS5 storage, auto-save on session end
2. **CLI subcommands** — `recognize history list/search/show/clear/count`
3. **Claude Code skill** — `/recognize-history` (alias `/rh`) for querying from Claude

## Architecture

### Layer 1: `history_manager.h/.cpp`

New manager following the existing `*_manager` pattern.

**Storage:** `~/.recognize/history.db` (SQLite3 with WAL mode)

**Schema:**

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;

CREATE TABLE IF NOT EXISTS transcripts (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now','localtime')),
    duration_s REAL,
    model      TEXT,
    mode       TEXT,       -- "auto-stop", "ptt", "continuous", "meeting"
    word_count INTEGER,
    text       TEXT NOT NULL
);

CREATE VIRTUAL TABLE IF NOT EXISTS transcripts_fts USING fts5(
    text,
    content='transcripts',
    content_rowid='id',
    tokenize='porter unicode61'
);

CREATE TRIGGER IF NOT EXISTS tr_ai AFTER INSERT ON transcripts BEGIN
    INSERT INTO transcripts_fts(rowid, text) VALUES (new.id, new.text);
END;

CREATE TRIGGER IF NOT EXISTS tr_ad AFTER DELETE ON transcripts BEGIN
    INSERT INTO transcripts_fts(transcripts_fts, rowid, text) VALUES('delete', old.id, old.text);
END;

CREATE INDEX IF NOT EXISTS idx_transcripts_ts ON transcripts(timestamp DESC);
```

**Class interface:**

```cpp
class HistoryManager {
public:
    HistoryManager();
    ~HistoryManager();

    bool open(const std::string& db_path = "");  // defaults to ~/.recognize/history.db
    void close();

    // Write
    int64_t save(const std::string& text, double duration_s,
                 const std::string& model, const std::string& mode);

    // Read
    struct Entry {
        int64_t id;
        std::string timestamp;
        double duration_s;
        std::string model;
        std::string mode;
        int word_count;
        std::string text;
    };

    std::vector<Entry> list(int limit = 20, int offset = 0);
    std::vector<Entry> search(const std::string& query, int limit = 20,
                              const std::string& since = "");
    std::optional<Entry> get(int64_t id);

    // Manage
    int clear_older_than(int days);
    int clear_all();
    int count();

    // Output formatting
    std::string format_table(const std::vector<Entry>& entries);
    std::string format_json(const std::vector<Entry>& entries);

private:
    sqlite3* db_ = nullptr;
    bool ensure_schema();
    int count_words(const std::string& text);
};
```

**Dependencies:** macOS system `libsqlite3.dylib` (always present, zero external deps).

### Layer 2: CLI Subcommands

**Detection:** In `main()`, before any whisper/audio initialization:

```cpp
if (argc >= 2 && std::string(argv[1]) == "history") {
    return handle_history_command(argc - 1, argv + 1);
}
```

**Subcommands:**

| Command | Description |
|---------|-------------|
| `recognize history` | Alias for `recognize history list` |
| `recognize history list [--limit N] [--offset N] [--json]` | Recent transcripts |
| `recognize history search <query> [--limit N] [--since Nd] [--json]` | FTS5 search with BM25 ranking |
| `recognize history show <id> [--json]` | Full transcript by ID |
| `recognize history clear [--older-than Nd] [--all]` | Delete entries with confirmation |
| `recognize history count` | Total entry count |

**Output format:** Follows existing `isatty(STDOUT_FILENO)` pattern:
- **TTY:** Pretty table with truncated preview (60 chars), relative timestamps
- **Pipe/`--json`:** JSON array

**Auto-save:** In `finalize_session()`, alongside existing auto-copy and export:

```cpp
if (params.history_enabled) {
    HistoryManager history;
    if (history.open()) {
        std::string text = /* final transcript */;
        if (!text.empty()) {
            double duration = /* session duration */;
            std::string mode = params.ptt_mode ? "ptt" :
                               params.meeting_mode ? "meeting" :
                               (params.silence_timeout > 0) ? "auto-stop" : "continuous";
            history.save(text, duration, params.model, mode);
        }
    }
}
```

**New params:**
- `whisper_params.history_enabled` (default: `true`)
- CLI flag: `--no-history` to disable for a single run
- Config key: `history.enabled` in `config_manager`

### Layer 3: Claude Code Skill

**Skill:** `~/.claude/commands/recognize-history.md`

Uses `!`command`` injection to call the CLI's history subcommand. The skill:
- Shows recent transcriptions via `recognize history list --limit 5 --json`
- Accepts `$ARGUMENTS` for search queries
- Presents results in readable format

**Alias:** `~/.claude/commands/rh.md` pointing to recognize-history

**No hook needed:** Auto-save is handled natively in the C++ binary's `finalize_session()`. No PostToolUse hook is required.

**No auto-injection:** Transcription history is queried explicitly via `/rh`, not injected into every prompt. This keeps prompts clean and avoids unnecessary context consumption.

## Build Changes

**CMakeLists.txt additions:**
- Add `history_manager.cpp` to sources
- Find and link `sqlite3`: `find_library(SQLITE3_LIBRARY sqlite3 REQUIRED)`
- Add to `target_link_libraries`

## File Changes Summary

| File | Change |
|------|--------|
| `history_manager.h` | **New** — class declaration |
| `history_manager.cpp` | **New** — SQLite implementation, subcommand handler |
| `whisper_params.h` | Add `history_enabled` field |
| `recognize.cpp` | Subcommand detection in `main()`, save in `finalize_session()` |
| `config_manager.h` | Add `history_enabled` to `ConfigData` |
| `config_manager.cpp` | Parse/serialize `history.enabled` config key |
| `CMakeLists.txt` | Add `history_manager.cpp`, link `sqlite3` |
| `~/.claude/commands/recognize-history.md` | **New** — skill |
| `~/.claude/commands/rh.md` | **New** — alias |

## Size Management

- **Max entries:** 10,000 (pruned on save)
- **Max age:** 90 days (configurable)
- DB auto-prune runs on each `save()` call (lightweight — just a DELETE + count check)

## Testing

- `make test` extended with history subcommand smoke test
- Manual: `recognize history list`, `recognize history search "test"`, `recognize history show 1`
