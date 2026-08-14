#include "robot_control/robot_jog_controller.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakeRobotSdk final : public RobotSdkApi
{
public:
    SOCKETFD connect(const std::string&, const std::string&) override
    {
        calls.push_back("connect");
        connected = true;
        return 42;
    }
    Result disconnect(SOCKETFD) override
    {
        calls.push_back("disconnect");
        connected = false;
        return SUCCESS;
    }
    Result receiveMessages(SOCKETFD, ControllerMessageCallback) override
    {
        calls.push_back("receiveMessages");
        return SUCCESS;
    }
    Result getMode(SOCKETFD, int& value) override { value = mode; return SUCCESS; }
    Result setMode(SOCKETFD, int value) override
    {
        calls.push_back("setMode:" + std::to_string(value));
        mode = value;
        return SUCCESS;
    }
    Result getTeachType(SOCKETFD, int& value) override { value = teachType; return SUCCESS; }
    Result setTeachType(SOCKETFD, int value) override
    {
        calls.push_back("setTeachType:" + std::to_string(value));
        teachType = value;
        return SUCCESS;
    }
    Result getCoordinate(SOCKETFD, int& value) override { value = coordinate; return SUCCESS; }
    Result setCoordinate(SOCKETFD, int value) override
    {
        calls.push_back("setCoordinate:" + std::to_string(value));
        coordinate = value;
        return SUCCESS;
    }
    Result getSpeed(SOCKETFD, int& value) override { value = speed; return SUCCESS; }
    Result setSpeed(SOCKETFD, int value) override
    {
        calls.push_back("setSpeed:" + std::to_string(value));
        speed = value;
        return SUCCESS;
    }
    Result getServoState(SOCKETFD, int& value) override
    {
        value = servoState;
        return SUCCESS;
    }
    Result setServoState(SOCKETFD, int value) override
    {
        calls.push_back("setServoState:" + std::to_string(value));
        servoState = value;
        return SUCCESS;
    }
    Result clearError(SOCKETFD) override
    {
        calls.push_back("clearError");
        servoState = static_cast<int>(ServoState::Ready);
        return SUCCESS;
    }
    Result powerOn(SOCKETFD) override
    {
        calls.push_back("powerOn");
        servoState = static_cast<int>(ServoState::Running);
        return SUCCESS;
    }
    Result powerOff(SOCKETFD) override
    {
        calls.push_back("powerOff");
        servoState = static_cast<int>(ServoState::Ready);
        return SUCCESS;
    }
    Result startJog(SOCKETFD, int axis, bool positive) override
    {
        calls.push_back("startJog:" + std::to_string(axis)
                        + (positive ? ":+" : ":-"));
        return SUCCESS;
    }
    Result stopJog(SOCKETFD, int axis) override
    {
        calls.push_back("stopJog:" + std::to_string(axis));
        return SUCCESS;
    }
    Result getCurrentPosition(SOCKETFD,
                              int value,
                              std::vector<double>& position) override
    {
        calls.push_back("getCurrentPosition:" + std::to_string(value));
        position = currentPosition;
        return SUCCESS;
    }
    Result moveJoint(SOCKETFD, const MoveCmd& command) override
    {
        calls.push_back("moveJoint");
        lastJointCommand = command;
        return SUCCESS;
    }
    Result moveLinear(SOCKETFD, const MoveCmd& command) override
    {
        calls.push_back("moveLinear");
        lastLinearCommand = command;
        return SUCCESS;
    }

    int count(const std::string& call) const
    {
        return static_cast<int>(std::count(calls.begin(), calls.end(), call));
    }

    bool connected{false};
    int mode{static_cast<int>(RobotMode::Remote)};
    int teachType{1};
    int coordinate{0};
    int speed{5};
    int servoState{static_cast<int>(ServoState::Stopped)};
    std::vector<double> currentPosition{100.0, 200.0, 300.0, 10.0, 20.0, 30.0, 7.0};
    MoveCmd lastJointCommand;
    MoveCmd lastLinearCommand;
    std::vector<std::string> calls;
};

void testFixedEnableAndShutdownSequence()
{
    FakeRobotSdk sdk;
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");

    JogSessionConfig config;
    config.speedPercent = 10;
    expect(robot.enterTeachJog(config, [] { return false; }),
           "teach jog session should enter running state");
    expect(sdk.mode == static_cast<int>(RobotMode::Teach),
           "session should switch to teach mode");
    expect(sdk.teachType == 0, "session should select jog teach type");
    expect(sdk.servoState == static_cast<int>(ServoState::Running),
           "session should power servo to running state");
    expect(sdk.count("powerOn") == 1, "enable should power on exactly once");

    expect(robot.startJog({1, true}), "jog start should succeed when enabled");
    expect(!robot.setMode(RobotMode::Remote),
           "mode switch must be rejected while jogging is active");
    robot.shutdown();
    expect(sdk.count("stopJog:1") == 1, "shutdown must stop active axis first");
    expect(sdk.count("powerOff") == 1, "shutdown must power off session-owned power");
    expect(sdk.mode == static_cast<int>(RobotMode::Remote),
           "shutdown must restore the previous mode");
    expect(sdk.servoState == static_cast<int>(ServoState::Stopped),
           "shutdown must restore initial stopped state");
    expect(!sdk.connected, "shutdown must disconnect SDK session");
}

void testPowerOnRequiresReadyState()
{
    FakeRobotSdk sdk;
    sdk.servoState = static_cast<int>(ServoState::Stopped);
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");
    expect(!robot.powerOnOnce(), "raw power-on must reject non-ready servo state");
    expect(sdk.count("powerOn") == 0,
           "rejected power-on must not call the controller SDK");
    robot.shutdown();
}

void testAlreadyRunningIsRestartedOnlyOnce()
{
    FakeRobotSdk sdk;
    sdk.mode = static_cast<int>(RobotMode::Teach);
    sdk.servoState = static_cast<int>(ServoState::Running);
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");

    JogSessionConfig config;
    config.restartIfRunning = true;
    expect(robot.enterTeachJog(config, [] { return false; }),
           "already-running servo should restart successfully");
    expect(sdk.count("powerOff") == 1,
           "already-running servo must be powered off exactly once before restart");
    expect(sdk.count("powerOn") == 1,
           "already-running servo must be powered on exactly once after restart");
    robot.shutdown();
}

void testRuntimeRecoveryStopsJogBeforePowerOn()
{
    FakeRobotSdk sdk;
    sdk.mode = static_cast<int>(RobotMode::Teach);
    sdk.servoState = static_cast<int>(ServoState::Running);
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");

    JogSessionConfig config;
    config.restartIfRunning = false;
    expect(robot.enterTeachJog(config, [] { return false; }), "session should enter");
    expect(robot.startJog({2, false}), "Y jog should start");

    sdk.servoState = static_cast<int>(ServoState::Ready);
    expect(robot.recoverRunningServo([] { return false; }), "runtime recovery should succeed");
    const auto stop = std::find(sdk.calls.begin(), sdk.calls.end(), "stopJog:2");
    const auto power = std::find(stop, sdk.calls.end(), "powerOn");
    expect(stop != sdk.calls.end(), "runtime recovery must stop active jogging");
    expect(power != sdk.calls.end() && stop < power,
           "runtime recovery must stop jogging before powering on");
    robot.shutdown();
}

void testLoopClearErrorReachesReadyState()
{
    FakeRobotSdk sdk;
    sdk.servoState = static_cast<int>(ServoState::Alarm);
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");
    expect(robot.clearErrorUntilReady([] { return false; }),
           "loop clear-error should stop when servo reaches Ready");
    expect(sdk.count("clearError") == 1, "alarm should be cleared once in fake SDK");
    expect(sdk.servoState == static_cast<int>(ServoState::Ready),
           "clear-error loop should leave fake servo Ready");
    robot.shutdown();
}

void testJointMoveBuildsMinimalMoveCommand()
{
    FakeRobotSdk sdk;
    sdk.servoState = static_cast<int>(ServoState::Running);
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");

    JointMoveCommand command;
    command.targetDegrees = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 0.0};
    command.axisCount = 6;
    command.velocityPercent = 12.0;
    expect(robot.moveJoints(command), "joint position command should succeed");
    expect(sdk.count("moveJoint") == 1, "joint command should call robot_movej once");
    expect(sdk.lastJointCommand.coord == 0, "joint command should use joint coordinate");
    expect(sdk.lastJointCommand.targetPosValue[5] == 6.0,
           "joint command should copy J6 target");
    expect(sdk.lastJointCommand.targetPosValue[6] == 0.0,
           "unused seventh robot axis should remain zero");
    expect(sdk.lastJointCommand.velocity == 12.0,
           "joint velocity should remain a percentage");
    robot.shutdown();
}

void testCartesianXyzMovePreservesCurrentOrientation()
{
    FakeRobotSdk sdk;
    sdk.servoState = static_cast<int>(ServoState::Running);
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");

    CartesianXyzCommand command;
    command.xMillimeters = 110.0;
    command.yMillimeters = 220.0;
    command.zMillimeters = 330.0;
    command.velocityMillimetersPerSecond = 25.0;
    expect(robot.moveCartesianXyz(command), "XYZ linear command should succeed");
    expect(sdk.count("getCurrentPosition:1") == 1,
           "XYZ command should read current Cartesian pose first");
    expect(sdk.count("moveLinear") == 1, "XYZ command should call robot_movel once");
    expect(sdk.lastLinearCommand.targetPosValue[0] == 110.0
               && sdk.lastLinearCommand.targetPosValue[1] == 220.0
               && sdk.lastLinearCommand.targetPosValue[2] == 330.0,
           "XYZ command should replace only translation values");
    expect(sdk.lastLinearCommand.targetPosValue[3] == 10.0
               && sdk.lastLinearCommand.targetPosValue[6] == 7.0,
           "XYZ command should preserve orientation and seventh current value");
    expect(sdk.lastLinearCommand.velocity == 25.0,
           "linear velocity should use millimeters per second");
    robot.shutdown();
}

void testJointCoordinateJogSupportsSixRobotAxes()
{
    FakeRobotSdk sdk;
    sdk.mode = static_cast<int>(RobotMode::Teach);
    sdk.servoState = static_cast<int>(ServoState::Running);
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");

    JogSessionConfig config;
    config.coordinate = 0;
    config.restartIfRunning = false;
    expect(robot.enterTeachJog(config, [] { return false; }), "joint jog session should enter");
    expect(robot.startJog({6, true}), "joint-coordinate J6 jog should start");
    expect(robot.stopJog(6), "joint-coordinate J6 jog should stop");
    robot.shutdown();
}

} // namespace

int main()
{
    testFixedEnableAndShutdownSequence();
    testAlreadyRunningIsRestartedOnlyOnce();
    testRuntimeRecoveryStopsJogBeforePowerOn();
    testPowerOnRequiresReadyState();
    testLoopClearErrorReachesReadyState();
    testJointMoveBuildsMinimalMoveCommand();
    testCartesianXyzMovePreservesCurrentOrientation();
    testJointCoordinateJogSupportsSixRobotAxes();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All robot jog controller tests passed\n";
    return EXIT_SUCCESS;
}
