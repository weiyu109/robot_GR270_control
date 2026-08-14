#include "robot_control/robot_jog_controller.h"

#include <algorithm>
#include <vector>

namespace {

constexpr int kPointCount = 50;
constexpr int kBatchSize = 25;
constexpr double kTotalForwardMillimeters = 100.0;
constexpr double kPointStepMillimeters = kTotalForwardMillimeters / kPointCount;
constexpr double kLiftMillimeters = 5.0;
constexpr double kVelocityMillimetersPerSecond = 100.0;

} // namespace

// 生成并发送50点阶梯MoveL队列：奇数点抬高5mm，偶数点回到起始Z。
bool RobotJogController::runStairQueueTest()
{
    if (!readyForPositionMotion()) {
        return false;
    }

    std::vector<double> start;
    if (!getCurrentPosition(1, start) || start.size() < 7) {
        return false;
    }
    int toolNumber = -1;
    ToolParam tcp{};
    if (!getCurrentTcp(toolNumber, tcp)) {
        return false;
    }

    // 打开队列模式会清空控制器远端队列；随后显式清空SDK本地队列。
    if (!requireSuccess(sdk_.queueSetStatus(socket_, true),
                        "queue_motion_set_status")
        || !requireSuccess(sdk_.queueClearData(socket_),
                           "queue_motion_clear_Data")) {
        return false;
    }

    for (int index = 0; index < kPointCount; ++index) {
        MoveCmd command;
        command.targetPosType = PosType::data;
        std::copy_n(start.begin(), 7, command.targetPosValue.begin());
        command.targetPosValue[0] = start[0] + (index + 1) * kPointStepMillimeters;
        command.targetPosValue[2] = start[2] + (index % 2 == 0 ? kLiftMillimeters : 0.0);
        command.coord = 1;
        command.velocity = kVelocityMillimetersPerSecond;
        command.acc = 50.0;
        command.dec = 50.0;
        command.pl = 0;
        command.toolNum = toolNumber;
        if (!requireSuccess(sdk_.queuePushMoveLinear(socket_, command),
                            "queue_motion_push_back_moveL")) {
            return false;
        }
    }

    // 第一批标记为继续拼接，第二批结束拼接；控制器收到第二批后开始执行。
    return requireSuccess(sdk_.queueSend(socket_, kBatchSize, true),
                          "queue_motion_send_to_controller(batch 1)")
        && requireSuccess(sdk_.queueSend(socket_, kBatchSize, false),
                          "queue_motion_send_to_controller(batch 2)");
}
