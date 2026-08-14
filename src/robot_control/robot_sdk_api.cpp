#include "robot_control/robot_sdk_api.h"

SOCKETFD InexbotSdkApi::connect(const std::string& ip, const std::string& port)
{
    return connect_robot(ip, port);
}

Result InexbotSdkApi::disconnect(SOCKETFD socket) { return disconnect_robot(socket); }
Result InexbotSdkApi::receiveMessages(SOCKETFD socket, ControllerMessageCallback callback)
{
    return recv_message(socket, callback);
}
Result InexbotSdkApi::getMode(SOCKETFD socket, int& mode) { return get_current_mode(socket, mode); }
Result InexbotSdkApi::setMode(SOCKETFD socket, int mode) { return set_current_mode(socket, mode); }
Result InexbotSdkApi::getTeachType(SOCKETFD socket, int& type) { return get_teach_type(socket, type); }
Result InexbotSdkApi::setTeachType(SOCKETFD socket, int type) { return set_teach_type(socket, type); }
Result InexbotSdkApi::getCoordinate(SOCKETFD socket, int& coordinate)
{
    return get_current_coord(socket, coordinate);
}
Result InexbotSdkApi::setCoordinate(SOCKETFD socket, int coordinate)
{
    return set_current_coord(socket, coordinate);
}
Result InexbotSdkApi::getSpeed(SOCKETFD socket, int& speed) { return get_speed(socket, speed); }
Result InexbotSdkApi::setSpeed(SOCKETFD socket, int speed) { return set_speed(socket, speed); }
Result InexbotSdkApi::getServoState(SOCKETFD socket, int& state)
{
    return get_servo_state(socket, state);
}
Result InexbotSdkApi::setServoState(SOCKETFD socket, int state)
{
    return set_servo_state(socket, state);
}
Result InexbotSdkApi::clearError(SOCKETFD socket) { return clear_error(socket); }
Result InexbotSdkApi::powerOn(SOCKETFD socket) { return set_servo_poweron(socket); }
Result InexbotSdkApi::powerOff(SOCKETFD socket) { return set_servo_poweroff(socket); }
Result InexbotSdkApi::startJog(SOCKETFD socket, int axis, bool positive)
{
    return robot_start_jogging(socket, axis, positive);
}
Result InexbotSdkApi::stopJog(SOCKETFD socket, int axis)
{
    return robot_stop_jogging(socket, axis);
}
Result InexbotSdkApi::getCurrentPosition(SOCKETFD socket,
                                         int coordinate,
                                         std::vector<double>& position)
{
    return get_current_position(socket, coordinate, position);
}
Result InexbotSdkApi::moveJoint(SOCKETFD socket, const MoveCmd& command)
{
    return robot_movej(socket, command);
}
Result InexbotSdkApi::moveLinear(SOCKETFD socket, const MoveCmd& command)
{
    return robot_movel(socket, command);
}

const char* sdkResultText(Result result)
{
    switch (result) {
    case SUCCESS: return "SUCCESS";
    case RECEIVE_FAILED: return "RECEIVE_FAILED";
    case DISCONNECT: return "DISCONNECT";
    case PARAM_ERR: return "PARAM_ERR";
    case OPERATION_NOT_ALLOWED: return "OPERATION_NOT_ALLOWED";
    case EXCEPTION: return "EXCEPTION";
    case TIMEOUT: return "TIMEOUT";
    default: return "UNKNOWN";
    }
}
