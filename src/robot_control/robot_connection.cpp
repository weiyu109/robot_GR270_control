#include "robot_control/robot_jog_controller.h"

#include <iostream>

// 连接控制器：先清理本对象旧会话，再通过厂商 SDK 建立唯一控制连接。
bool RobotJogController::connect(const std::string& ip,
                                 const std::string& port,
                                 ControllerMessageCallback messageCallback)
{
    shutdown();

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
        shutdown();
        return false;
    }
    return true;
}

// 断开连接：对外提供语义明确的安全断开入口。
void RobotJogController::disconnect()
{
    shutdown();
}

// 安全退出：停止点动、按会话责任下电、恢复初始状态，最后断开 SDK。
void RobotJogController::shutdown()
{
    if (!connected_) {
        return;
    }

    joggingEnabled_ = false;
    stopAllJogging();

    // 只关闭本会话开启的伺服电源，避免破坏进入程序前已存在的外部会话状态。
    if (poweredOnBySession_) {
        const Result result = sdk_.powerOff(socket_);
        if (result != SUCCESS) {
            std::cerr << "set_servo_poweroff failed: " << sdkResultText(result)
                      << " (" << static_cast<int>(result) << ")\n";
        } else {
            waitForServoState(static_cast<int>(ServoState::Ready));
        }
    }

    if (initialServoState_ == static_cast<int>(ServoState::Stopped)) {
        requireSuccess(sdk_.setServoState(socket_, static_cast<int>(ServoState::Stopped)),
                       "restore servo stopped state");
    }
    if (previousMode_ >= 0 && previousMode_ != static_cast<int>(RobotMode::Teach)) {
        requireSuccess(sdk_.setMode(socket_, previousMode_), "restore current mode");
    }

    requireSuccess(sdk_.disconnect(socket_), "disconnect_robot");
    socket_ = -1;
    connected_ = false;
    joggingEnabled_ = false;
    poweredOnBySession_ = false;
    previousMode_ = -1;
    initialServoState_ = -1;
    activeAxes_.fill(false);
}
