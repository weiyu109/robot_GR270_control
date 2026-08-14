#include "robot_control/robot_jog_controller.h"

#include <algorithm>
#include <iostream>

// 构造函数：只保存 SDK 适配器，不连接控制器，也不改变机械臂状态。
RobotJogController::RobotJogController(RobotSdkApi& sdk)
    : sdk_(sdk)
{
}

// 析构函数：兜底执行安全退出，确保窗口关闭时也走停止、下电和断开流程。
RobotJogController::~RobotJogController()
{
    shutdown();
}

// SDK 结果检查：统一打印函数名、错误文本和错误码。
bool RobotJogController::requireSuccess(Result result, const char* operation) const
{
    if (result == SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed: " << sdkResultText(result)
              << " (" << static_cast<int>(result) << ")\n";
    return false;
}

// 点动状态检查：判断本会话是否仍记录着未停止的轴。
bool RobotJogController::hasActiveJogging() const
{
    return std::any_of(activeAxes_.begin(), activeAxes_.end(), [](bool active) {
        return active;
    });
}

// 位置运动前置检查：必须已连接、伺服运行，并且没有持续点动命令。
bool RobotJogController::readyForPositionMotion()
{
    if (!connected_ || hasActiveJogging()) {
        std::cerr << "Position motion requires a connection and no active jogging.\n";
        return false;
    }
    int state = -1;
    if (!getServoState(state) || state != static_cast<int>(ServoState::Running)) {
        std::cerr << "Position motion requires servo Running state (state=3).\n";
        return false;
    }
    return true;
}

// 伺服状态查询：读取控制器当前的 0/1/2/3 状态。
bool RobotJogController::getServoState(int& state)
{
    return connected_
        && requireSuccess(sdk_.getServoState(socket_, state), "get_servo_state");
}

// 状态打印：集中输出模式、示教类型、坐标系、速度和伺服状态。
bool RobotJogController::printState()
{
    if (!connected_) {
        return false;
    }

    int mode = -1;
    int teachType = -1;
    int coordinate = -1;
    int speed = -1;
    int servoState = -1;
    if (!requireSuccess(sdk_.getMode(socket_, mode), "get_current_mode(diagnostic)")
        || !requireSuccess(sdk_.getTeachType(socket_, teachType),
                           "get_teach_type(diagnostic)")
        || !requireSuccess(sdk_.getCoordinate(socket_, coordinate),
                           "get_current_coord(diagnostic)")
        || !requireSuccess(sdk_.getSpeed(socket_, speed), "get_speed(diagnostic)")
        || !getServoState(servoState)) {
        return false;
    }

    std::cout << "[STATE] mode=" << mode
              << " (0=teach,1=remote,2=run)"
              << " teachType=" << teachType
              << " (0=jog,1=drag)"
              << " coord=" << coordinate
              << " (0=joint,1=cartesian,2=tool,3=user)"
              << " speed=" << speed
              << "% servo=" << servoState
              << " (0=stopped,1=ready,2=alarm,3=running)\n";
    return true;
}
