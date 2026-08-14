#ifndef KEYBOARD_JOG_DEMO_ROBOT_DEBUG_MENU_H
#define KEYBOARD_JOG_DEMO_ROBOT_DEBUG_MENU_H

#include "robot_control/robot_jog_controller.h"
#include "terminal/keyboard_teleop_action.h"

#include <functional>
#include <iosfwd>

// 数字菜单只负责功能选择；机器人状态和运动仍由 RobotJogController 管理。
class RobotDebugMenu
{
public:
    using TeleopAction = std::function<TeleopResult()>;

    RobotDebugMenu(RobotJogController& robot,
                   RobotJogController::ExitRequested exitRequested,
                   TeleopAction teleopAction,
                   std::istream& input,
                   std::ostream& output,
                   std::ostream& error);

    // 运行菜单直到输入0、收到退出信号或输入流关闭。
    int run();

private:
    bool readChoice(const char* prompt, int& choice);
    void showMainMenu();
    void runModeMenu();
    void runPowerOn();
    void runPowerOff();
    void runJointStep();
    void runCartesianStep();
    void runReadRobotInfo();
    void runTcpMenu();
    void runQueueTest();

    RobotJogController& robot_;
    RobotJogController::ExitRequested exitRequested_;
    TeleopAction teleopAction_;
    std::istream& input_;
    std::ostream& output_;
    std::ostream& error_;
};

#endif
