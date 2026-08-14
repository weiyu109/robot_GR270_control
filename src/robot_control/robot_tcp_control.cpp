#include "robot_control/robot_jog_controller.h"

// 读取当前激活工具手编号，以及该编号对应的TCP和负载参数。
bool RobotJogController::getCurrentTcp(int& toolNumber, ToolParam& param)
{
    if (!connected_
        || !requireSuccess(sdk_.getToolNumber(socket_, toolNumber),
                           "get_tool_hand_number")) {
        return false;
    }
    return requireSuccess(sdk_.getToolParam(socket_, toolNumber, param),
                          "get_tool_hand_param");
}

// 修改当前TCP的Z偏移；先读取原参数，避免覆盖负载、质心和其他TCP分量。
bool RobotJogController::setCurrentTcpZ(double zMillimeters)
{
    int toolNumber = -1;
    ToolParam param{};
    if (!getCurrentTcp(toolNumber, param)) {
        return false;
    }
    param.Z = zMillimeters;
    return requireSuccess(sdk_.setToolParam(socket_, toolNumber, param),
                          "set_tool_hand_param");
}

// TCP归零只清除XYZABC六个几何分量，保留负载、惯量和质心参数。
bool RobotJogController::resetCurrentTcpPose()
{
    int toolNumber = -1;
    ToolParam param{};
    if (!getCurrentTcp(toolNumber, param)) {
        return false;
    }
    param.X = 0.0;
    param.Y = 0.0;
    param.Z = 0.0;
    param.A = 0.0;
    param.B = 0.0;
    param.C = 0.0;
    return requireSuccess(sdk_.setToolParam(socket_, toolNumber, param),
                          "set_tool_hand_param");
}
