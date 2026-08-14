#ifndef KEYBOARD_JOG_DEMO_JOG_INPUT_H
#define KEYBOARD_JOG_DEMO_JOG_INPUT_H

#include <array>

struct JogRequest {
    int axis{0};
    bool positive{false};

    bool operator==(const JogRequest& other) const
    {
        return axis == other.axis && positive == other.positive;
    }

    bool operator!=(const JogRequest& other) const
    {
        return !(*this == other);
    }
};

struct KeySnapshot {
    bool up{false};
    bool down{false};
    bool left{false};
    bool right{false};
    bool plus{false};
    bool minus{false};
    bool escape{false};
#ifdef __linux__
    // Linux evdev只提供物理键状态；保留组合键状态以正确识别Shift+=。
    bool leftShift{false};
    bool rightShift{false};
    bool equalKey{false};
    bool minusKey{false};
    bool keypadPlus{false};
    bool keypadMinus{false};
#endif
};

std::array<JogRequest, 3> makeJogRequests(const KeySnapshot& keys);

#ifdef __linux__
void applyEvdevKeyEvent(unsigned short type,
                        unsigned short code,
                        int value,
                        KeySnapshot& keys);
#endif

#endif
