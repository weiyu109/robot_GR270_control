#ifndef KEYBOARD_JOG_DEMO_KEY_READER_H
#define KEYBOARD_JOG_DEMO_KEY_READER_H

#include "input/jog_input.h"

#include <string>

// 跨平台键盘读取最小接口，终端遥操作不依赖 Linux/Windows 具体实现。
class KeyReader
{
public:
    virtual ~KeyReader() = default;

    virtual bool valid() const = 0;
    virtual const std::string& error() const = 0;
    virtual bool discardPendingEvents() = 0;
    virtual bool poll(KeySnapshot& snapshot) = 0;
};

#endif
