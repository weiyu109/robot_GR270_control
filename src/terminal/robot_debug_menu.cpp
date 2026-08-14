#include "terminal/robot_debug_menu.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kJointStepDegrees = 1.0;
constexpr double kJointVelocityPercent = 10.0;
constexpr double kCartesianStepMillimeters = 50.0;
constexpr double kCartesianVelocityMillimetersPerSecond = 20.0;

} // namespace

RobotDebugMenu::RobotDebugMenu(RobotJogController& robot,
                               RobotJogController::ExitRequested exitRequested,
                               TeleopAction teleopAction,
                               std::istream& input,
                               std::ostream& output,
                               std::ostream& error)
    : robot_(robot),
      exitRequested_(std::move(exitRequested)),
      teleopAction_(std::move(teleopAction)),
      input_(input),
      output_(output),
      error_(error)
{
}

// 严格读取一个整数；多余字符或空输入不会触发任何机器人动作。
bool RobotDebugMenu::readChoice(const char* prompt, int& choice)
{
    output_ << prompt << std::flush;
    std::string line;
    if (!std::getline(input_, line)) {
        return false;
    }
    std::istringstream parser(line);
    char extra = '\0';
    if (!(parser >> choice) || (parser >> extra)) {
        error_ << "输入无效，请输入菜单中的整数序号。\n";
        choice = -1;
    }
    return true;
}

// 打印主菜单；输入序号并按Enter后直接执行对应功能。
void RobotDebugMenu::showMainMenu()
{
    output_
        << "\n========== GR270 调试菜单 ==========\n"
        << "1. 循环安全上电\n"
        << "2. 安全下电\n"
        << "3. 切换控制模式\n"
        << "4. J1 相对运动 +1 度\n"
        << "5. X 相对运动 +50 mm（速度 20 mm/s）\n"
        << "6. 键盘 XYZ 遥操作\n"
        << "7. 读取末端位姿、关节和当前 TCP\n"
        << "8. 设置当前 TCP\n"
        << "9. 运行 50 点阶梯队列测试\n"
        << "10. 循环清错（不上电）\n"
        << "0. 安全退出\n";
}

// 循环上电菜单动作：持续清错、Ready、上电，直到确认Running或退出。
void RobotDebugMenu::runPowerOn()
{
    output_ << "开始循环安全上电...\n";
    if (robot_.powerOnUntilRunning(exitRequested_)) {
        output_ << "上电完成：伺服已确认 Running(state=3)。\n";
    } else if (!exitRequested_()) {
        error_ << "循环上电失败。\n";
    }
}

// 安全下电菜单动作：停止运动并循环到Ready/Stopped。
void RobotDebugMenu::runPowerOff()
{
    output_ << "开始安全下电...\n";
    if (robot_.powerOffUntilReady(exitRequested_)) {
        output_ << "下电完成：伺服已确认 Ready/Stopped。\n";
    } else if (!exitRequested_()) {
        error_ << "安全下电失败。\n";
    }
}

// 模式切换二级菜单：1示教、2远程、3运行、0返回。
void RobotDebugMenu::runModeMenu()
{
    output_
        << "\n1. 示教模式\n"
        << "2. 远程模式\n"
        << "3. 运行模式\n"
        << "0. 返回\n";
    int choice = -1;
    if (!readChoice("请选择模式：", choice) || choice == 0) {
        return;
    }

    RobotMode mode;
    switch (choice) {
    case 1: mode = RobotMode::Teach; break;
    case 2: mode = RobotMode::Remote; break;
    case 3: mode = RobotMode::Run; break;
    default:
        error_ << "模式序号无效。\n";
        return;
    }
    if (robot_.setMode(mode)) {
        output_ << "模式切换指令执行成功。\n";
        robot_.printState();
    } else {
        error_ << "模式切换失败。\n";
    }
}

// J1相对运动菜单动作：读取当前位置后直接发送J1+1度，不进行二次确认。
void RobotDebugMenu::runJointStep()
{
    if (robot_.moveJointRelative(1, kJointStepDegrees, kJointVelocityPercent)) {
        output_ << "已发送 J1 +1 度关节运动指令。\n";
    } else {
        error_ << "J1 +1 度运动指令失败；请确认已经上电。\n";
    }
}

// X相对运动菜单动作：读取当前位置后直接发送X+50mm、20mm/s，不二次确认。
void RobotDebugMenu::runCartesianStep()
{
    if (robot_.moveCartesianXRelative(kCartesianStepMillimeters,
                                      kCartesianVelocityMillimetersPerSecond)) {
        output_ << "已发送 X +50 mm 直线运动指令，速度 20 mm/s。\n";
    } else {
        error_ << "X +50 mm 运动指令失败；请确认已经上电。\n";
    }
}

// 一次读取并打印末端直角位姿、关节数组和当前激活TCP参数。
void RobotDebugMenu::runReadRobotInfo()
{
    std::vector<double> cartesian;
    std::vector<double> joints;
    int toolNumber = -1;
    ToolParam tcp{};
    if (!robot_.getCurrentPosition(1, cartesian)
        || !robot_.getCurrentPosition(0, joints)
        || !robot_.getCurrentTcp(toolNumber, tcp)) {
        error_ << "读取机器人位置或TCP失败。\n";
        return;
    }

    const auto oldFlags = output_.flags();
    const auto oldPrecision = output_.precision();
    output_ << std::fixed << std::setprecision(3);
    output_ << "末端位姿(coord=1)：";
    for (double value : cartesian) {
        output_ << ' ' << value;
    }
    output_ << "\n关节位置(coord=0)：";
    for (double value : joints) {
        output_ << ' ' << value;
    }
    output_ << "\n当前TCP编号=" << toolNumber
            << " XYZABC=[" << tcp.X << ", " << tcp.Y << ", " << tcp.Z
            << ", " << tcp.A << ", " << tcp.B << ", " << tcp.C << "]\n";
    output_.flags(oldFlags);
    output_.precision(oldPrecision);
}

// TCP设置子菜单：把当前激活TCP的Z设为-342mm，或把六个几何分量归零。
void RobotDebugMenu::runTcpMenu()
{
    output_
        << "\n1. 当前TCP设置 Z=-342 mm\n"
        << "2. 当前TCP的 XYZABC 全部归零\n"
        << "0. 返回\n";
    int choice = -1;
    if (!readChoice("请选择TCP操作：", choice) || choice == 0) {
        return;
    }
    if (choice == 1) {
        if (robot_.setCurrentTcpZ(-342.0)) {
            output_ << "当前TCP的Z已设置为 -342 mm。\n";
        } else {
            error_ << "设置TCP Z失败。\n";
        }
    } else if (choice == 2) {
        if (robot_.resetCurrentTcpPose()) {
            output_ << "当前TCP的XYZABC已归零。\n";
        } else {
            error_ << "TCP归零失败。\n";
        }
    } else {
        error_ << "TCP操作序号无效。\n";
    }
}

// 发送50点阶梯MoveL队列；等待执行完毕后关闭队列模式并自动下电。
void RobotDebugMenu::runQueueTest()
{
    output_ << "开始发送50点阶梯队列：X总前进100mm，速度100mm/s...\n";
    if (robot_.runStairQueueTest(exitRequested_)) {
        output_ << "50点队列执行完成：队列模式已关闭，机械臂已下电。\n";
    } else if (!exitRequested_()) {
        error_ << "阶梯队列发送失败；请确认机械臂已经上电。\n";
    } else {
        output_ << "收到退出请求：已进入关闭队列模式和下电清理流程。\n";
    }
}

// 独立循环清错菜单动作：报警解除后停在Ready；已经Running时保持当前状态，不执行上电。
void RobotDebugMenu::runClearError()
{
    output_ << "开始循环清错（不会执行上电）...\n";
    if (robot_.clearErrorUntilReady(exitRequested_)) {
        output_ << "清错完成：伺服已处于 Ready/Running。\n";
    } else if (!exitRequested_()) {
        error_ << "循环清错失败。\n";
    }
}

// 主菜单循环：每个动作结束后回到菜单；Esc只由遥操作处理并返回本菜单。
int RobotDebugMenu::run()
{
    while (!exitRequested_()) {
        showMainMenu();
        int choice = -1;
        if (!readChoice("请输入序号并按 Enter：", choice)) {
            return 0;
        }
        switch (choice) {
        case 0: return 0;
        case 1: runPowerOn(); break;
        case 2: runPowerOff(); break;
        case 3: runModeMenu(); break;
        case 4: runJointStep(); break;
        case 5: runCartesianStep(); break;
        case 6: {
            const TeleopResult result = teleopAction_();
            if (result == TeleopResult::ExitProgram) {
                return 0;
            }
            if (result == TeleopResult::Failed) {
                error_ << "键盘遥操作异常，已返回主菜单。\n";
            }
            break;
        }
        case 7: runReadRobotInfo(); break;
        case 8: runTcpMenu(); break;
        case 9: runQueueTest(); break;
        case 10: runClearError(); break;
        default: error_ << "菜单序号无效。\n"; break;
        }
    }
    return 0;
}
