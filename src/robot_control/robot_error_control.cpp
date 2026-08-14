#include "robot_control/robot_jog_controller.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {

constexpr auto kClearErrorRetryPeriod = std::chrono::milliseconds(300);

} // namespace

// 单次清错：停止点动后只调用一次 clear_error，不承诺报警已经消失。
bool RobotJogController::clearErrorOnce()
{
    stopAllJogging();
    joggingEnabled_ = false;
    return connected_ && requireSuccess(sdk_.clearError(socket_), "clear_error");
}

// 循环清错：报警存在时反复 clear_error，并尝试切到 Ready，直到报警解除或退出。
bool RobotJogController::clearErrorUntilReady(const ExitRequested& exitRequested)
{
    if (!connected_) {
        return false;
    }
    stopAllJogging();
    joggingEnabled_ = false;

    int attempts = 0;
    bool clearRequestAccepted = false;
    while (!exitRequested()) {
        int state = -1;
        if (!getServoState(state)) {
            return false;
        }
        if (state == static_cast<int>(ServoState::Running)) {
            return true;
        }
        // state=1 只表示伺服未上电，不代表控制器内部错误已经清除。
        // 必须确认本次流程至少成功发送过一次 clear_error，才能报告清错完成。
        if (state == static_cast<int>(ServoState::Ready) && clearRequestAccepted) {
            return true;
        }
        if (state != static_cast<int>(ServoState::Stopped)
            && state != static_cast<int>(ServoState::Ready)
            && state != static_cast<int>(ServoState::Alarm)) {
            std::cerr << "Unexpected servo state while clearing error: " << state << ".\n";
            return false;
        }

        ++attempts;
        std::cout << "Clearing controller error. clear_error attempt "
                  << attempts << " (servo state=" << state << ")...\n";
        const Result clearResult = sdk_.clearError(socket_);
        if (clearResult != SUCCESS) {
            std::cerr << "clear_error failed: " << sdkResultText(clearResult)
                      << " (" << static_cast<int>(clearResult) << "); retrying...\n";
        } else {
            clearRequestAccepted = true;
        }

        if (state != static_cast<int>(ServoState::Ready)) {
            const Result readyResult = sdk_.setServoState(
                socket_, static_cast<int>(ServoState::Ready));
            if (readyResult != SUCCESS) {
                std::cerr << "set_servo_state(1) while clearing failed: "
                          << sdkResultText(readyResult)
                          << " (" << static_cast<int>(readyResult) << "); retrying...\n";
            }
        }
        std::this_thread::sleep_for(kClearErrorRetryPeriod);
    }

    std::cerr << "Clear-error loop cancelled by exit request.\n";
    return false;
}
