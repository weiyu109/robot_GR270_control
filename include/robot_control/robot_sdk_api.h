#ifndef KEYBOARD_JOG_DEMO_ROBOT_SDK_API_H
#define KEYBOARD_JOG_DEMO_ROBOT_SDK_API_H

#include <string>
#include <vector>

#include "cpp/interface/nrc_interface.h"
#include "cpp/interface/nrc_queue_operate.h"

using ControllerMessageCallback = void (*)(int messageId, const char* message);

// 厂商 SDK 的最小适配接口。
// RobotJogController 只依赖本接口，因此后续可以替换 SDK、增加模拟器或单元测试桩。
class RobotSdkApi
{
public:
    virtual ~RobotSdkApi() = default;

    virtual SOCKETFD connect(const std::string& ip, const std::string& port) = 0;
    virtual Result disconnect(SOCKETFD socket) = 0;
    virtual Result receiveMessages(SOCKETFD socket, ControllerMessageCallback callback) = 0;

    virtual Result getMode(SOCKETFD socket, int& mode) = 0;
    virtual Result setMode(SOCKETFD socket, int mode) = 0;
    virtual Result getTeachType(SOCKETFD socket, int& type) = 0;
    virtual Result setTeachType(SOCKETFD socket, int type) = 0;
    virtual Result getCoordinate(SOCKETFD socket, int& coordinate) = 0;
    virtual Result setCoordinate(SOCKETFD socket, int coordinate) = 0;
    virtual Result getSpeed(SOCKETFD socket, int& speed) = 0;
    virtual Result setSpeed(SOCKETFD socket, int speed) = 0;

    virtual Result getServoState(SOCKETFD socket, int& state) = 0;
    virtual Result setServoState(SOCKETFD socket, int state) = 0;
    virtual Result clearError(SOCKETFD socket) = 0;
    virtual Result powerOn(SOCKETFD socket) = 0;
    virtual Result powerOff(SOCKETFD socket) = 0;

    virtual Result startJog(SOCKETFD socket, int axis, bool positive) = 0;
    virtual Result stopJog(SOCKETFD socket, int axis) = 0;
    virtual Result getCurrentPosition(SOCKETFD socket,
                                      int coordinate,
                                      std::vector<double>& position) = 0;
    virtual Result moveJoint(SOCKETFD socket, const MoveCmd& command) = 0;
    virtual Result moveLinear(SOCKETFD socket, const MoveCmd& command) = 0;

    virtual Result getToolNumber(SOCKETFD socket, int& toolNumber) = 0;
    virtual Result getToolParam(SOCKETFD socket, int toolNumber, ToolParam& param) = 0;
    virtual Result setToolParam(SOCKETFD socket, int toolNumber, const ToolParam& param) = 0;

    virtual Result queueSetStatus(SOCKETFD socket, bool enabled) = 0;
    virtual Result queueClearData(SOCKETFD socket) = 0;
    virtual Result queuePushMoveLinear(SOCKETFD socket, const MoveCmd& command) = 0;
    virtual Result queueSend(SOCKETFD socket, int size, bool isContinue) = 0;
};

// 生产环境适配器：把抽象接口逐一转发到纳博特/Inexbot C++ SDK。
class InexbotSdkApi final : public RobotSdkApi
{
public:
    SOCKETFD connect(const std::string& ip, const std::string& port) override;
    Result disconnect(SOCKETFD socket) override;
    Result receiveMessages(SOCKETFD socket, ControllerMessageCallback callback) override;

    Result getMode(SOCKETFD socket, int& mode) override;
    Result setMode(SOCKETFD socket, int mode) override;
    Result getTeachType(SOCKETFD socket, int& type) override;
    Result setTeachType(SOCKETFD socket, int type) override;
    Result getCoordinate(SOCKETFD socket, int& coordinate) override;
    Result setCoordinate(SOCKETFD socket, int coordinate) override;
    Result getSpeed(SOCKETFD socket, int& speed) override;
    Result setSpeed(SOCKETFD socket, int speed) override;

    Result getServoState(SOCKETFD socket, int& state) override;
    Result setServoState(SOCKETFD socket, int state) override;
    Result clearError(SOCKETFD socket) override;
    Result powerOn(SOCKETFD socket) override;
    Result powerOff(SOCKETFD socket) override;

    Result startJog(SOCKETFD socket, int axis, bool positive) override;
    Result stopJog(SOCKETFD socket, int axis) override;
    Result getCurrentPosition(SOCKETFD socket,
                              int coordinate,
                              std::vector<double>& position) override;
    Result moveJoint(SOCKETFD socket, const MoveCmd& command) override;
    Result moveLinear(SOCKETFD socket, const MoveCmd& command) override;

    Result getToolNumber(SOCKETFD socket, int& toolNumber) override;
    Result getToolParam(SOCKETFD socket, int toolNumber, ToolParam& param) override;
    Result setToolParam(SOCKETFD socket, int toolNumber, const ToolParam& param) override;

    Result queueSetStatus(SOCKETFD socket, bool enabled) override;
    Result queueClearData(SOCKETFD socket) override;
    Result queuePushMoveLinear(SOCKETFD socket, const MoveCmd& command) override;
    Result queueSend(SOCKETFD socket, int size, bool isContinue) override;
};

const char* sdkResultText(Result result);

#endif
