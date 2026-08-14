#ifdef _WIN32
#include "input/windows_console_key_reader.h"

#include <atomic>
#elif defined(__linux__)
#include "input/linux_evdev_key_reader.h"

#include <csignal>
#else
#error Unsupported platform
#endif

#include "input/jog_input.h"
#include "robot_control/robot_jog_controller.h"
#include "robot_control/robot_sdk_api.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr int kDefaultSpeedPercent = 10;
constexpr auto kJogPeriod = std::chrono::milliseconds(100);
constexpr auto kKeyboardPollPeriod = std::chrono::milliseconds(10);
constexpr auto kServoHealthCheckPeriod = std::chrono::milliseconds(200);

#ifdef _WIN32
std::atomic_bool g_exitRequested{false};

BOOL WINAPI consoleHandler(DWORD event)
{
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_exitRequested = true;
        return TRUE;
    default:
        return FALSE;
    }
}

void requestExit() { g_exitRequested = true; }
bool exitRequested() { return g_exitRequested.load(); }
#else
volatile std::sig_atomic_t g_exitRequested = 0;

void signalHandler(int) { g_exitRequested = 1; }
void requestExit() { g_exitRequested = 1; }
bool exitRequested() { return g_exitRequested != 0; }
#endif

// 只记录控制器普通消息，不在回调线程里清错、上下电或改变运动状态。
void controllerMessageCallback(int messageId, const char* message)
{
    std::cout << "[CONTROLLER MESSAGE] id=" << messageId
              << " message=" << (message != nullptr ? message : "") << '\n';
}

} // namespace

int main(int argc, char* argv[])
{
    const std::string ip = argc > 1 ? argv[1] : "192.168.1.13";
    const std::string port = argc > 2 ? argv[2] : "6001";
    const int speed = argc > 3 ? std::atoi(argv[3]) : kDefaultSpeedPercent;
    if (speed <= 0 || speed > 100) {
        std::cerr << "Speed must be in the range 1..100.\n";
        return 2;
    }

#ifdef _WIN32
    WindowsConsoleKeyReader keyReader;
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    if (argc <= 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <controller_ip> <controller_port> <speed_percent>"
                     " <input_device>\n";
        return 2;
    }
    const std::string inputDevice = argv[4];
    EvdevKeyReader keyReader(inputDevice);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGHUP, signalHandler);
#endif

    // 键盘必须先验证成功，输入异常时不能连接或改变机械臂状态。
    if (!keyReader.valid()) {
        std::cerr << keyReader.error() << '\n';
        return 1;
    }

    InexbotSdkApi sdk;
    RobotJogController robot(sdk);
    std::cout << "Connecting to " << ip << ':' << port << " ...\n";
    if (!robot.connect(ip, port, controllerMessageCallback)) {
        return 1;
    }

    std::cout
        << "WARNING: This program can move the robot.\n"
        << "Clear the work area and keep the emergency stop available.\n"
#ifdef __linux__
        << "Linux evdev input is global: arrow keys remain active when another window has focus.\n"
#endif
        << "Type ENABLE and press Enter to enter teach/jog mode and power on: ";

    std::string confirmation;
    std::getline(std::cin, confirmation);
    if (confirmation != "ENABLE") {
        std::cout << "Cancelled.\n";
        robot.shutdown();
        return 0;
    }

    // 清掉输入 ENABLE 期间产生的事件，确保进入运动状态时所有轴从“未按下”开始。
    if (!keyReader.discardPendingEvents()) {
        std::cerr << keyReader.error() << "; refusing to enter jog mode.\n";
        robot.shutdown();
        return 1;
    }

    JogSessionConfig config;
    config.speedPercent = speed;
    config.coordinate = 1;          // 直角坐标系
    config.restartIfRunning = true; // 已上电时按已验证流程先下电再重新上电
    if (!robot.enterTeachJog(config, exitRequested)) {
        robot.shutdown();
        return 1;
    }

    std::cout
        << "Connected. Cartesian coordinate mode selected.\n"
        << "Hold UP for X+, hold DOWN for X-.\n"
        << "Hold LEFT for Y+, hold RIGHT for Y-.\n"
        << "Hold + for Z+, hold - for Z-.\n"
        << "X, Y and Z may be jogged together; opposite keys cancel that axis.\n"
        << "Release the key to stop. Press ESC or Ctrl+C to exit.\n"
        << "Speed: " << speed << "%\n";

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
            std::cerr << keyReader.error() << "; stopping for safety.\n";
            break;
        }
        if (keys.escape) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();

        // 运行中只响应主动读取的伺服状态，不在错误/警告回调中执行恢复动作。
        if (now >= nextServoHealthCheck) {
            int servoState = -1;
            if (!robot.getServoState(servoState)) {
                break;
            }
            if (servoState != static_cast<int>(ServoState::Running)) {
                std::cerr << "Non-running servo state detected (state=" << servoState
                          << "); stopping all jogging before recovery.\n";
                activeRequests.fill({});
                if (!robot.recoverRunningServo(exitRequested)) {
                    break;
                }

                // 恢复后丢弃旧按键，必须重新按键才能再次运动，防止自动续动。
                if (!keyReader.discardPendingEvents()) {
                    std::cerr << keyReader.error() << "; stopping for safety.\n";
                    break;
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
                // 同轴换向必须先停旧方向，再启动新方向。
                if (active.axis != 0) {
                    robot.stopJog(active.axis);
                }
                active = requested;
                if (active.axis != 0) {
                    if (!robot.startJog(active)) {
                        active = {};
                        requestExit();
                        break;
                    }
                    std::cout << "[JOG] START axis=" << active.axis
                              << " direction="
                              << (active.positive ? "positive" : "negative")
                              << " result=SUCCESS\n";
                    nextJogSend[i] = now + kJogPeriod;
                }
            } else if (active.axis != 0 && now >= nextJogSend[i]) {
                // 点动为保持型命令：按住期间每 100 ms 续发一次。
                if (!robot.startJog(active)) {
                    robot.stopJog(active.axis);
                    active = {};
                    requestExit();
                    break;
                }
                nextJogSend[i] = now + kJogPeriod;
            }
        }

        std::this_thread::sleep_for(kKeyboardPollPeriod);
    }

    robot.shutdown();
    std::cout << "Stopped and disconnected.\n";
    return 0;
}
