#ifndef KEYBOARD_JOG_DEMO_ROBOT_JOG_CONTROLLER_H
#define KEYBOARD_JOG_DEMO_ROBOT_JOG_CONTROLLER_H

#include "input/jog_input.h"
#include "robot_control/robot_sdk_api.h"

#include <array>
#include <functional>
#include <string>

enum class RobotMode : int {
    Teach = 0,
    Remote = 1,
    Run = 2
};

enum class ServoState : int {
    Stopped = 0,
    Ready = 1,
    Alarm = 2,
    Running = 3
};

struct JogSessionConfig {
    int speedPercent{10};
    int coordinate{1};       // 0=关节，1=直角，2=工具，3=用户
    bool restartIfRunning{true};
};

// 关节目标运动的最小参数。
// targetDegrees[0..5] 对应六轴机械臂 J1..J6；第 7 位为厂商 SDK 预留本体轴位。
struct JointMoveCommand {
    std::array<double, 7> targetDegrees{};
    int axisCount{6};
    double velocityPercent{10.0};
    double accelerationPercent{50.0};
    double decelerationPercent{50.0};
};

// XYZ 直线运动的最小参数。
// 只替换当前直角坐标中的 X/Y/Z，姿态和第 7 位保持控制器当前值不变。
struct CartesianXyzCommand {
    double xMillimeters{0.0};
    double yMillimeters{0.0};
    double zMillimeters{0.0};
    double velocityMillimetersPerSecond{10.0};
    double accelerationPercent{50.0};
    double decelerationPercent{50.0};
    int coordinate{1};
    int toolNumber{0};
    int userNumber{0};
};

// 可复用的机械臂点动会话。
//
// 固定流程：
// connect -> 保存原模式 -> 切换示教/点动 -> 清错/就绪/上电
// -> 确认 state=3 -> 设置坐标系和速度 -> 开放点动。
//
// shutdown() 固定执行：停止所有点动 -> 本会话负责的下电
// -> 恢复进入前的停止态和操作模式 -> 断开 SDK。
class RobotJogController
{
public:
    using ExitRequested = std::function<bool()>;

    explicit RobotJogController(RobotSdkApi& sdk);
    ~RobotJogController();

    RobotJogController(const RobotJogController&) = delete;
    RobotJogController& operator=(const RobotJogController&) = delete;

    // 连接前会先清理本对象已有连接，但不会关闭其他进程持有的控制器连接。
    bool connect(const std::string& ip,
                 const std::string& port,
                 ControllerMessageCallback messageCallback = nullptr);

    // 安全断开：停止点动、按会话责任下电、恢复初始状态后断开 SDK。
    void disconnect();

    // 完整进入示教点动会话。只有返回 true 后才允许发送点动命令。
    bool enterTeachJog(const JogSessionConfig& config,
                       const ExitRequested& exitRequested);

    // 运行中检测到 state!=3 时调用。函数会先停止全部点动，再清错并恢复上电。
    // 返回后不会自动恢复旧按键请求，调用方必须要求操作者重新按键。
    bool recoverRunningServo(const ExitRequested& exitRequested);

    // 以下接口为后续上位机复用保留。调用方仍需遵守状态机顺序。
    bool setMode(RobotMode mode);
    bool clearErrorOnce();
    bool clearErrorUntilReady(const ExitRequested& exitRequested);
    bool setServoReady();
    bool powerOnOnce();
    bool powerOff();
    bool getServoState(int& state);
    bool printState();

    // 设置点动速度百分比。点动是持续速度式运动，松键必须调用 stopJog()。
    bool setJogSpeedPercent(int speedPercent);
    // 设置点动坐标系：0=关节，1=直角，2=工具，3=用户。
    bool setJogCoordinate(int coordinate);
    bool startJog(const JogRequest& request);
    bool stopJog(int axis);
    void stopAllJogging();

    // 发送一次关节目标位置命令（robot_movej），不是持续点动命令。
    bool moveJoints(const JointMoveCommand& command);

    // 发送一次 XYZ 直线目标命令（robot_movel），保留当前位置的姿态。
    bool moveCartesianXyz(const CartesianXyzCommand& command);

    // 幂等清理，可主动调用；析构函数也会执行一次。
    void shutdown();

    bool connected() const { return connected_; }
    bool joggingEnabled() const { return joggingEnabled_; }

private:
    bool requireSuccess(Result result, const char* operation) const;
    bool hasActiveJogging() const;
    bool readyForPositionMotion();
    bool waitForServoState(int expectedState);
    bool ensureServoRunning(const ExitRequested& exitRequested,
                            bool restartIfAlreadyRunning);

    RobotSdkApi& sdk_;
    SOCKETFD socket_{-1};
    bool connected_{false};
    bool joggingEnabled_{false};
    bool poweredOnBySession_{false};
    int previousMode_{-1};
    int initialServoState_{-1};
    // 厂商 MoveCmd 为本体预留 7 个轴位；六轴 GR270 实际使用前 6 位。
    std::array<bool, 7> activeAxes_{};
};

#endif
