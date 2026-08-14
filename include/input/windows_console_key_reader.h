#ifndef KEYBOARD_JOG_DEMO_WINDOWS_CONSOLE_KEY_READER_H
#define KEYBOARD_JOG_DEMO_WINDOWS_CONSOLE_KEY_READER_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "input/jog_input.h"

#include <string>

// Windows 控制台键盘读取器，提供与 Linux EvdevKeyReader 一致的核心接口。
class WindowsConsoleKeyReader
{
public:
    WindowsConsoleKeyReader();
    ~WindowsConsoleKeyReader();

    WindowsConsoleKeyReader(const WindowsConsoleKeyReader&) = delete;
    WindowsConsoleKeyReader& operator=(const WindowsConsoleKeyReader&) = delete;

    bool valid() const;
    const std::string& error() const;
    bool discardPendingEvents();
    bool poll(KeySnapshot& snapshot);

private:
    void resetKeys();

    HANDLE input_{INVALID_HANDLE_VALUE};
    DWORD originalMode_{0};
    bool valid_{false};
    bool shiftDown_{false};
    bool mainPlusKeyDown_{false};
    bool keypadPlusDown_{false};
    bool mainMinusKeyDown_{false};
    bool keypadMinusDown_{false};
    KeySnapshot keys_;
    std::string error_;
};

#endif
#endif
