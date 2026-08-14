#include "robot_control/robot_jog_controller.h"

// 读取机器人当前位置：0返回关节数组，1返回末端直角位姿数组。
bool RobotJogController::getCurrentPosition(int coordinate,
                                            std::vector<double>& position)
{
    if (!connected_ || (coordinate != 0 && coordinate != 1)) {
        return false;
    }
    return requireSuccess(sdk_.getCurrentPosition(socket_, coordinate, position),
                          "get_current_position");
}
