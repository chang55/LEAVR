/**
 * @file state_machine.h
 * @brief 设备主状态机 - 执法记录仪核心调度引擎
 */

#ifndef LEAVR_APP_STATE_MACHINE_H
#define LEAVR_APP_STATE_MACHINE_H

#include "leavr_types.h"
#include "leavr_errors.h"
#include <functional>
#include <map>
#include <utility>
#include <pthread.h>
#include <atomic>

namespace leavr {

class StateMachine {
public:
    using StateAction = std::function<int()>;
    using EventCallback = std::function<void(EventType event, int result)>;

    StateMachine();
    ~StateMachine();

    /** 初始化状态机（定义转移表） */
    int Init();

    /** 投递事件（线程安全，可从任意线程调用） */
    int PostEvent(EventType event);

    /** 获取当前状态 */
    DeviceState GetState() const { return state_; }

    /** 获取状态名 */
    static const char* GetStateName(DeviceState state);
    static const char* GetEventName(EventType event);

    /** 注册状态转移回调 */
    void RegisterTransition(DeviceState from, EventType event,
                             DeviceState to, StateAction action);

    /** 注册事件结果回调 */
    void SetEventCallback(EventCallback cb) { event_cb_ = cb; }

    /** 判断是否可以录像 */
    bool CanRecord() const;

    /** 判断是否可以拍照 */
    bool CanCapture() const;

    /** 是否为录制相关状态 */
    bool IsRecording() const { return state_ == DEVICE_STATE_RECORDING; }

private:
    struct Transition {
        DeviceState to;
        StateAction action;
    };

    // (from, event) → (to, action)
    std::map<std::pair<DeviceState, EventType>, Transition> transitions_;

    std::atomic<DeviceState> state_{DEVICE_STATE_BOOT};
    EventCallback event_cb_;

    pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;
    bool initialized_ = false;
};

} // namespace leavr

#endif // LEAVR_APP_STATE_MACHINE_H