#include "jog_input.h"

#ifdef __linux__
#include <linux/input.h>
#endif

namespace {

// SDK 点动接口使用 1、2 表示当前笛卡尔坐标下的 X、Y 轴。
constexpr int kXAxis = 1;
constexpr int kYAxis = 2;

} // namespace

std::array<JogRequest, 2> makeJogRequests(const KeySnapshot& keys)
{
    std::array<JogRequest, 2> requests{};

    // 同一轴的正反方向同时按下时返回空请求，主循环会停止该轴。
    if (keys.up != keys.down) {
        requests[0] = {kXAxis, keys.up};
    }
    if (keys.left != keys.right) {
        requests[1] = {kYAxis, keys.left};
    }
    return requests;
}

#ifdef __linux__
void applyEvdevKeyEvent(unsigned short type,
                        unsigned short code,
                        int value,
                        KeySnapshot& keys)
{
    // evdev value: 1=按下，0=松开，2=自动重复。重复事件不改变状态。
    if (type != EV_KEY || value == 2 || (value != 0 && value != 1)) {
        return;
    }

    const bool pressed = value == 1;
    switch (code) {
    case KEY_UP: keys.up = pressed; break;
    case KEY_DOWN: keys.down = pressed; break;
    case KEY_LEFT: keys.left = pressed; break;
    case KEY_RIGHT: keys.right = pressed; break;
    case KEY_ESC: keys.escape = pressed; break;
    default: break;
    }
}
#endif
