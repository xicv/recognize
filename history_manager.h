#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

struct sqlite3;

class HistoryManager {
public:
    HistoryManager();
    ~HistoryManager();

    bool open(const std::string& db_path = "");
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
    static std::string format_table(const std::vector<Entry>& entries);
    static std::string format_json(const std::vector<Entry>& entries);
    static std::string format_entry_json(const Entry& entry);

private:
    sqlite3* db_ = nullptr;
    std::string db_path_;
    bool has_fts_ = false;

    bool ensure_schema();
    void auto_prune();
    static int count_words(const std::string& text);
    static std::string relative_time(const std::string& iso_timestamp);
    static std::string truncate_text(const std::string& text, size_t max_len);
    static std::string escape_json(const std::string& str);
};
