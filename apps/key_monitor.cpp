#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace {

struct KeyState {
    bool up{false};
    bool down{false};
    bool left{false};
    bool right{false};
    bool escape{false};
};

bool isKeyDown(int virtualKey)
{
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

KeyState readKeys()
{
    return {
        isKeyDown(VK_UP),
        isKeyDown(VK_DOWN),
        isKeyDown(VK_LEFT),
        isKeyDown(VK_RIGHT),
        isKeyDown(VK_ESCAPE)
    };
}

void printTransition(const char* name, bool previous, bool current)
{
    if (previous != current) {
        std::cout << "[KEY] " << name << (current ? " PRESSED" : " RELEASED")
                  << std::endl;
    }
}

} // namespace

int main()
{
    SetConsoleTitleW(L"Inexbot Keyboard Monitor");
    std::cout << "Keyboard monitor started.\n"
              << "Keep focus on the ROBOT DEMO window, then press arrow keys.\n"
              << "This window only observes keys and never sends robot commands.\n"
              << "Press ESC while this window is focused to close it.\n\n";

    KeyState previous;
    while (true) {
        const KeyState current = readKeys();
        printTransition("UP", previous.up, current.up);
        printTransition("DOWN", previous.down, current.down);
        printTransition("LEFT", previous.left, current.left);
        printTransition("RIGHT", previous.right, current.right);

        if (GetForegroundWindow() == GetConsoleWindow() && current.escape) {
            break;
        }

        previous = current;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}
