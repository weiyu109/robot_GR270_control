#ifdef _WIN32
#include "input/windows_console_key_reader.h"

#include <atomic>
#elif defined(__linux__)
#include "input/linux_evdev_key_reader.h"

#include <csignal>
#else
#error Unsupported platform
#endif

#include "robot_control/robot_jog_controller.h"
#include "robot_control/robot_sdk_api.h"
#include "terminal/keyboard_teleop_action.h"
#include "terminal/robot_debug_menu.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr int kDefaultSpeedPercent = 10;
constexpr auto kShutdownRetryPeriod = std::chrono::milliseconds(300);

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

bool exitRequested() { return g_exitRequested.load(); }
#else
volatile std::sig_atomic_t g_exitRequested = 0;

void signalHandler(int) { g_exitRequested = 1; }
bool exitRequested() { return g_exitRequested != 0; }
#endif

// 只记录控制器普通消息，不在回调线程里清错、上下电或改变运动状态。
void controllerMessageCallback(int messageId, const char* message)
{
    std::cout << "[CONTROLLER MESSAGE] id=" << messageId
              << " message=" << (message != nullptr ? message : "") << '\n';
}

// 页面关闭或菜单退出时循环下电；确认安全状态后才断开SDK。
void shutdownWithRetry(RobotJogController& robot)
{
    int attempt = 0;
    while (!robot.shutdown()) {
        ++attempt;
        std::cerr << "Safe shutdown attempt " << attempt
                  << " failed. SDK connection is retained; retrying. "
                     "Use the emergency stop if robot motion is not confirmed stopped.\n";
        std::this_thread::sleep_for(kShutdownRetryPeriod);
    }
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
    EvdevKeyReader keyReader(argv[4]);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGHUP, signalHandler);
#endif

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

    // 本终端无论机械臂进入程序前是否已上电，退出时都必须确认下电后再断开。
    robot.requirePowerOffOnShutdown();
    std::cout
        << "WARNING: Menu actions can move the robot immediately after Enter.\n"
        << "Clear the work area and keep the emergency stop available.\n"
#ifdef __linux__
        << "Linux evdev input is global while keyboard teleoperation is active.\n"
#endif
        ;

    RobotDebugMenu menu(
        robot,
        exitRequested,
        [&] { return runKeyboardTeleop(robot, keyReader, speed, exitRequested); },
        std::cin,
        std::cout,
        std::cerr);
    const int result = menu.run();

    shutdownWithRetry(robot);
    std::cout << "Robot powered off and disconnected.\n";
    return result;
}
