#include "robot_control/robot_jog_controller.h"

#include <algorithm>
#include <iostream>
#include <vector>

// XYZ 位置控制：读取当前姿态，只替换 X/Y/Z，再调用 robot_movel 做直线运动。
bool RobotJogController::moveCartesianXyz(const CartesianXyzCommand& command)
{
    if (command.coordinate < 1 || command.coordinate > 3
        || command.velocityMillimetersPerSecond <= 0.0
        || command.velocityMillimetersPerSecond > 1000.0
        || command.accelerationPercent <= 0.0 || command.accelerationPercent > 100.0
        || command.decelerationPercent <= 0.0 || command.decelerationPercent > 100.0
        || command.toolNumber < 0 || command.userNumber < 0
        || !readyForPositionMotion()) {
        return false;
    }

    std::vector<double> currentPosition;
    if (!requireSuccess(
            sdk_.getCurrentPosition(socket_, command.coordinate, currentPosition),
            "get_current_position")) {
        return false;
    }
    if (currentPosition.size() < 7) {
        std::cerr << "get_current_position returned fewer than 7 values.\n";
        return false;
    }

    MoveCmd sdkCommand;
    sdkCommand.targetPosType = PosType::data;
    std::fill(sdkCommand.targetPosValue.begin(), sdkCommand.targetPosValue.end(), 0.0);
    std::copy_n(currentPosition.begin(), 7, sdkCommand.targetPosValue.begin());
    sdkCommand.targetPosValue[0] = command.xMillimeters;
    sdkCommand.targetPosValue[1] = command.yMillimeters;
    sdkCommand.targetPosValue[2] = command.zMillimeters;
    sdkCommand.coord = command.coordinate;
    sdkCommand.velocity = command.velocityMillimetersPerSecond;
    sdkCommand.acc = command.accelerationPercent;
    sdkCommand.dec = command.decelerationPercent;
    sdkCommand.toolNum = command.toolNumber;
    sdkCommand.userNum = command.userNumber;
    return requireSuccess(sdk_.moveLinear(socket_, sdkCommand), "robot_movel");
}
