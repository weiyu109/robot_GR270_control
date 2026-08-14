#include "robot_control/robot_jog_controller.h"

#include <algorithm>
#include <iostream>
#include <vector>

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

// 关节相对控制：读取当前关节数组，只修改指定关节后发送一次 robot_movej。
bool RobotJogController::moveJointRelative(int jointNumber,
                                           double deltaDegrees,
                                           double velocityPercent)
{
    if (!connected_ || jointNumber < 1 || jointNumber > 7) {
        return false;
    }
    std::vector<double> currentJoints;
    if (!requireSuccess(sdk_.getCurrentPosition(socket_, 0, currentJoints),
                        "get_current_position(joint)")) {
        return false;
    }
    if (currentJoints.size() < 7) {
        std::cerr << "Joint position query returned fewer than 7 values.\n";
        return false;
    }

    JointMoveCommand command;
    std::copy_n(currentJoints.begin(), 7, command.targetDegrees.begin());
    command.axisCount = 6;
    command.targetDegrees[static_cast<std::size_t>(jointNumber - 1)] += deltaDegrees;
    command.velocityPercent = velocityPercent;
    return moveJoints(command);
}
