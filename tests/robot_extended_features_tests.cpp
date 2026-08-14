#include "robot_control/robot_jog_controller.h"
#include "terminal/robot_debug_menu.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
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

class ExtendedFeatureFakeSdk final : public RobotSdkApi
{
public:
    SOCKETFD connect(const std::string&, const std::string&) override { return 42; }
    Result disconnect(SOCKETFD) override { return SUCCESS; }
    Result receiveMessages(SOCKETFD, ControllerMessageCallback) override { return SUCCESS; }
    Result getMode(SOCKETFD, int& value) override { value = 0; return SUCCESS; }
    Result setMode(SOCKETFD, int) override { return SUCCESS; }
    Result getTeachType(SOCKETFD, int& value) override { value = 0; return SUCCESS; }
    Result setTeachType(SOCKETFD, int) override { return SUCCESS; }
    Result getCoordinate(SOCKETFD, int& value) override { value = 1; return SUCCESS; }
    Result setCoordinate(SOCKETFD, int) override { return SUCCESS; }
    Result getSpeed(SOCKETFD, int& value) override { value = 10; return SUCCESS; }
    Result setSpeed(SOCKETFD, int) override { return SUCCESS; }
    Result getServoState(SOCKETFD, int& value) override
    {
        value = static_cast<int>(ServoState::Running);
        return SUCCESS;
    }
    Result setServoState(SOCKETFD, int) override { return SUCCESS; }
    Result clearError(SOCKETFD) override { return SUCCESS; }
    Result powerOn(SOCKETFD) override { return SUCCESS; }
    Result powerOff(SOCKETFD) override { return SUCCESS; }
    Result startJog(SOCKETFD, int, bool) override { return SUCCESS; }
    Result stopJog(SOCKETFD, int) override { return SUCCESS; }
    Result getCurrentPosition(SOCKETFD,
                              int coordinate,
                              std::vector<double>& position) override
    {
        position = coordinate == 0 ? joints : cartesian;
        return SUCCESS;
    }
    Result moveJoint(SOCKETFD, const MoveCmd&) override { return SUCCESS; }
    Result moveLinear(SOCKETFD, const MoveCmd&) override { return SUCCESS; }
    Result getToolNumber(SOCKETFD, int& value) override
    {
        value = toolNumber;
        return SUCCESS;
    }
    Result getToolParam(SOCKETFD, int, ToolParam& value) override
    {
        value = toolParam;
        return SUCCESS;
    }
    Result setToolParam(SOCKETFD, int number, const ToolParam& value) override
    {
        lastSetToolNumber = number;
        ++toolSetCount;
        toolParam = value;
        return SUCCESS;
    }
    Result queueSetStatus(SOCKETFD, bool enabled) override
    {
        queueEnabled = enabled;
        return SUCCESS;
    }
    Result queueClearData(SOCKETFD) override
    {
        queueCleared = true;
        return SUCCESS;
    }
    Result queuePushMoveLinear(SOCKETFD, const MoveCmd& command) override
    {
        queuedCommands.push_back(command);
        return SUCCESS;
    }
    Result queueSend(SOCKETFD, int size, bool isContinue) override
    {
        sends.emplace_back(size, isContinue);
        return SUCCESS;
    }

    int toolNumber{3};
    int lastSetToolNumber{-1};
    int toolSetCount{0};
    bool queueEnabled{false};
    bool queueCleared{false};
    std::vector<double> joints{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 0.0};
    std::vector<double> cartesian{100.0, 200.0, 300.0, 10.0, 20.0, 30.0, 7.0};
    ToolParam toolParam{1.0, 2.0, -100.0, 4.0, 5.0, 6.0,
                        12.0, 13.0, 14.0, 15.0, 16.0};
    std::vector<MoveCmd> queuedCommands;
    std::vector<std::pair<int, bool>> sends;
};

void testPositionAndTcpFunctions()
{
    ExtendedFeatureFakeSdk sdk;
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");

    std::vector<double> pose;
    expect(robot.getCurrentPosition(1, pose) && pose == sdk.cartesian,
           "Cartesian pose query should return coord=1 data");
    expect(robot.getCurrentPosition(0, pose) && pose == sdk.joints,
           "joint query should return coord=0 data");

    expect(robot.setCurrentTcpZ(-342.0), "TCP Z update should succeed");
    expect(sdk.lastSetToolNumber == 3, "TCP update should use active tool number");
    expect(sdk.toolParam.X == 1.0 && sdk.toolParam.Z == -342.0,
           "TCP Z update should preserve other pose components");
    expect(sdk.toolParam.payloadMass == 12.0
               && sdk.toolParam.payloadMassCenter_Z == 16.0,
           "TCP Z update should preserve payload data");

    expect(robot.resetCurrentTcpPose(), "TCP pose reset should succeed");
    expect(sdk.toolParam.X == 0.0 && sdk.toolParam.Y == 0.0
               && sdk.toolParam.Z == 0.0 && sdk.toolParam.A == 0.0
               && sdk.toolParam.B == 0.0 && sdk.toolParam.C == 0.0,
           "TCP reset should zero XYZABC");
    expect(sdk.toolParam.payloadMass == 12.0
               && sdk.toolParam.payloadMassCenter_Z == 16.0,
           "TCP reset should preserve payload data");
}

void testFiftyPointStairQueue()
{
    ExtendedFeatureFakeSdk sdk;
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");
    expect(robot.runStairQueueTest(), "stair queue should be accepted by fake SDK");

    expect(sdk.queueEnabled, "queue mode should be enabled");
    expect(sdk.queueCleared, "local queue should be cleared before filling");
    expect(sdk.queuedCommands.size() == 50, "exactly 50 MoveL points should be queued");
    if (sdk.queuedCommands.size() == 50) {
        const MoveCmd& first = sdk.queuedCommands.front();
        const MoveCmd& second = sdk.queuedCommands[1];
        const MoveCmd& last = sdk.queuedCommands.back();
        expect(first.targetPosValue[0] == 102.0 && first.targetPosValue[2] == 305.0,
               "first point should advance 2mm and lift 5mm");
        expect(second.targetPosValue[0] == 104.0 && second.targetPosValue[2] == 300.0,
               "second point should advance another 2mm and return Z");
        expect(last.targetPosValue[0] == 200.0 && last.targetPosValue[2] == 300.0,
               "last point should finish 100mm forward at base Z");
        expect(first.velocity == 100.0 && last.velocity == 100.0,
               "all queue points should use 100mm/s");
        expect(first.toolNum == 3 && first.coord == 1,
               "queue points should use active TCP and Cartesian coordinate");
    }

    expect(sdk.sends.size() == 2, "queue should be sent in two batches");
    if (sdk.sends.size() == 2) {
        expect(sdk.sends[0] == std::make_pair(25, true),
               "first 25 points should keep queue concatenation open");
        expect(sdk.sends[1] == std::make_pair(25, false),
               "second 25 points should close concatenation and start motion");
    }
}

void testMenuRoutesNewOptions()
{
    ExtendedFeatureFakeSdk sdk;
    RobotJogController robot(sdk);
    expect(robot.connect("127.0.0.1", "6001"), "connect should succeed");
    std::istringstream input("7\n8\n1\n8\n2\n9\n0\n");
    std::ostringstream output;
    std::ostringstream error;
    RobotDebugMenu menu(
        robot,
        [] { return false; },
        [] { return TeleopResult::ReturnToMenu; },
        input,
        output,
        error);

    expect(menu.run() == 0, "menu should execute new options and exit");
    expect(output.str().find("当前TCP编号=3") != std::string::npos,
           "menu option 7 should print current TCP");
    expect(sdk.toolSetCount == 2,
           "menu option 8 should route both TCP set and reset actions");
    expect(sdk.queuedCommands.size() == 50,
           "menu option 9 should route the 50-point queue test");
    expect(error.str().empty(), "new menu actions should not print errors in fake test");
}

} // namespace

int main()
{
    testPositionAndTcpFunctions();
    testFiftyPointStairQueue();
    testMenuRoutesNewOptions();
    if (failures != 0) {
        std::cerr << failures << " extended feature assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All extended robot feature tests passed\n";
    return EXIT_SUCCESS;
}
