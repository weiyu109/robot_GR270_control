#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include "linux_evdev_key_reader.h"

#include <csignal>
#else
#error Unsupported platform
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "cpp/interface/nrc_api.h"
#include "jog_input.h"

namespace {

constexpr int kCartesianCoord = 1;
constexpr int kTeachMode = 0;
constexpr int kJogTeachType = 0;
constexpr int kDefaultSpeedPercent = 10;
constexpr auto kJogPeriod = std::chrono::milliseconds(100);
constexpr auto kKeyboardPollPeriod = std::chrono::milliseconds(10);
constexpr auto kServoStatePollPeriod = std::chrono::milliseconds(100);
constexpr auto kServoHealthCheckPeriod = std::chrono::milliseconds(200);
constexpr auto kServoStateTimeout = std::chrono::seconds(5);
constexpr auto kServoRecoveryPeriod = std::chrono::milliseconds(300);
constexpr int kStableRunningSamples = 3;

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
#else
volatile std::sig_atomic_t g_exitRequested = 0;

void signalHandler(int)
{
    g_exitRequested = 1;
}
#endif

void controllerMessageCallback(int messageId, const char* message)
{
    std::cout << "[CONTROLLER MESSAGE] id=" << messageId
              << " message=" << (message != nullptr ? message : "") << '\n';
}

const char* resultText(Result result)
{
    switch (result) {
    case SUCCESS: return "SUCCESS";
    case RECEIVE_FAILED: return "RECEIVE_FAILED";
    case DISCONNECT: return "DISCONNECT";
    case PARAM_ERR: return "PARAM_ERR";
    case OPERATION_NOT_ALLOWED: return "OPERATION_NOT_ALLOWED";
    case EXCEPTION: return "EXCEPTION";
    case TIMEOUT: return "TIMEOUT";
    default: return "UNKNOWN";
    }
}

// 所有连接和重连都从这里进入：先关闭本进程持有的旧 SDK 连接，再建立新连接。
bool reconnectRobot(SOCKETFD& socket, const std::string& ip, const std::string& port)
{
    if (socket >= 0) {
        std::cout << "Cleaning up existing SDK connection before reconnecting...\n";
        disconnect_robot(socket);
        socket = -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // 不额外创建裸TCP探测连接。部分控制器的6001服务为单连接，
    // 探测连接刚关闭时立即调用SDK会触发Connection refused。
    socket = connect_robot(ip, port);
    return socket >= 0;
}

#ifdef _WIN32
class ConsoleKeyReader
{
public:
    ConsoleKeyReader()
        : input_(GetStdHandle(STD_INPUT_HANDLE))
    {
        if (input_ == INVALID_HANDLE_VALUE || !GetConsoleMode(input_, &originalMode_)) {
            return;
        }

        const DWORD mode = originalMode_ | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
        if (!SetConsoleMode(input_, mode)) {
            return;
        }

        // 丢弃输入 ENABLE 时残留的 Enter 等控制台事件。
        FlushConsoleInputBuffer(input_);
        valid_ = true;
    }

    ~ConsoleKeyReader()
    {
        if (valid_) {
            SetConsoleMode(input_, originalMode_);
        }
    }

    bool valid() const
    {
        return valid_;
    }

    bool poll(KeySnapshot& snapshot)
    {
        if (!valid_) {
            snapshot = {};
            return false;
        }

        DWORD pending = 0;
        if (!GetNumberOfConsoleInputEvents(input_, &pending)) {
            snapshot = {};
            return false;
        }

        while (pending > 0) {
            INPUT_RECORD records[32];
            DWORD read = 0;
            const DWORD count = pending < 32 ? pending : 32;
            if (!ReadConsoleInputW(input_, records, count, &read)) {
                snapshot = {};
                return false;
            }

            for (DWORD i = 0; i < read; ++i) {
                const INPUT_RECORD& record = records[i];
                if (record.EventType == FOCUS_EVENT) {
                    if (!record.Event.FocusEvent.bSetFocus) {
                        keys_ = {};
                        shiftDown_ = false;
                        mainPlusKeyDown_ = false;
                        keypadPlusDown_ = false;
                        mainMinusKeyDown_ = false;
                        keypadMinusDown_ = false;
                    }
                    continue;
                }
                if (record.EventType != KEY_EVENT) {
                    continue;
                }

                const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
                const bool down = key.bKeyDown != FALSE;
                shiftDown_ = (key.dwControlKeyState & SHIFT_PRESSED) != 0;
                switch (key.wVirtualKeyCode) {
                case VK_UP: keys_.up = down; break;
                case VK_DOWN: keys_.down = down; break;
                case VK_LEFT: keys_.left = down; break;
                case VK_RIGHT: keys_.right = down; break;
                case VK_OEM_PLUS: mainPlusKeyDown_ = down; break;
                case VK_ADD: keypadPlusDown_ = down; break;
                case VK_OEM_MINUS: mainMinusKeyDown_ = down; break;
                case VK_SUBTRACT: keypadMinusDown_ = down; break;
                case VK_ESCAPE: keys_.escape = down; break;
                default: break;
                }
                // 主键盘+要求Shift+=，主键盘-要求未按Shift；数字键盘不受Shift影响。
                keys_.plus = keypadPlusDown_ || (mainPlusKeyDown_ && shiftDown_);
                keys_.minus = keypadMinusDown_ || (mainMinusKeyDown_ && !shiftDown_);
            }

            if (!GetNumberOfConsoleInputEvents(input_, &pending)) {
                snapshot = {};
                return false;
            }
        }

        snapshot = keys_;
        return true;
    }

private:
    HANDLE input_{INVALID_HANDLE_VALUE};
    DWORD originalMode_{0};
    bool valid_{false};
    bool shiftDown_{false};
    bool mainPlusKeyDown_{false};
    bool keypadPlusDown_{false};
    bool mainMinusKeyDown_{false};
    bool keypadMinusDown_{false};
    KeySnapshot keys_;
};
#endif

bool sendJog(SOCKETFD socket, const JogRequest& request)
{
    static unsigned long long callCount = 0;
    ++callCount;
    // 点动是保持型命令：首次按键立即发送，主循环随后每 100 ms 续发。
    const Result result = robot_start_jogging(socket, request.axis, request.positive);
    std::cout << "[JOG API] robot_start_jogging call=" << callCount
              << " axis=" << request.axis
              << " direction=" << (request.positive ? "positive" : "negative")
              << " result=" << resultText(result)
              << " (" << static_cast<int>(result) << ")\n";
    if (result != SUCCESS) {
        return false;
    }
    return true;
}

void stopJog(SOCKETFD socket, int axis)
{
    if (axis == 0) {
        return;
    }

    // 松键、换向、输入异常和退出清理都会经过这里。
    const Result result = robot_stop_jogging(socket, axis);
    std::cout << "[JOG API] robot_stop_jogging axis=" << axis
              << " result=" << resultText(result)
              << " (" << static_cast<int>(result) << ")\n";
}

bool requireSuccess(Result result, const char* operation)
{
    if (result == SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed: " << resultText(result)
              << " (" << static_cast<int>(result) << ")\n";
    return false;
}

bool printControllerState(SOCKETFD socket)
{
    int mode = -1;
    int teachType = -1;
    int coord = -1;
    int speed = -1;
    int servoState = -1;

    if (!requireSuccess(get_current_mode(socket, mode), "get_current_mode(diagnostic)")
        || !requireSuccess(get_teach_type(socket, teachType), "get_teach_type(diagnostic)")
        || !requireSuccess(get_current_coord(socket, coord), "get_current_coord(diagnostic)")
        || !requireSuccess(get_speed(socket, speed), "get_speed(diagnostic)")
        || !requireSuccess(get_servo_state(socket, servoState), "get_servo_state(diagnostic)")) {
        return false;
    }

    std::cout << "[STATE] mode=" << mode
              << " (0=teach,1=remote,2=run)"
              << " teachType=" << teachType
              << " (0=jog,1=drag)"
              << " coord=" << coord
              << " (0=joint,1=cartesian,2=tool,3=user)"
              << " speed=" << speed
              << "% servo=" << servoState
              << " (0=stopped,1=ready,2=alarm,3=running)\n";
    return true;
}

bool waitForServoState(SOCKETFD socket, int expectedState)
{
    const auto deadline = std::chrono::steady_clock::now() + kServoStateTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        int state = -1;
        if (!requireSuccess(get_servo_state(socket, state), "get_servo_state")) {
            return false;
        }
        if (state == expectedState) {
            return true;
        }
        if (state == 2) {
            std::cerr << "Servo entered alarm state. Clear the fault manually.\n";
            return false;
        }
        std::this_thread::sleep_for(kServoStatePollPeriod);
    }
    std::cerr << "Timed out waiting for servo state " << expectedState << ".\n";
    return false;
}

bool ensureServoRunning(SOCKETFD socket,
                        bool& poweredOnByDemo,
                        bool restartIfAlreadyRunning)
{
    bool mustPowerCycleAfterAlarm = false;
    bool powerOnSucceededByDemo = false;
    int clearAttempts = 0;
    int powerOnAttempts = 0;
    int stableRunningSamples = 0;

    // 报警未解除时持续恢复，不设置超时；操作者可随时用Ctrl+C终止。
    while (!g_exitRequested) {
        int state = -1;
        if (!requireSuccess(get_servo_state(socket, state), "get_servo_state")) {
            return false;
        }

        if (state == 2) {
            stableRunningSamples = 0;
            // SDK 要求报警清除后先释放伺服占用，再重新上电。
            mustPowerCycleAfterAlarm = true;
            powerOnSucceededByDemo = false;
            ++clearAttempts;
            std::cout << "Servo alarm detected. clear_error attempt "
                      << clearAttempts << "...\n";
            const Result result = clear_error(socket);
            if (result != SUCCESS) {
                std::cerr << "clear_error failed: " << resultText(result)
                          << " (" << static_cast<int>(result)
                          << "); retrying...\n";
            }
            // state=2不能直接运动；清错后尝试切到就绪态，若控制器仍有故障则继续循环。
            const Result readyResult = set_servo_state(socket, 1);
            if (readyResult != SUCCESS) {
                std::cerr << "set_servo_state(1) while recovering failed: "
                          << resultText(readyResult)
                          << " (" << static_cast<int>(readyResult)
                          << "); alarm may still be active.\n";
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == 3) {
            // 本程序完成上电后再次读到运行态，说明重启流程已经闭环。
            // 必须立即返回，否则会把确认成功的运行态误判为初始上电态而反复下电。
            if (!mustPowerCycleAfterAlarm
                && (powerOnSucceededByDemo || !restartIfAlreadyRunning)) {
                ++stableRunningSamples;
                if (stableRunningSamples >= kStableRunningSamples) {
                    std::cout << "Servo running state confirmed by "
                              << stableRunningSamples << " consecutive checks.\n";
                    return true;
                }
                std::this_thread::sleep_for(kServoStatePollPeriod);
                continue;
            }

            // 初次检测到已经上电，或报警清除后仍是运行态：只执行一次下电，
            // 随后通过就绪态重新上电。
            std::cout << (mustPowerCycleAfterAlarm
                              ? "Alarm cleared to running state; powering off before restart...\n"
                              : "Servo is already running; powering off before restart...\n");
            if (!requireSuccess(set_servo_poweroff(socket),
                                "set_servo_poweroff(before restart)")) {
                return false;
            }
            poweredOnByDemo = true;
            powerOnSucceededByDemo = false;
            stableRunningSamples = 0;
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == 0) {
            stableRunningSamples = 0;
            std::cout << "Setting servo to ready state...\n";
            if (!requireSuccess(set_servo_state(socket, 1),
                                "set_servo_state(ready)")) {
                return false;
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == 1) {
            stableRunningSamples = 0;
            // 只有就绪态允许调用上电接口；成功后仍需轮询确认运行态。
            ++powerOnAttempts;
            std::cout << "Servo ready. Power-on attempt " << powerOnAttempts << "...\n";
            const Result result = set_servo_poweron(socket);
            if (result == SUCCESS) {
                poweredOnByDemo = true;
                powerOnSucceededByDemo = true;
                mustPowerCycleAfterAlarm = false;
            } else {
                std::cerr << "set_servo_poweron failed: " << resultText(result)
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

} // namespace

int main(int argc, char* argv[])
{
    const std::string ip = argc > 1 ? argv[1] : "192.168.1.13";
    const std::string port = argc > 2 ? argv[2] : "6001";
    const int speed = argc > 3 ? std::atoi(argv[3]) : kDefaultSpeedPercent;
#ifdef __linux__
    // Linux 第四个参数必须是明确的 evdev 键盘设备，不能盲选 eventN。
    if (argc <= 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <controller_ip> <controller_port> <speed_percent>"
                     " <input_device>\n";
        return 2;
    }
    const std::string inputDevice = argv[4];
#endif

    if (speed <= 0 || speed > 100) {
        std::cerr << "Speed must be in the range 1..100.\n";
        return 2;
    }

#ifdef __linux__
    // 在连接控制器前先验证键盘设备，输入不可用时不触碰机器人状态。
    EvdevKeyReader keyReader(inputDevice);
    if (!keyReader.valid()) {
        std::cerr << keyReader.error() << '\n';
        return 1;
    }
#endif

#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGHUP, signalHandler);
#endif

    SOCKETFD socket = -1;
    std::cout << "Connecting to " << ip << ':' << port << " ...\n";
    if (!reconnectRobot(socket, ip, port)) {
        std::cerr << "connect_robot failed.\n";
        return 1;
    }

    // 暂不注册错误/警告回调，避免普通警告在运行态触发清错和电源恢复动作。
    // 安全判断统一以主循环主动读取的伺服状态为准。
    if (!requireSuccess(recv_message(socket, controllerMessageCallback),
                        "recv_message")) {
        disconnect_robot(socket);
        return 1;
    }

    bool connected = true;
    bool poweredOnByDemo = false;
    std::array<bool, 3> activeAxes{false, false, false};
    int previousMode = -1;
    int initialServoState = -1;
    auto cleanup = [&]() {
        if (!connected) {
            return;
        }

        // 无论退出原因是什么，先停止所有可能仍处于活动状态的轴。
        for (std::size_t i = 0; i < activeAxes.size(); ++i) {
            if (activeAxes[i]) {
                stopJog(socket, static_cast<int>(i) + 1);
                activeAxes[i] = false;
            }
        }

        if (poweredOnByDemo) {
            const Result result = set_servo_poweroff(socket);
            if (result != SUCCESS) {
                std::cerr << "set_servo_poweroff failed: " << resultText(result)
                          << " (" << static_cast<int>(result) << ")\n";
            } else {
                waitForServoState(socket, 1);
            }
        }

        // 如果程序进入时伺服为停止状态，则尽量恢复到停止状态。
        if (initialServoState == 0) {
            requireSuccess(set_servo_state(socket, 0), "restore servo stopped state");
        }

        if (previousMode >= 0 && previousMode != kTeachMode) {
            requireSuccess(set_current_mode(socket, previousMode), "restore current mode");
        }

        disconnect_robot(socket);
        connected = false;
    };

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
        cleanup();
        return 0;
    }

#ifdef __linux__
    // 清理 ENABLE 前积压的按键事件，防止进入点动后立即误动作。
    if (!keyReader.discardPendingEvents()) {
        std::cerr << keyReader.error() << "; refusing to enter jog mode.\n";
        cleanup();
        return 1;
    }
#endif

    if (!requireSuccess(get_current_mode(socket, previousMode), "get_current_mode")
        || !requireSuccess(set_current_mode(socket, kTeachMode), "set_current_mode(teach)")
        || !requireSuccess(set_teach_type(socket, kJogTeachType), "set_teach_type(jog)")) {
        cleanup();
        return 1;
    }

    int servoState = -1;
    if (!requireSuccess(get_servo_state(socket, servoState), "get_servo_state")) {
        cleanup();
        return 1;
    }
    initialServoState = servoState;

    if (!ensureServoRunning(socket, poweredOnByDemo, true)) {
        cleanup();
        return 1;
    }

    const Result coordResult = set_current_coord(socket, kCartesianCoord);
    if (coordResult != SUCCESS) {
        std::cerr << "set_current_coord(1) failed: " << resultText(coordResult)
                  << " (" << static_cast<int>(coordResult) << ")\n";
        cleanup();
        return 1;
    }

    const Result speedResult = set_speed(socket, speed);
    if (speedResult != SUCCESS) {
        std::cerr << "set_speed failed: " << resultText(speedResult)
                  << " (" << static_cast<int>(speedResult) << ")\n";
        cleanup();
        return 1;
    }

    if (!printControllerState(socket)) {
        cleanup();
        return 1;
    }

    std::cout
        << "Connected. Cartesian coordinate mode selected.\n"
        << "Hold UP for X+, hold DOWN for X-.\n"
        << "Hold LEFT for Y+, hold RIGHT for Y-.\n"
        << "Hold + for Z+, hold - for Z-.\n"
        << "X, Y and Z may be jogged together; opposite keys on the same axis cancel that axis.\n"
        << "Release the key to stop. Press ESC or Ctrl+C to exit.\n"
        << "Speed: " << speed << "%\n";

    std::array<JogRequest, 3> activeRequests{};
#ifdef _WIN32
    ConsoleKeyReader keyReader;
    if (!keyReader.valid()) {
        std::cerr << "Failed to initialize Windows console key-event reader.\n";
        cleanup();
        return 1;
    }
    std::cout << "[INPUT] Windows console key-event reader active.\n";
#else
    std::cout << "[INPUT] Linux evdev reader active: " << inputDevice << '\n';
#endif
    std::array<std::chrono::steady_clock::time_point, 3> nextJogSend{
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now()
    };
    auto nextServoHealthCheck = std::chrono::steady_clock::now();

    while (!g_exitRequested) {
        KeySnapshot keys;
        if (!keyReader.poll(keys)) {
#ifdef _WIN32
            std::cerr << "Console input read failed; stopping for safety.\n";
#else
            std::cerr << keyReader.error() << "; stopping for safety.\n";
#endif
            break;
        }

        if (keys.escape) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();

        // 运行中只依据主动查询到的真实伺服状态处理，不响应错误/警告回调。
        if (now >= nextServoHealthCheck) {
            int liveServoState = -1;
            if (!requireSuccess(get_servo_state(socket, liveServoState),
                                "get_servo_state(runtime health check)")) {
                break;
            }

            if (liveServoState != 3) {
                std::cerr << "Non-running servo state detected (state="
                          << liveServoState
                          << "); stopping all jogging before recovery.\n";
                for (std::size_t i = 0; i < activeRequests.size(); ++i) {
                    if (activeRequests[i].axis != 0 || activeAxes[i]) {
                        stopJog(socket, static_cast<int>(i) + 1);
                    }
                    activeRequests[i] = {};
                    activeAxes[i] = false;
                }

                if (!ensureServoRunning(socket, poweredOnByDemo, false)) {
                    break;
                }
#ifdef __linux__
                // 故障恢复期间可能积压按键；恢复后要求操作者重新按键。
                if (!keyReader.discardPendingEvents()) {
                    std::cerr << keyReader.error() << "; stopping for safety.\n";
                    break;
                }
#endif
                nextServoHealthCheck = std::chrono::steady_clock::now()
                    + kServoHealthCheckPeriod;
                continue;
            }

            nextServoHealthCheck = now + kServoHealthCheckPeriod;
        }

        // 将当前按键快照转换为每个轴的目标请求，再与上一周期状态比较。
        const auto requestedRequests = makeJogRequests(keys);

        for (std::size_t i = 0; i < activeRequests.size(); ++i) {
            JogRequest& activeRequest = activeRequests[i];
            const JogRequest& requestedRequest = requestedRequests[i];

            if (requestedRequest != activeRequest) {
                // 状态变化时先停止旧请求，再启动新请求，避免同轴方向重叠。
                if (activeRequest.axis != 0) {
                    stopJog(socket, activeRequest.axis);
                    activeAxes[i] = false;
                }

                activeRequest = requestedRequest;
                if (activeRequest.axis != 0) {
                    // 每个轴状态变化时立即发首包，不等待下一个100 ms周期。
                    if (!sendJog(socket, activeRequest)) {
                        activeRequest = {};
                        g_exitRequested = true;
                        break;
                    }
                    activeAxes[i] = true;
                    std::cout << "[JOG] START axis=" << activeRequest.axis
                              << " direction="
                              << (activeRequest.positive ? "positive" : "negative")
                              << " result=SUCCESS\n";
                    nextJogSend[i] = now + kJogPeriod;
                }
            } else if (activeRequest.axis != 0 && now >= nextJogSend[i]) {
                if (!sendJog(socket, activeRequest)) {
                    stopJog(socket, activeRequest.axis);
                    activeAxes[i] = false;
                    activeRequest = {};
                    g_exitRequested = true;
                    break;
                }
                nextJogSend[i] = now + kJogPeriod;
            }
        }

        std::this_thread::sleep_for(kKeyboardPollPeriod);
    }

    cleanup();
    std::cout << "Stopped and disconnected.\n";
    return 0;
}
