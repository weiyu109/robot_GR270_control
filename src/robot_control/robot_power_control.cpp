#include "robot_control/robot_jog_controller.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {

constexpr auto kServoStatePollPeriod = std::chrono::milliseconds(100);
constexpr auto kServoStateTimeout = std::chrono::seconds(5);

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
    poweredOnBySession_ = false;
    joggingEnabled_ = false;
    return waitForServoState(static_cast<int>(ServoState::Ready));
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
