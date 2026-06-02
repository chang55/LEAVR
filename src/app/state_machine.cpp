#include "app/state_machine.h"
#include "utils/logger.h"

namespace leavr {

const char* StateMachine::GetStateName(DeviceState state) {
    static const char* names[] = {
        "BOOT", "STANDBY", "RECORDING", "PAUSED",
        "CAPTURING", "PLAYBACK", "MENU", "USB", "SUSPEND"
    };
    if (state < DEVICE_STATE_BOOT || state > DEVICE_STATE_SUSPEND) return "UNKNOWN";
    return names[state];
}

const char* StateMachine::GetEventName(EventType event) {
    static const char* names[] = {
        "INIT_DONE", "KEY_RECORD", "KEY_CAPTURE", "KEY_STOP",
        "KEY_PAUSE", "KEY_RESUME", "KEY_PLAYBACK", "KEY_MENU",
        "KEY_BACK", "KEY_UP", "KEY_DOWN", "KEY_OK",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        "USB_CONNECT", "USB_DISCONNECT",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        "BATTERY_LOW", "BATTERY_CRITICAL",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        "SDCARD_FULL", "SDCARD_REMOVED",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        "TIMER_TIMEOUT", "CAPTURE_DONE", "SEGMENT_TIMER", "OVERRUN_TIMER",
        "TIMED_START", "TIMED_STOP",
        nullptr, nullptr, nullptr,
        "NETWORK_UP", "NETWORK_DOWN",
        0
    };
    if (event < 0 || event > 100) return "UNKNOWN";
    return names[event] ? names[event] : "INTERNAL";
}

StateMachine::StateMachine() : state_(DEVICE_STATE_BOOT) {}

StateMachine::~StateMachine() {}

int StateMachine::Init() {
    // 注册核心转移
    // BOOT:
    RegisterTransition(DEVICE_STATE_BOOT, EVENT_INIT_DONE, DEVICE_STATE_STANDBY, nullptr);

    // STANDBY 下的操作:
    RegisterTransition(DEVICE_STATE_STANDBY, EVENT_KEY_RECORD, DEVICE_STATE_RECORDING, nullptr);
    RegisterTransition(DEVICE_STATE_STANDBY, EVENT_KEY_CAPTURE, DEVICE_STATE_CAPTURING, nullptr);
    RegisterTransition(DEVICE_STATE_STANDBY, EVENT_KEY_PLAYBACK, DEVICE_STATE_PLAYBACK, nullptr);
    RegisterTransition(DEVICE_STATE_STANDBY, EVENT_KEY_MENU, DEVICE_STATE_MENU, nullptr);
    RegisterTransition(DEVICE_STATE_STANDBY, EVENT_USB_CONNECT, DEVICE_STATE_USB, nullptr);
    RegisterTransition(DEVICE_STATE_STANDBY, EVENT_TIMER_TIMEOUT, DEVICE_STATE_SUSPEND, nullptr);
    RegisterTransition(DEVICE_STATE_STANDBY, EVENT_SDCARD_FULL, DEVICE_STATE_STANDBY, nullptr);

    // RECORDING:
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_KEY_STOP, DEVICE_STATE_STANDBY, nullptr);
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_KEY_PAUSE, DEVICE_STATE_PAUSED, nullptr);
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_BATTERY_LOW, DEVICE_STATE_STANDBY, nullptr);
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_BATTERY_CRITICAL, DEVICE_STATE_STANDBY, nullptr);
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_SDCARD_FULL, DEVICE_STATE_STANDBY, nullptr);
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_TIMED_STOP, DEVICE_STATE_STANDBY, nullptr);
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_SEGMENT_TIMER, DEVICE_STATE_RECORDING, nullptr);
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_OVERRUN_TIMER, DEVICE_STATE_RECORDING, nullptr);

    // PAUSED:
    RegisterTransition(DEVICE_STATE_PAUSED, EVENT_KEY_RESUME, DEVICE_STATE_RECORDING, nullptr);
    RegisterTransition(DEVICE_STATE_PAUSED, EVENT_KEY_STOP, DEVICE_STATE_STANDBY, nullptr);
    RegisterTransition(DEVICE_STATE_PAUSED, EVENT_BATTERY_CRITICAL, DEVICE_STATE_STANDBY, nullptr);

    // CAPTURING:
    RegisterTransition(DEVICE_STATE_CAPTURING, EVENT_CAPTURE_DONE, DEVICE_STATE_STANDBY, nullptr);

    // PLAYBACK:
    RegisterTransition(DEVICE_STATE_PLAYBACK, EVENT_KEY_BACK, DEVICE_STATE_STANDBY, nullptr);

    // MENU:
    RegisterTransition(DEVICE_STATE_MENU, EVENT_KEY_BACK, DEVICE_STATE_STANDBY, nullptr);

    // USB:
    RegisterTransition(DEVICE_STATE_USB, EVENT_USB_DISCONNECT, DEVICE_STATE_STANDBY, nullptr);

    // SUSPEND:
    RegisterTransition(DEVICE_STATE_SUSPEND, EVENT_KEY_RECORD, DEVICE_STATE_STANDBY, nullptr);
    RegisterTransition(DEVICE_STATE_SUSPEND, EVENT_USB_CONNECT, DEVICE_STATE_STANDBY, nullptr);

    // 全局: 任意状态到 USB
    RegisterTransition(DEVICE_STATE_RECORDING, EVENT_USB_CONNECT, DEVICE_STATE_USB, nullptr);
    RegisterTransition(DEVICE_STATE_PLAYBACK, EVENT_USB_CONNECT, DEVICE_STATE_USB, nullptr);
    RegisterTransition(DEVICE_STATE_MENU, EVENT_USB_CONNECT, DEVICE_STATE_USB, nullptr);

    initialized_ = true;
    LOG_INFO("StateMachine initialized with %zu transitions", transitions_.size());
    return LEAVR_OK;
}

void StateMachine::RegisterTransition(DeviceState from, EventType event,
                                       DeviceState to, StateAction action) {
    transitions_[{from, event}] = {to, action};
}

int StateMachine::PostEvent(EventType event) {
    pthread_mutex_lock(&lock_);
    DeviceState current = state_.load(std::memory_order_acquire);

    // 查找转移
    auto it = transitions_.find({current, event});
    if (it == transitions_.end()) {
        LOG_DEBUG("SM: Event %s in state %s - no transition",
                  GetEventName(event), GetStateName(current));
        pthread_mutex_unlock(&lock_);
        return LEAVR_OK;  // 忽略无匹配事件
    }

    DeviceState next = it->second.to;
    StateAction action = it->second.action;

    LOG_INFO("SM: %s  + %s  →  %s",
             GetStateName(current), GetEventName(event), GetStateName(next));

    // 执行转移动作
    if (action) {
        pthread_mutex_unlock(&lock_);
        int ret = action();
        pthread_mutex_lock(&lock_);
        if (ret != LEAVR_OK) {
            LOG_WARN("SM: Transition action failed: %d", ret);
            pthread_mutex_unlock(&lock_);
            if (event_cb_) event_cb_(event, ret);
            return ret;
        }
    }

    // 更新状态
    state_.store(next, std::memory_order_release);
    pthread_mutex_unlock(&lock_);

    if (event_cb_) {
        event_cb_(event, LEAVR_OK);
    }

    return LEAVR_OK;
}

bool StateMachine::CanRecord() const {
    DeviceState s = state_.load(std::memory_order_acquire);
    return s == DEVICE_STATE_STANDBY;
}

bool StateMachine::CanCapture() const {
    DeviceState s = state_.load(std::memory_order_acquire);
    return s == DEVICE_STATE_STANDBY;
}

} // namespace leavr