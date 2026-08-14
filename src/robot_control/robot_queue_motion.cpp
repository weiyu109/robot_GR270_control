#include "robot_control/robot_jog_controller.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr int kPointCount = 50;
constexpr int kBatchSize = 25;
constexpr double kTotalForwardMillimeters = 100.0;
constexpr double kPointStepMillimeters = kTotalForwardMillimeters / kPointCount;
constexpr double kLiftMillimeters = 5.0;
constexpr double kVelocityMillimetersPerSecond = 100.0;
constexpr auto kQueuePollPeriod = std::chrono::milliseconds(100);

} // namespace

// 生成并发送50点阶梯MoveL队列；执行完毕后关闭队列模式并自动下电。
bool RobotJogController::runStairQueueTest(const ExitRequested& exitRequested)
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
                        "queue_motion_set_status")) {
        return false;
    }
    queueModeEnabled_ = true;

    // 队列模式开启后，任一失败路径都尝试关闭队列模式并下电。
    const auto closeQueueAndPowerOff = [this] {
        const bool queueClosed = closeQueueMode();
        const bool poweredOff = powerOff();
        return queueClosed && poweredOff;
    };

    if (!requireSuccess(sdk_.queueClearData(socket_),
                        "queue_motion_clear_Data")) {
        closeQueueAndPowerOff();
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
            closeQueueAndPowerOff();
            return false;
        }
    }

    // 第一批标记为继续拼接，第二批结束拼接；控制器收到第二批后开始执行。
    const bool firstBatchSent = requireSuccess(
        sdk_.queueSend(socket_, kBatchSize, true),
        "queue_motion_send_to_controller(batch 1)");
    const bool secondBatchSent = firstBatchSent && requireSuccess(
        sdk_.queueSend(socket_, kBatchSize, false),
        "queue_motion_send_to_controller(batch 2)");
    if (!secondBatchSent) {
        closeQueueAndPowerOff();
        return false;
    }

    // 不设置超时；持续等待控制器剩余队列归零，Ctrl+C可进入关闭队列和下电流程。
    bool completed = false;
    int lastRemaining = -1;
    while (!exitRequested()) {
        int remaining = -1;
        if (!requireSuccess(sdk_.queueGetRemainingLength(socket_, remaining),
                            "queue_motion_get_queuelen")) {
            closeQueueAndPowerOff();
            return false;
        }
        if (remaining != lastRemaining) {
            std::cout << "Queue remaining instructions: " << remaining << '\n';
            lastRemaining = remaining;
        }
        if (remaining == 0) {
            completed = true;
            break;
        }
        std::this_thread::sleep_for(kQueuePollPeriod);
    }

    const bool cleanupSucceeded = closeQueueAndPowerOff();
    return completed && cleanupSucceeded;
}

// 关闭队列模式。SDK说明该操作会清空控制器远端已存储队列。
bool RobotJogController::closeQueueMode()
{
    if (queueModeEnabled_) {
        if (!connected_
            || !requireSuccess(sdk_.queueSetStatus(socket_, false),
                               "queue_motion_set_status(false)")) {
            return false;
        }
        queueModeEnabled_ = false;
    }
    return true;
}
