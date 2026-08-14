#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "cpp/interface/nrc_api.h"

namespace {

constexpr int kCartesianCoord = 1;
constexpr int kXAxis = 1;
constexpr int kYAxis = 2;
constexpr int kTeachMode = 0;
constexpr int kJogTeachType = 0;
constexpr int kDefaultSpeedPercent = 10;
constexpr auto kJogPeriod = std::chrono::milliseconds(100);
constexpr auto kKeyboardPollPeriod = std::chrono::milliseconds(10);
constexpr auto kServoStatePollPeriod = std::chrono::milliseconds(100);
constexpr auto kServoStateTimeout = std::chrono::seconds(5);
constexpr auto kServoRecoveryPeriod = std::chrono::milliseconds(300);
constexpr auto kServoRecoveryTimeout = std::chrono::seconds(30);

std::atomic_bool g_exitRequested{false};

struct JogRequest {
    int axis{0};
    bool positive{false};

    bool operator==(const JogRequest& other) const
    {
        return axis == other.axis && positive == other.positive;
    }

    bool operator!=(const JogRequest& other) const
    {
        return !(*this == other);
    }
};

struct KeySnapshot {
    bool up{false};
    bool down{false};
    bool left{false};
    bool right{false};
    bool escape{false};

    bool operator==(const KeySnapshot& other) const
    {
        return up == other.up && down == other.down
            && left == other.left && right == other.right
            && escape == other.escape;
    }

    bool operator!=(const KeySnapshot& other) const
    {
        return !(*this == other);
    }
};

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

void controllerErrorCallback(int messageType, const char* message, int messageCode)
{
    std::cerr << "[CONTROLLER ERROR/WARNING] type=" << messageType
              << " code=" << messageCode
              << " message=" << (message != nullptr ? message : "") << '\n';
}

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
                    }
                    continue;
                }
                if (record.EventType != KEY_EVENT) {
                    continue;
                }

                const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
                const bool down = key.bKeyDown != FALSE;
                switch (key.wVirtualKeyCode) {
                case VK_UP: keys_.up = down; break;
                case VK_DOWN: keys_.down = down; break;
                case VK_LEFT: keys_.left = down; break;
                case VK_RIGHT: keys_.right = down; break;
                case VK_ESCAPE: keys_.escape = down; break;
                default: break;
                }
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
    KeySnapshot keys_;
};

std::array<JogRequest, 2> makeJogRequests(const KeySnapshot& keys)
{
    std::array<JogRequest, 2> requests{};

    // 同一轴的正负方向互斥；X、Y两个正交轴可以同时点动。
    if (keys.up != keys.down) {
        requests[0] = {kXAxis, keys.up};
    }
    if (keys.left != keys.right) {
        requests[1] = {kYAxis, keys.left};
    }
    return requests;
}

bool sendJog(SOCKETFD socket, const JogRequest& request)
{
    static unsigned long long callCount = 0;
    ++callCount;
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

bool ensureServoRunning(SOCKETFD socket, bool& poweredOnByDemo)
{
    const auto deadline = std::chrono::steady_clock::now() + kServoRecoveryTimeout;
    bool mustPowerCycleAfterAlarm = false;
    int clearAttempts = 0;
    int powerOnAttempts = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        int state = -1;
        if (!requireSuccess(get_servo_state(socket, state), "get_servo_state")) {
            return false;
        }

        if (state == 2) {
            mustPowerCycleAfterAlarm = true;
            ++clearAttempts;
            std::cout << "Servo alarm detected. clear_error attempt "
                      << clearAttempts << "...\n";
            const Result result = clear_error(socket);
            if (result != SUCCESS) {
                std::cerr << "clear_error failed: " << resultText(result)
                          << " (" << static_cast<int>(result)
                          << "); retrying...\n";
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == 3) {
            if (!mustPowerCycleAfterAlarm) {
                std::cout << "Servo is running.\n";
                return true;
            }

            // SDK 要求：若报警前处于运行状态，清错后必须先下电释放占用，
            // 然后才能再次上电。
            std::cout << "Alarm cleared to running state; powering off before restart...\n";
            if (!requireSuccess(set_servo_poweroff(socket),
                                "set_servo_poweroff(after clear_error)")) {
                return false;
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == 0) {
            std::cout << "Setting servo to ready state...\n";
            if (!requireSuccess(set_servo_state(socket, 1),
                                "set_servo_state(ready)")) {
                return false;
            }
            std::this_thread::sleep_for(kServoRecoveryPeriod);
            continue;
        }

        if (state == 1) {
            ++powerOnAttempts;
            std::cout << "Servo ready. Power-on attempt " << powerOnAttempts << "...\n";
            const Result result = set_servo_poweron(socket);
            if (result == SUCCESS) {
                poweredOnByDemo = true;
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

    std::cerr << "Timed out after "
              << std::chrono::duration_cast<std::chrono::seconds>(kServoRecoveryTimeout).count()
              << " seconds while clearing alarms and powering on. "
                 "Check emergency stop, safety circuit, and the controller alarm details.\n";
    return false;
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

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    std::cout << "Connecting to " << ip << ':' << port << " ...\n";
    const SOCKETFD socket = connect_robot(ip, port);
    if (socket < 0) {
        std::cerr << "connect_robot failed.\n";
        return 1;
    }

    if (!requireSuccess(
            set_receive_error_or_warnning_message_callback(socket, controllerErrorCallback),
            "set_receive_error_or_warnning_message_callback")
        || !requireSuccess(recv_message(socket, controllerMessageCallback),
                           "recv_message")) {
        disconnect_robot(socket);
        return 1;
    }

    bool connected = true;
    bool poweredOnByDemo = false;
    std::array<bool, 2> activeAxes{false, false};
    int previousMode = -1;
    int initialServoState = -1;
    auto cleanup = [&]() {
        if (!connected) {
            return;
        }

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
        << "Type ENABLE and press Enter to enter teach/jog mode and power on: ";
    std::string confirmation;
    std::getline(std::cin, confirmation);
    if (confirmation != "ENABLE") {
        std::cout << "Cancelled.\n";
        cleanup();
        return 0;
    }

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

    if (!ensureServoRunning(socket, poweredOnByDemo)) {
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
        << "X and Y may be jogged together; opposite keys on the same axis cancel that axis.\n"
        << "Release the key to stop. Press ESC or Ctrl+C to exit.\n"
        << "Speed: " << speed << "%\n";

    std::array<JogRequest, 2> activeRequests{};
    ConsoleKeyReader keyReader;
    if (!keyReader.valid()) {
        std::cerr << "Failed to initialize Windows console key-event reader.\n";
        cleanup();
        return 1;
    }
    std::cout << "[INPUT] Windows console key-event reader active.\n";
    std::array<std::chrono::steady_clock::time_point, 2> nextJogSend{
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now()
    };

    while (!g_exitRequested) {
        KeySnapshot keys;
        if (!keyReader.poll(keys)) {
            std::cerr << "Console input read failed; stopping for safety.\n";
            break;
        }

        if (keys.escape) {
            break;
        }

        const auto requestedRequests = makeJogRequests(keys);
        const auto now = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < activeRequests.size(); ++i) {
            JogRequest& activeRequest = activeRequests[i];
            const JogRequest& requestedRequest = requestedRequests[i];

            if (requestedRequest != activeRequest) {
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
