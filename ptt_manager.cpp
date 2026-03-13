#include "ptt_manager.h"
#include <cstdio>
#include <algorithm>

// Modifier key virtual keycodes
static constexpr int kVK_RightOption  = 61;
static constexpr int kVK_RightControl = 62;
static constexpr int kVK_Function     = 63;
static constexpr int kVK_F13          = 105;
static constexpr int kVK_Space        = 49;

PushToTalkManager::PushToTalkManager() = default;

PushToTalkManager::~PushToTalkManager() {
    stop();
}

bool PushToTalkManager::is_key_held() const {
    return key_held_.load();
}

bool PushToTalkManager::is_running() const {
    return running_.load();
}

int PushToTalkManager::key_name_to_code(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "space")                                return kVK_Space;
    if (lower == "right_option" || lower == "right_alt") return kVK_RightOption;
    if (lower == "right_ctrl")                           return kVK_RightControl;
    if (lower == "fn")                                   return kVK_Function;
    if (lower == "f13")                                  return kVK_F13;
    return -1;
}

std::string PushToTalkManager::key_code_to_name(int code) {
    switch (code) {
        case kVK_Space:        return "space";
        case kVK_RightOption:  return "right_option";
        case kVK_RightControl: return "right_ctrl";
        case kVK_Function:     return "fn";
        case kVK_F13:          return "f13";
        default:               return "unknown(" + std::to_string(code) + ")";
    }
}

#ifdef __APPLE__

bool PushToTalkManager::check_permission() {
    return CGPreflightListenEventAccess();
}

bool PushToTalkManager::request_permission() {
    if (CGPreflightListenEventAccess()) return true;

    // Triggers system dialog — user must grant permission then restart
    CGRequestListenEventAccess();

    // Check again (may succeed if already granted but stale cache)
    if (CGPreflightListenEventAccess()) return true;

    fprintf(stderr, "PTT requires Input Monitoring permission.\n");
    fprintf(stderr, "Grant it to your terminal app in:\n");
    fprintf(stderr, "  System Settings > Privacy & Security > Input Monitoring\n");
    fprintf(stderr, "Then restart your terminal.\n");
    return false;
}

bool PushToTalkManager::start(int key_code) {
    if (running_.load()) return true;

    key_code_ = key_code;

    if (!request_permission()) {
        return false;
    }

    running_.store(true);
    tap_thread_ = std::thread(&PushToTalkManager::run_event_loop, this);
    return true;
}

void PushToTalkManager::stop() {
    if (!running_.load()) return;

    running_.store(false);
    key_held_.store(false);

    if (tap_runloop_) {
        CFRunLoopStop(tap_runloop_);
    }

    if (tap_thread_.joinable()) {
        tap_thread_.join();
    }

    if (event_tap_) {
        CGEventTapEnable(event_tap_, false);
        CFRelease(event_tap_);
        event_tap_ = nullptr;
    }
    tap_runloop_ = nullptr;
}

static bool is_modifier_key(int key_code) {
    return key_code == kVK_RightOption ||
           key_code == kVK_RightControl ||
           key_code == kVK_Function;
}

CGEventRef PushToTalkManager::event_callback(CGEventTapProxy /*proxy*/,
    CGEventType type, CGEventRef event, void* refcon) {

    auto* self = static_cast<PushToTalkManager*>(refcon);

    // Re-enable if system disabled due to timeout
    if (type == kCGEventTapDisabledByTimeout) {
        if (self->event_tap_) {
            CGEventTapEnable(self->event_tap_, true);
        }
        return event;
    }

    if (is_modifier_key(self->key_code_)) {
        // Modifier keys use kCGEventFlagsChanged
        if (type != kCGEventFlagsChanged) return event;

        CGEventFlags flags = CGEventGetFlags(event);
        bool held = false;

        switch (self->key_code_) {
            case kVK_RightOption:
                held = (flags & kCGEventFlagMaskAlternate) &&
                       (CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) == kVK_RightOption);
                break;
            case kVK_RightControl:
                held = (flags & kCGEventFlagMaskControl) &&
                       (CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) == kVK_RightControl);
                break;
            case kVK_Function:
                held = (flags & kCGEventFlagMaskSecondaryFn) != 0;
                break;
        }

        self->key_held_.store(held);
        return event;  // Don't consume modifier events
    }

    // Regular keys: keyDown / keyUp
    if (type != kCGEventKeyDown && type != kCGEventKeyUp) return event;

    int keycode = static_cast<int>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    if (keycode != self->key_code_) return event;

    // Filter auto-repeat
    bool is_repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0;
    if (is_repeat) return nullptr;  // Consume repeat events

    self->key_held_.store(type == kCGEventKeyDown);

    return nullptr;  // Consume the PTT key event
}

void PushToTalkManager::run_event_loop() {
    CGEventMask mask = (1 << kCGEventKeyDown) | (1 << kCGEventKeyUp) | (1 << kCGEventFlagsChanged);

    event_tap_ = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,  // active tap (can consume events)
        mask,
        event_callback,
        this
    );

    if (!event_tap_) {
        fprintf(stderr, "Failed to create event tap. Check Input Monitoring permissions.\n");
        running_.store(false);
        return;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap_, 0);
    tap_runloop_ = CFRunLoopGetCurrent();
    CFRunLoopAddSource(tap_runloop_, source, kCFRunLoopCommonModes);
    CGEventTapEnable(event_tap_, true);

    CFRelease(source);

    // Blocks until CFRunLoopStop() is called from stop()
    CFRunLoopRun();
}

#else
// Non-Apple stubs
bool PushToTalkManager::check_permission() { return false; }
bool PushToTalkManager::request_permission() {
    fprintf(stderr, "PTT mode is only supported on macOS\n");
    return false;
}
bool PushToTalkManager::start(int) { return false; }
void PushToTalkManager::stop() {}
#endif
