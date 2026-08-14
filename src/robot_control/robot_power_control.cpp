#include "robot_control/robot_jog_controller.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {

constexpr auto kServoStatePollPeriod = std::chrono::milliseconds(100);
constexpr auto kServoStateTimeout = std::chrono::seconds(5);
constexpr auto kPowerRetryPeriod = std::chrono::milliseconds(300);

} // namespace

// 切换伺服到就绪状态 state=1；调用前先撤销所有点动请求。
bool RobotJogController::setServoReady()
{
    stopAllJogging();
    joggingEnabled_ = false;
    return connected_
        && requireSuccess(sdk_.setServoState(socket_, static_cast<int>(ServoState::Ready)),
                          "set_servo_state(ready)");
}

// 单次上电：只有伺服已经处于 Ready(state=1) 时才发送上电指令。
bool RobotJogController::powerOnOnce()
{
    if (!connected_) {
        return false;
    }
    int state = -1;
    if (!getServoState(state) || state != static_cast<int>(ServoState::Ready)) {
        std::cerr << "Refusing to power on: servo must be in Ready state (state=1).\n";
        return false;
    }
    const Result result = sdk_.powerOn(socket_);
    if (result == SUCCESS) {
        poweredOnBySession_ = true;
        return true;
    }
    return requireSuccess(result, "set_servo_poweron");
}

// 单次下电：先停止运动，再将 Running(state=3) 切回 Ready(state=1)。
bool RobotJogController::powerOff()
{
    if (!connected_) {
        return false;
    }
    stopAllJogging();
    const Result result = sdk_.powerOff(socket_);
    if (!requireSuccess(result, "set_servo_poweroff")) {
        return false;
    }
    joggingEnabled_ = false;
    if (!waitForServoState(static_cast<int>(ServoState::Ready))) {
        // 未确认 Ready 前继续保留本会话的下电责任，供 shutdown() 重试。
        return false;
    }
    poweredOnBySession_ = false;
    return true;
}

// 循环安全上电：复用清错/Ready/Running 状态机，已经上电时不重复下电重启。
bool RobotJogController::powerOnUntilRunning(const ExitRequested& exitRequested)
{
    if (!connected_) {
        return false;
    }
    if (initialServoState_ < 0 && !getServoState(initialServoState_)) {
        return false;
    }
    if (!ensureServoRunning(exitRequested, false)) {
        return false;
    }
    poweredOnBySession_ = true;
    return true;
}

// 循环安全下电：报警时先循环清错，Running 时反复下电直到 Ready/Stopped。
bool RobotJogController::powerOffUntilReady(const ExitRequested& exitRequested)
{
    if (!connected_) {
        return false;
    }
    while (!exitRequested()) {
        int state = -1;
        if (!getServoState(state)) {
            return false;
        }
        if (state == static_cast<int>(ServoState::Ready)
            || state == static_cast<int>(ServoState::Stopped)) {
            poweredOnBySession_ = false;
            joggingEnabled_ = false;
            return true;
        }
        if (state == static_cast<int>(ServoState::Alarm)) {
            if (!clearErrorUntilReady(exitRequested)) {
                return false;
            }
            continue;
        }
        if (state == static_cast<int>(ServoState::Running) && powerOff()) {
            return true;
        }
        std::this_thread::sleep_for(kPowerRetryPeriod);
    }
    return false;
}

// 接管退出下电责任：满足“关闭页面必须确认下电后再断开”的终端要求。
void RobotJogController::requirePowerOffOnShutdown()
{
    powerOffRequiredOnShutdown_ = true;
}

// 等待伺服状态：在超时范围内轮询目标状态，报警时立即返回失败。
bool RobotJogController::waitForServoState(int expectedState)
{
    const auto deadline = std::chrono::steady_clock::now() + kServoStateTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        int state = -1;
        if (!getServoState(state)) {
            return false;
        }
        if (state == expectedState) {
            return true;
        }
        if (state == static_cast<int>(ServoState::Alarm)) {
            std::cerr << "Servo entered alarm state while waiting for state "
                      << expectedState << ".\n";
            return false;
        }
        std::this_thread::sleep_for(kServoStatePollPeriod);
    }
    std::cerr << "Timed out waiting for servo state " << expectedState << ".\n";
    return false;
}
