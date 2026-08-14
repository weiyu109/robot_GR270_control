#include "robot_control/robot_jog_controller.h"

#include <algorithm>

// 关节位置控制：把 J1..Jn 目标角度封装为 MoveCmd，并调用 robot_movej。
bool RobotJogController::moveJoints(const JointMoveCommand& command)
{
    if (command.axisCount <= 0 || command.axisCount > 7
        || command.velocityPercent <= 0.0 || command.velocityPercent > 100.0
        || command.accelerationPercent <= 0.0 || command.accelerationPercent > 100.0
        || command.decelerationPercent <= 0.0 || command.decelerationPercent > 100.0
        || !readyForPositionMotion()) {
        return false;
    }

    MoveCmd sdkCommand;
    sdkCommand.targetPosType = PosType::data;
    std::fill(sdkCommand.targetPosValue.begin(), sdkCommand.targetPosValue.end(), 0.0);
    for (int i = 0; i < command.axisCount; ++i) {
        sdkCommand.targetPosValue[static_cast<std::size_t>(i)] =
            command.targetDegrees[static_cast<std::size_t>(i)];
    }
    sdkCommand.coord = 0;
    sdkCommand.velocity = command.velocityPercent;
    sdkCommand.acc = command.accelerationPercent;
    sdkCommand.dec = command.decelerationPercent;
    return requireSuccess(sdk_.moveJoint(socket_, sdkCommand), "robot_movej");
}
