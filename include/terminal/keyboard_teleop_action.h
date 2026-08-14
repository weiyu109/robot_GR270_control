#ifndef KEYBOARD_JOG_DEMO_KEYBOARD_TELEOP_ACTION_H
#define KEYBOARD_JOG_DEMO_KEYBOARD_TELEOP_ACTION_H

#include "input/key_reader.h"
#include "robot_control/robot_jog_controller.h"

enum class TeleopResult {
    ReturnToMenu,
    ExitProgram,
    Failed
};

// 运行现有 XYZ 键盘遥操作；Esc 返回菜单，外部退出信号结束整个程序。
TeleopResult runKeyboardTeleop(RobotJogController& robot,
                               KeyReader& keyReader,
                               int speedPercent,
                               const RobotJogController::ExitRequested& exitRequested);

#endif
