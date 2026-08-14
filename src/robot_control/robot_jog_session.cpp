#include "robot_control/robot_jog_controller.h"

namespace {

constexpr int kTeachTypeJog = 0;

} // namespace

// 进入示教点动会话：切模式、循环恢复上电、设置坐标系和速度后开放点动。
bool RobotJogController::enterTeachJog(const JogSessionConfig& config,
                                       const ExitRequested& exitRequested)
{
    if (!connected_ || config.speedPercent <= 0 || config.speedPercent > 100) {
        return false;
    }

    if ((previousMode_ < 0
         && !requireSuccess(sdk_.getMode(socket_, previousMode_), "get_current_mode"))
        || !setMode(RobotMode::Teach)
        || !requireSuccess(sdk_.setTeachType(socket_, kTeachTypeJog),
                           "set_teach_type(jog)")) {
        return false;
    }

    if ((initialServoState_ < 0 && !getServoState(initialServoState_))
        || !ensureServoRunning(exitRequested, config.restartIfRunning)) {
        return false;
    }

    if (!setJogCoordinate(config.coordinate)
        || !setJogSpeedPercent(config.speedPercent)) {
        return false;
    }

    joggingEnabled_ = true;
    return printState();
}

// 运行中恢复：先停点动，再循环清错/就绪/上电；恢复后要求操作者重新按键。
bool RobotJogController::recoverRunningServo(const ExitRequested& exitRequested)
{
    if (!connected_) {
        return false;
    }
    joggingEnabled_ = false;
    stopAllJogging();
    if (!ensureServoRunning(exitRequested, false)) {
        return false;
    }
    joggingEnabled_ = true;
    return true;
}
