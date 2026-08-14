#include "robot_control/robot_jog_controller.h"

#include <iostream>

// 设置点动速度：写入控制器全局速度百分比，范围为 1..100。
bool RobotJogController::setJogSpeedPercent(int speedPercent)
{
    if (!connected_ || speedPercent <= 0 || speedPercent > 100) {
        return false;
    }
    return requireSuccess(sdk_.setSpeed(socket_, speedPercent), "set_speed");
}

// 设置点动坐标系：切换前不得存在活动点动轴，避免轴号语义在运动中改变。
bool RobotJogController::setJogCoordinate(int coordinate)
{
    if (!connected_ || coordinate < 0 || coordinate > 3 || hasActiveJogging()) {
        return false;
    }
    return requireSuccess(sdk_.setCoordinate(socket_, coordinate), "set_current_coord");
}

// 开始轴点动：按设定速度沿指定轴持续运动，直到显式调用 stopJog()。
bool RobotJogController::startJog(const JogRequest& request)
{
    if (!connected_ || !joggingEnabled_
        || request.axis < 1
        || request.axis > static_cast<int>(activeAxes_.size())) {
        return false;
    }

    const Result result = sdk_.startJog(socket_, request.axis, request.positive);
    std::cout << "[JOG API] robot_start_jogging axis=" << request.axis
              << " direction=" << (request.positive ? "positive" : "negative")
              << " result=" << sdkResultText(result)
              << " (" << static_cast<int>(result) << ")\n";
    if (result != SUCCESS) {
        return false;
    }
    activeAxes_[static_cast<std::size_t>(request.axis - 1)] = true;
    return true;
}

// 停止单轴点动：松键时调用，对应厂商 robot_stop_jogging。
bool RobotJogController::stopJog(int axis)
{
    if (!connected_ || axis < 1 || axis > static_cast<int>(activeAxes_.size())) {
        return false;
    }

    const Result result = sdk_.stopJog(socket_, axis);
    std::cout << "[JOG API] robot_stop_jogging axis=" << axis
              << " result=" << sdkResultText(result)
              << " (" << static_cast<int>(result) << ")\n";
    if (result == SUCCESS) {
        activeAxes_[static_cast<std::size_t>(axis - 1)] = false;
        return true;
    }
    return false;
}

// 停止全部点动：只停止本会话已经启动并记录的轴，供异常、下电和退出复用。
bool RobotJogController::stopAllJogging()
{
    if (!connected_) {
        activeAxes_.fill(false);
        return true;
    }
    bool allStopped = true;
    for (std::size_t i = 0; i < activeAxes_.size(); ++i) {
        if (activeAxes_[i]) {
            if (!stopJog(static_cast<int>(i) + 1)) {
                allStopped = false;
            }
        }
    }
    return allStopped;
}
