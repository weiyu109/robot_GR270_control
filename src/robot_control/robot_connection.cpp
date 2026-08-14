#include "robot_control/robot_jog_controller.h"

#include <iostream>

// 连接控制器：先清理本对象旧会话，再通过厂商 SDK 建立唯一控制连接。
bool RobotJogController::connect(const std::string& ip,
                                 const std::string& port,
                                 ControllerMessageCallback messageCallback)
{
    // 旧会话没有安全退出时禁止建立第二条连接，避免丢失旧控制句柄。
    if (!shutdown()) {
        std::cerr << "Previous robot session is not safely closed; refusing to reconnect.\n";
        return false;
    }

    // 不建立额外裸 TCP 探测连接；6001 端口可能只允许一个控制会话。
    socket_ = sdk_.connect(ip, port);
    if (socket_ < 0) {
        std::cerr << "connect_robot failed.\n";
        return false;
    }
    connected_ = true;

    // 只注册普通消息回调；运行中错误判断由调用方主动查询伺服状态。
    if (messageCallback != nullptr
        && !requireSuccess(sdk_.receiveMessages(socket_, messageCallback), "recv_message")) {
        if (!shutdown()) {
            std::cerr << "Failed to safely close SDK session after recv_message failure.\n";
        }
        return false;
    }
    return true;
}

// 断开连接：对外提供语义明确的安全断开入口。
bool RobotJogController::disconnect()
{
    return shutdown();
}

// 安全退出：只有运动停止和下电状态已经确认后，才允许断开 SDK。
bool RobotJogController::shutdown()
{
    if (!connected_) {
        return true;
    }

    joggingEnabled_ = false;
    const bool allJoggingStopped = stopAllJogging();
    bool safeStateConfirmed = allJoggingStopped;

    // 本会话负责上电时，下电成功并确认 Ready 可以作为点动停止失败的安全兜底。
    if (poweredOnBySession_ || powerOffRequiredOnShutdown_) {
        int state = -1;
        if (!getServoState(state)) {
            std::cerr << "Cannot confirm servo state; keeping SDK connection for retry.\n";
            return false;
        }

        if (state == static_cast<int>(ServoState::Running)) {
            const Result result = sdk_.powerOff(socket_);
            if (result != SUCCESS) {
                std::cerr << "set_servo_poweroff failed: " << sdkResultText(result)
                          << " (" << static_cast<int>(result)
                          << "); keeping SDK connection for retry.\n";
                return false;
            }
            if (!waitForServoState(static_cast<int>(ServoState::Ready))) {
                std::cerr << "Power-off state is unconfirmed; keeping SDK connection for retry.\n";
                return false;
            }
            safeStateConfirmed = true;
        } else if (state == static_cast<int>(ServoState::Ready)
                   || state == static_cast<int>(ServoState::Stopped)) {
            safeStateConfirmed = true;
        } else {
            std::cerr << "Servo is in unsafe or unknown state " << state
                      << "; keeping SDK connection for recovery.\n";
            return false;
        }
        poweredOnBySession_ = false;
    }

    if (!safeStateConfirmed) {
        std::cerr << "Jog stop is unconfirmed; keeping SDK connection for retry.\n";
        return false;
    }

    if (initialServoState_ == static_cast<int>(ServoState::Stopped)) {
        requireSuccess(sdk_.setServoState(socket_, static_cast<int>(ServoState::Stopped)),
                       "restore servo stopped state");
    }
    if (previousMode_ >= 0 && previousMode_ != static_cast<int>(RobotMode::Teach)) {
        requireSuccess(sdk_.setMode(socket_, previousMode_), "restore current mode");
    }

    if (!requireSuccess(sdk_.disconnect(socket_), "disconnect_robot")) {
        std::cerr << "SDK disconnect failed; preserving connection state for retry.\n";
        return false;
    }
    socket_ = -1;
    connected_ = false;
    joggingEnabled_ = false;
    poweredOnBySession_ = false;
    powerOffRequiredOnShutdown_ = false;
    previousMode_ = -1;
    initialServoState_ = -1;
    activeAxes_.fill(false);
    return true;
}
