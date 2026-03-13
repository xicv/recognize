#include "history_manager.h"

#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <cmath>

namespace fs = std::filesystem;

static const int MAX_ENTRIES = 10000;
static const int AUTO_PRUNE_DAYS = 90;

// Safe column text extraction — handles NULL without crashing
static std::string col_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

HistoryManager::HistoryManager() = default;

HistoryManager::~HistoryManager() {
    close();
}

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

bool HistoryManager::open(const std::string& db_path) {
    if (db_ != nullptr) {
        close();
    }

    if (db_path.empty()) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "[history] HOME not set\n";
            return false;
        }
        db_path_ = std::string(home) + "/.recognize/history.db";
    } else {
        db_path_ = db_path;
    }

    // Create parent directories
    fs::path parent = fs::path(db_path_).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            std::cerr << "[history] cannot create directory " << parent
                      << ": " << ec.message() << "\n";
            return false;
        }
    }

    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[history] cannot open database: "
                  << sqlite3_errmsg(db_) << "\n";
        sqlite3_close_v2(db_);
        db_ = nullptr;
        return false;
    }

    // WAL mode + NORMAL sync for performance
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    if (!ensure_schema()) {
        close();
        return false;
    }

    return true;
}

void HistoryManager::close() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

bool HistoryManager::ensure_schema() {
    // Core table — must succeed
    const char* core_sql = R"(
        CREATE TABLE IF NOT EXISTS transcripts (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now','localtime')),
            duration_s REAL,
            model      TEXT,
            mode       TEXT,
            word_count INTEGER,
            text       TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_transcripts_ts ON transcripts(timestamp DESC);
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, core_sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[history] schema error: " << (err ? err : "unknown") << "\n";
        sqlite3_free(err);
        return false;
    }

    // FTS5 — optional, gracefully degrade to LIKE search if unavailable
    const char* fts_sql = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS transcripts_fts USING fts5(
            text, content='transcripts', content_rowid='id', tokenize='porter unicode61'
        );
        CREATE TRIGGER IF NOT EXISTS tr_ai AFTER INSERT ON transcripts BEGIN
            INSERT INTO transcripts_fts(rowid, text) VALUES (new.id, new.text);
        END;
        CREATE TRIGGER IF NOT EXISTS tr_ad AFTER DELETE ON transcripts BEGIN
            INSERT INTO transcripts_fts(transcripts_fts, rowid, text) VALUES('delete', old.id, old.text);
        END;
    )";

    err = nullptr;
    rc = sqlite3_exec(db_, fts_sql, nullptr, nullptr, &err);
    if (rc == SQLITE_OK) {
        has_fts_ = true;
    } else {
        std::cerr << "[history] FTS5 unavailable, using basic search: "
                  << (err ? err : "unknown") << "\n";
        sqlite3_free(err);
        has_fts_ = false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

int64_t HistoryManager::save(const std::string& text, double duration_s,
                             const std::string& model, const std::string& mode) {
    if (db_ == nullptr || text.empty()) {
        return -1;
    }

    const char* sql =
        "INSERT INTO transcripts (duration_s, model, mode, word_count, text) "
        "VALUES (?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[history] save prepare error: " << sqlite3_errmsg(db_) << "\n";
        return -1;
    }

    int wc = count_words(text);
    sqlite3_bind_double(stmt, 1, duration_s);
    sqlite3_bind_text(stmt, 2, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, wc);
    sqlite3_bind_text(stmt, 5, text.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "[history] save error: " << sqlite3_errmsg(db_) << "\n";
        return -1;
    }

    int64_t row_id = sqlite3_last_insert_rowid(db_);
    auto_prune();
    return row_id;
}

// ---------------------------------------------------------------------------
// auto_prune
// ---------------------------------------------------------------------------

void HistoryManager::auto_prune() {
    if (db_ == nullptr) {
        return;
    }

    // Delete entries older than AUTO_PRUNE_DAYS (parameterized)
    const char* age_sql =
        "DELETE FROM transcripts WHERE timestamp < "
        "strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime', ? || ' days');";
    sqlite3_stmt* age_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, age_sql, -1, &age_stmt, nullptr) == SQLITE_OK) {
        std::string modifier = "-" + std::to_string(AUTO_PRUNE_DAYS);
        sqlite3_bind_text(age_stmt, 1, modifier.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(age_stmt);
        sqlite3_finalize(age_stmt);
    }

    // Enforce count limit
    const char* count_sql =
        "DELETE FROM transcripts WHERE id NOT IN "
        "(SELECT id FROM transcripts ORDER BY timestamp DESC LIMIT ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, count_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, MAX_ENTRIES);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// ---------------------------------------------------------------------------
// list
// ---------------------------------------------------------------------------

std::vector<HistoryManager::Entry> HistoryManager::list(int limit, int offset) {
    std::vector<Entry> results;
    if (db_ == nullptr) {
        return results;
    }

    const char* sql =
        "SELECT id, timestamp, duration_s, model, mode, word_count, text "
        "FROM transcripts ORDER BY timestamp DESC LIMIT ? OFFSET ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[history] list error: " << sqlite3_errmsg(db_) << "\n";
        return results;
    }

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Entry e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.timestamp = col_text(stmt, 1);
        e.duration_s = sqlite3_column_double(stmt, 2);
        e.model = col_text(stmt, 3);
        e.mode = col_text(stmt, 4);
        e.word_count = sqlite3_column_int(stmt, 5);
        e.text = col_text(stmt, 6);
        results.push_back(std::move(e));
    }

    sqlite3_finalize(stmt);
    return results;
}

// ---------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------

std::vector<HistoryManager::Entry> HistoryManager::search(
        const std::string& query, int limit, const std::string& since) {
    std::vector<Entry> results;
    if (db_ == nullptr || query.empty()) {
        return results;
    }

    std::string sql;
    std::string like_query = "%" + query + "%";

    if (has_fts_) {
        if (since.empty()) {
            sql = "SELECT t.id, t.timestamp, t.duration_s, t.model, t.mode, "
                  "t.word_count, t.text "
                  "FROM transcripts_fts f "
                  "JOIN transcripts t ON t.id = f.rowid "
                  "WHERE transcripts_fts MATCH ? "
                  "ORDER BY bm25(transcripts_fts) "
                  "LIMIT ?;";
        } else {
            sql = "SELECT t.id, t.timestamp, t.duration_s, t.model, t.mode, "
                  "t.word_count, t.text "
                  "FROM transcripts_fts f "
                  "JOIN transcripts t ON t.id = f.rowid "
                  "WHERE transcripts_fts MATCH ? AND t.timestamp >= ? "
                  "ORDER BY bm25(transcripts_fts) "
                  "LIMIT ?;";
        }
    } else {
        // Fallback: LIKE-based search when FTS5 is unavailable
        if (since.empty()) {
            sql = "SELECT id, timestamp, duration_s, model, mode, "
                  "word_count, text "
                  "FROM transcripts "
                  "WHERE text LIKE ? "
                  "ORDER BY timestamp DESC "
                  "LIMIT ?;";
        } else {
            sql = "SELECT id, timestamp, duration_s, model, mode, "
                  "word_count, text "
                  "FROM transcripts "
                  "WHERE text LIKE ? AND timestamp >= ? "
                  "ORDER BY timestamp DESC "
                  "LIMIT ?;";
        }
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[history] search error: " << sqlite3_errmsg(db_) << "\n";
        return results;
    }

    const std::string& bind_val = has_fts_ ? query : like_query;
    sqlite3_bind_text(stmt, 1, bind_val.c_str(), -1, SQLITE_TRANSIENT);
    if (since.empty()) {
        sqlite3_bind_int(stmt, 2, limit);
    } else {
        sqlite3_bind_text(stmt, 2, since.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Entry e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.timestamp = col_text(stmt, 1);
        e.duration_s = sqlite3_column_double(stmt, 2);
        e.model = col_text(stmt, 3);
        e.mode = col_text(stmt, 4);
        e.word_count = sqlite3_column_int(stmt, 5);
        e.text = col_text(stmt, 6);
        results.push_back(std::move(e));
    }

    sqlite3_finalize(stmt);
    return results;
}

// ---------------------------------------------------------------------------
// get
// ---------------------------------------------------------------------------

std::optional<HistoryManager::Entry> HistoryManager::get(int64_t id) {
    if (db_ == nullptr) {
        return std::nullopt;
    }

    const char* sql =
        "SELECT id, timestamp, duration_s, model, mode, word_count, text "
        "FROM transcripts WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[history] get error: " << sqlite3_errmsg(db_) << "\n";
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);

    std::optional<Entry> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Entry e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.timestamp = col_text(stmt, 1);
        e.duration_s = sqlite3_column_double(stmt, 2);
        e.model = col_text(stmt, 3);
        e.mode = col_text(stmt, 4);
        e.word_count = sqlite3_column_int(stmt, 5);
        e.text = col_text(stmt, 6);
        result = std::move(e);
    }

    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// clear_older_than
// ---------------------------------------------------------------------------

int HistoryManager::clear_older_than(int days) {
    if (db_ == nullptr || days <= 0) {
        return 0;
    }

    const char* sql =
        "DELETE FROM transcripts WHERE timestamp < "
        "strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime', ? || ' days');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    std::string modifier = "-" + std::to_string(days);
    sqlite3_bind_text(stmt, 1, modifier.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int changed = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return changed;
}

// ---------------------------------------------------------------------------
// clear_all
// ---------------------------------------------------------------------------

int HistoryManager::clear_all() {
    if (db_ == nullptr) {
        return 0;
    }

    sqlite3_exec(db_, "DELETE FROM transcripts;", nullptr, nullptr, nullptr);
    int deleted = sqlite3_changes(db_);
    if (has_fts_) {
        sqlite3_exec(db_,
            "INSERT INTO transcripts_fts(transcripts_fts) VALUES('rebuild');",
            nullptr, nullptr, nullptr);
    }
    return deleted;
}

// ---------------------------------------------------------------------------
// count
// ---------------------------------------------------------------------------

int HistoryManager::count() {
    if (db_ == nullptr) {
        return 0;
    }

    const char* sql = "SELECT COUNT(*) FROM transcripts;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int HistoryManager::count_words(const std::string& text) {
    int wc = 0;
    bool in_word = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++wc;
        }
    }
    return wc;
}

std::string HistoryManager::relative_time(const std::string& iso_timestamp) {
    // Parse ISO timestamp: YYYY-MM-DDTHH:MM:SS
    std::tm tm = {};
    std::istringstream ss(iso_timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        return iso_timestamp;
    }

    std::time_t entry_time = std::mktime(&tm);
    std::time_t now = std::time(nullptr);
    double diff = std::difftime(now, entry_time);

    if (diff < 0) {
        return "just now";
    }

    int seconds = static_cast<int>(diff);
    if (seconds < 60) {
        return "just now";
    }

    int minutes = seconds / 60;
    if (minutes < 60) {
        return std::to_string(minutes) + " min ago";
    }

    int hours = minutes / 60;
    if (hours < 24) {
        return std::to_string(hours) + "h ago";
    }

    int days = hours / 24;
    if (days < 30) {
        return std::to_string(days) + "d ago";
    }

    int months = days / 30;
    if (months < 12) {
        return std::to_string(months) + "mo ago";
    }

    int years = months / 12;
    return std::to_string(years) + "y ago";
}

std::string HistoryManager::truncate_text(const std::string& text, size_t max_len) {
    std::string result;
    result.reserve(std::min(text.size(), max_len + 3));

    for (size_t i = 0; i < text.size() && result.size() < max_len; ++i) {
        char c = text[i];
        if (c == '\n' || c == '\r') {
            result += ' ';
        } else {
            result += c;
        }
    }

    if (text.size() > max_len) {
        // Trim trailing space before appending ellipsis
        while (!result.empty() && result.back() == ' ') {
            result.pop_back();
        }
        result += "...";
    }

    return result;
}

std::string HistoryManager::escape_json(const std::string& str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// format_table
// ---------------------------------------------------------------------------

std::string HistoryManager::format_table(const std::vector<Entry>& entries) {
    if (entries.empty()) {
        return "No transcription history found.\n";
    }

    // Column widths
    const int col_id    = 5;
    const int col_when  = 12;
    const int col_mode  = 10;
    const int col_model = 18;
    const int col_words = 6;
    const int col_preview = 60;

    std::ostringstream out;

    // Header
    out << std::left
        << std::setw(col_id)      << "ID"
        << std::setw(col_when)    << "When"
        << std::setw(col_mode)    << "Mode"
        << std::setw(col_model)   << "Model"
        << std::setw(col_words)   << "Words"
        << "Preview"
        << "\n";

    // Separator
    out << std::string(col_id, '-')
        << std::string(col_when, '-')
        << std::string(col_mode, '-')
        << std::string(col_model, '-')
        << std::string(col_words, '-')
        << std::string(col_preview, '-')
        << "\n";

    // Rows
    for (const auto& e : entries) {
        std::string when = truncate_text(relative_time(e.timestamp), col_when - 1);
        std::string preview = truncate_text(e.text, col_preview);
        std::string model_short = truncate_text(e.model, col_model - 2);

        out << std::left
            << std::setw(col_id)      << e.id
            << std::setw(col_when)    << when
            << std::setw(col_mode)    << (e.mode.empty() ? "-" : e.mode)
            << std::setw(col_model)   << (model_short.empty() ? "-" : model_short)
            << std::setw(col_words)   << e.word_count
            << preview
            << "\n";
    }

    return out.str();
}

// ---------------------------------------------------------------------------
// format_json / format_entry_json
// ---------------------------------------------------------------------------

std::string HistoryManager::format_entry_json(const Entry& entry) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << entry.id << ","
        << "\"timestamp\":\"" << escape_json(entry.timestamp) << "\","
        << "\"duration_s\":" << std::fixed << std::setprecision(1) << entry.duration_s << ","
        << "\"model\":\"" << escape_json(entry.model) << "\","
        << "\"mode\":\"" << escape_json(entry.mode) << "\","
        << "\"word_count\":" << entry.word_count << ","
        << "\"text\":\"" << escape_json(entry.text) << "\""
        << "}";
    return out.str();
}

std::string HistoryManager::format_json(const std::vector<Entry>& entries) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << format_entry_json(entries[i]);
    }
    out << "]";
    return out.str();
}
