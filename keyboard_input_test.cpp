#include "linux_evdev_key_reader.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_exitRequested = 0;

void signalHandler(int)
{
    g_exitRequested = 1;
}

std::vector<std::string> findKeyboardDevices()
{
    std::vector<std::string> devices;
    const std::filesystem::path directories[]{
        "/dev/input/by-id",
        "/dev/input/by-path",
    };

    // 只搜索稳定的 by-id/by-path 链接，避免 eventN 重启后编号变化。
    for (const auto& directory : directories) {
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(directory, error), end;
             !error && iterator != end;
             iterator.increment(error)) {
            const std::string name = iterator->path().filename().string();
            if (name.size() >= 9 && name.compare(name.size() - 9, 9, "event-kbd") == 0) {
                devices.push_back(iterator->path().string());
            }
        }
    }

    std::sort(devices.begin(), devices.end());
    devices.erase(std::unique(devices.begin(), devices.end()), devices.end());
    return devices;
}

void printDevices()
{
    const auto devices = findKeyboardDevices();
    if (devices.empty()) {
        std::cerr << "No stable keyboard event devices were found under "
                     "/dev/input/by-id or /dev/input/by-path.\n";
        return;
    }

    std::cout << "Keyboard event device candidates:\n";
    for (const auto& device : devices) {
        EvdevKeyReader reader(device);
        std::cout << "  " << device << " ["
                  << (reader.valid() ? "readable" : "not readable") << "]\n";
        if (!reader.valid()) {
            std::cout << "    " << reader.error() << '\n';
        }
    }
}

void printSnapshot(const KeySnapshot& keys)
{
    std::cout << "UP=" << keys.up
              << " DOWN=" << keys.down
              << " LEFT=" << keys.left
              << " RIGHT=" << keys.right
              << " ESC=" << keys.escape << '\n';
}

bool sameSnapshot(const KeySnapshot& left, const KeySnapshot& right)
{
    return left.up == right.up
        && left.down == right.down
        && left.left == right.left
        && left.right == right.right
        && left.escape == right.escape;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--list")) {
        printDevices();
        std::cout << "\nUsage: " << argv[0] << " <keyboard-event-device>\n";
        return argc == 1 ? 2 : 0;
    }
    if (argc != 2 || std::string(argv[1]) == "--help") {
        std::cout << "Usage: " << argv[0] << " <keyboard-event-device>\n"
                  << "       " << argv[0] << " --list\n";
        return argc == 2 ? 0 : 2;
    }

    // 这是独立诊断程序，不初始化 SDK，也不会连接或控制机器人。
    EvdevKeyReader reader(argv[1]);
    if (!reader.valid()) {
        std::cerr << reader.error() << '\n';
        return 1;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGHUP, signalHandler);

    if (!reader.discardPendingEvents()) {
        std::cerr << reader.error() << '\n';
        return 1;
    }

    std::cout << "Keyboard reader active: " << argv[1] << '\n'
              << "Press and release arrow keys. Press ESC or Ctrl+C to exit.\n"
              << "This diagnostic does not connect to or command the robot.\n";

    KeySnapshot previous;
    while (!g_exitRequested) {
        KeySnapshot current;
        std::vector<EvdevKeyEvent> observedKeyEvents;
        if (!reader.poll(current, &observedKeyEvents)) {
            std::cerr << reader.error() << '\n';
            return 1;
        }
        for (const auto& event : observedKeyEvents) {
            std::cout << "[RAW] code=" << event.code
                      << " value=" << event.value << '\n';
        }
        if (!sameSnapshot(current, previous)) {
            printSnapshot(current);
            if (current.escape) {
                return 0;
            }
            previous = current;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
