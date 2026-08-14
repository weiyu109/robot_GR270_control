#include "terminal/keyboard_teleop_action.h"

#include "input/jog_input.h"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

constexpr auto kJogPeriod = std::chrono::milliseconds(100);
constexpr auto kKeyboardPollPeriod = std::chrono::milliseconds(10);
constexpr auto kServoHealthCheckPeriod = std::chrono::milliseconds(200);

} // namespace

// 运行XYZ遥操作：进入时不重复上下电，Esc停止点动并返回数字主菜单。
TeleopResult runKeyboardTeleop(RobotJogController& robot,
                               KeyReader& keyReader,
                               int speedPercent,
                               const RobotJogController::ExitRequested& exitRequested)
{
    int servoState = -1;
    if (!robot.getServoState(servoState)
        || servoState != static_cast<int>(ServoState::Running)) {
        std::cerr << "请先选择菜单1完成上电，再进入键盘遥操作。\n";
        return TeleopResult::ReturnToMenu;
    }
    if (!keyReader.discardPendingEvents()) {
        std::cerr << keyReader.error() << "; refusing to enter jog mode.\n";
        return TeleopResult::Failed;
    }

    JogSessionConfig config;
    config.speedPercent = speedPercent;
    config.coordinate = 1;
    config.restartIfRunning = false;
    if (!robot.enterTeachJog(config, exitRequested)) {
        return TeleopResult::Failed;
    }

    std::cout
        << "键盘遥操作已启用：方向键控制X/Y，+/-控制Z。\n"
        << "松键停止；Esc返回主菜单；Ctrl+C安全下电并退出。\n";

    std::array<JogRequest, 3> activeRequests{};
    std::array<std::chrono::steady_clock::time_point, 3> nextJogSend{
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now()
    };
    auto nextServoHealthCheck = std::chrono::steady_clock::now();

    while (!exitRequested()) {
        KeySnapshot keys;
        if (!keyReader.poll(keys)) {
            std::cerr << keyReader.error() << "; stopping teleoperation.\n";
            robot.stopAllJogging();
            return TeleopResult::Failed;
        }
        if (keys.escape) {
            robot.stopAllJogging();
            keyReader.discardPendingEvents();
            return TeleopResult::ReturnToMenu;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextServoHealthCheck) {
            if (!robot.getServoState(servoState)) {
                robot.stopAllJogging();
                return TeleopResult::Failed;
            }
            if (servoState != static_cast<int>(ServoState::Running)) {
                activeRequests.fill({});
                if (!robot.recoverRunningServo(exitRequested)) {
                    return exitRequested() ? TeleopResult::ExitProgram : TeleopResult::Failed;
                }
                if (!keyReader.discardPendingEvents()) {
                    return TeleopResult::Failed;
                }
                nextServoHealthCheck = std::chrono::steady_clock::now()
                    + kServoHealthCheckPeriod;
                continue;
            }
            nextServoHealthCheck = now + kServoHealthCheckPeriod;
        }

        const auto requestedRequests = makeJogRequests(keys);
        for (std::size_t i = 0; i < activeRequests.size(); ++i) {
            JogRequest& active = activeRequests[i];
            const JogRequest& requested = requestedRequests[i];
            if (requested != active) {
                if (active.axis != 0) {
                    robot.stopJog(active.axis);
                }
                active = requested;
                if (active.axis != 0 && !robot.startJog(active)) {
                    active = {};
                    robot.stopAllJogging();
                    return TeleopResult::Failed;
                }
                nextJogSend[i] = now + kJogPeriod;
            } else if (active.axis != 0 && now >= nextJogSend[i]) {
                if (!robot.startJog(active)) {
                    robot.stopAllJogging();
                    return TeleopResult::Failed;
                }
                nextJogSend[i] = now + kJogPeriod;
            }
        }
        std::this_thread::sleep_for(kKeyboardPollPeriod);
    }

    robot.stopAllJogging();
    return TeleopResult::ExitProgram;
}
