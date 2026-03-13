#pragma once

#include <atomic>
#include <thread>
#include <string>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

class PushToTalkManager {
public:
    PushToTalkManager();
    ~PushToTalkManager();

    // Initialize the event tap on a background thread
    // Returns false if permission denied
    bool start(int key_code = 49);  // 49 = spacebar
    void stop();

    // State queries (thread-safe)
    bool is_key_held() const;
    bool is_running() const;

    // Permission helpers
    static bool check_permission();
    static bool request_permission();

    // Key name <-> keycode mapping
    static int key_name_to_code(const std::string& name);
    static std::string key_code_to_name(int code);

private:
    std::atomic<bool> key_held_{false};
    std::atomic<bool> running_{false};
    std::thread tap_thread_;

#ifdef __APPLE__
    CFRunLoopRef tap_runloop_{nullptr};
    CFMachPortRef event_tap_{nullptr};
#endif

    int key_code_{49};

    void run_event_loop();

#ifdef __APPLE__
    static CGEventRef event_callback(CGEventTapProxy proxy,
        CGEventType type, CGEventRef event, void* refcon);
#endif
};
