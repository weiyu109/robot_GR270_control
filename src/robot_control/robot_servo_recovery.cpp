#include "robot_control/robot_jog_controller.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {

constexpr auto kServoStatePollPeriod = std::chrono::milliseconds(100);
constexpr auto kServoRecoveryPeriod = std::chrono::milliseconds(300);
constexpr int kStableRunningSamples = 3;

} // namespace

// 循环恢复并上电：处理 Alarm/Stopped/Ready/Running，连续三次 state=3 才成功。
bool RobotJogController::ensureServoRunning(const ExitRequested& exitRequested,
                                            bool restartIfAlreadyRunning)
{
    bool mustPowerCycleAfterAlarm = false;
    bool powerOnSucceededBySession = false;
    int powerOnAttempts = 0;
    int stableRunningSamples = 0;

    while (!exitRequested()) {
        int state = -1;
        if (!getServoState(state)) {
            return false;
        }

        if (state == static_cast<int>(ServoState::Alarm)) {
            stableRunningSamples = 0;
            mustPowerCycleAfterAlarm = true;
            powerOnSucceededBySession = false;
            if (!clearErrorUntilReady(exitRequested)) {
                return false;
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == static_cast<int>(ServoState::Running)) {
            if (!mustPowerCycleAfterAlarm
                && (powerOnSucceededBySession || !restartIfAlreadyRunning)) {
                ++stableRunningSamples;
                if (stableRunningSamples >= kStableRunningSamples) {
                    std::cout << "Servo running state confirmed by "
                              << stableRunningSamples << " consecutive checks.\n";
                    return true;
                }
                std::this_thread::sleep_for(kServoStatePollPeriod);
                continue;
            }

            std::cout << (mustPowerCycleAfterAlarm
                              ? "Alarm cleared to running state; powering off before restart...\n"
                              : "Servo is already running; powering off before restart...\n");
            if (!requireSuccess(sdk_.powerOff(socket_),
                                "set_servo_poweroff(before restart)")) {
                return false;
            }
            poweredOnBySession_ = true;
            powerOnSucceededBySession = false;
            stableRunningSamples = 0;
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == static_cast<int>(ServoState::Stopped)) {
            stableRunningSamples = 0;
            std::cout << "Setting servo to ready state...\n";
            if (!setServoReady()) {
                return false;
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == static_cast<int>(ServoState::Ready)) {
            stableRunningSamples = 0;
            ++powerOnAttempts;
            // 恢复上一个实机路线：独立清错完成后，Ready态直接发送上电指令。
            std::cout << "Servo ready. Power-on attempt " << powerOnAttempts << "...\n";
            const Result result = sdk_.powerOn(socket_);
            if (result == SUCCESS) {
                poweredOnBySession_ = true;
                powerOnSucceededBySession = true;
                mustPowerCycleAfterAlarm = false;
            } else {
                std::cerr << "set_servo_poweron failed: " << sdkResultText(result)
                          << " (" << static_cast<int>(result)
                          << "); checking servo state and retrying...\n";
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        std::cerr << "Unexpected servo state: " << state << ".\n";
        return false;
    }

    std::cerr << "Servo recovery cancelled by exit request.\n";
    return false;
}
