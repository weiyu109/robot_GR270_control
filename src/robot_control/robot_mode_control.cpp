#include "robot_control/robot_jog_controller.h"

#include <iostream>

// 切换控制模式：Teach=示教、Remote=远程、Run=运行；点动期间禁止切换。
bool RobotJogController::setMode(RobotMode mode)
{
    if (hasActiveJogging()) {
        std::cerr << "Refusing to switch controller mode while jogging is active.\n";
        return false;
    }
    return connected_
        && requireSuccess(sdk_.setMode(socket_, static_cast<int>(mode)), "set_current_mode");
}
