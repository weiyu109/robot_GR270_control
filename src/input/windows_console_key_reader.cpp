#include "input/windows_console_key_reader.h"

#ifdef _WIN32

WindowsConsoleKeyReader::WindowsConsoleKeyReader()
    : input_(GetStdHandle(STD_INPUT_HANDLE))
{
    if (input_ == INVALID_HANDLE_VALUE || !GetConsoleMode(input_, &originalMode_)) {
        error_ = "Failed to get Windows console input mode";
        return;
    }
    const DWORD mode = originalMode_ | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    if (!SetConsoleMode(input_, mode)) {
        error_ = "Failed to enable Windows console input events";
        return;
    }
    valid_ = true;
}

WindowsConsoleKeyReader::~WindowsConsoleKeyReader()
{
    if (valid_) {
        SetConsoleMode(input_, originalMode_);
    }
}

bool WindowsConsoleKeyReader::valid() const { return valid_; }
const std::string& WindowsConsoleKeyReader::error() const { return error_; }

void WindowsConsoleKeyReader::resetKeys()
{
    keys_ = {};
    shiftDown_ = false;
    mainPlusKeyDown_ = false;
    keypadPlusDown_ = false;
    mainMinusKeyDown_ = false;
    keypadMinusDown_ = false;
}

bool WindowsConsoleKeyReader::discardPendingEvents()
{
    if (!valid_ || !FlushConsoleInputBuffer(input_)) {
        error_ = "Failed to flush Windows console input events";
        resetKeys();
        return false;
    }
    resetKeys();
    return true;
}

bool WindowsConsoleKeyReader::poll(KeySnapshot& snapshot)
{
    if (!valid_) {
        snapshot = {};
        return false;
    }

    DWORD pending = 0;
    if (!GetNumberOfConsoleInputEvents(input_, &pending)) {
        error_ = "Failed to query Windows console input events";
        resetKeys();
        snapshot = {};
        return false;
    }

    while (pending > 0) {
        INPUT_RECORD records[32];
        DWORD read = 0;
        const DWORD count = pending < 32 ? pending : 32;
        if (!ReadConsoleInputW(input_, records, count, &read)) {
            error_ = "Failed to read Windows console input events";
            resetKeys();
            snapshot = {};
            return false;
        }

        for (DWORD i = 0; i < read; ++i) {
            const INPUT_RECORD& record = records[i];
            if (record.EventType == FOCUS_EVENT) {
                if (!record.Event.FocusEvent.bSetFocus) {
                    // 窗口失焦立即清空按键，防止后台误点动。
                    resetKeys();
                }
                continue;
            }
            if (record.EventType != KEY_EVENT) {
                continue;
            }

            const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
            const bool down = key.bKeyDown != FALSE;
            shiftDown_ = (key.dwControlKeyState & SHIFT_PRESSED) != 0;
            switch (key.wVirtualKeyCode) {
            case VK_UP: keys_.up = down; break;
            case VK_DOWN: keys_.down = down; break;
            case VK_LEFT: keys_.left = down; break;
            case VK_RIGHT: keys_.right = down; break;
            case VK_OEM_PLUS: mainPlusKeyDown_ = down; break;
            case VK_ADD: keypadPlusDown_ = down; break;
            case VK_OEM_MINUS: mainMinusKeyDown_ = down; break;
            case VK_SUBTRACT: keypadMinusDown_ = down; break;
            case VK_ESCAPE: keys_.escape = down; break;
            default: break;
            }
            keys_.plus = keypadPlusDown_ || (mainPlusKeyDown_ && shiftDown_);
            keys_.minus = keypadMinusDown_ || (mainMinusKeyDown_ && !shiftDown_);
        }

        if (!GetNumberOfConsoleInputEvents(input_, &pending)) {
            error_ = "Failed to query Windows console input events";
            resetKeys();
            snapshot = {};
            return false;
        }
    }

    snapshot = keys_;
    return true;
}

#endif
