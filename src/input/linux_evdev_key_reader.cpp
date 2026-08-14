#include "input/linux_evdev_key_reader.h"

#include <linux/input.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

EvdevKeyReader::EvdevKeyReader(const std::string& devicePath)
    : descriptor_(::open(devicePath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC))
{
    // 使用非阻塞只读方式，避免键盘断开或设备异常时阻塞控制循环。
    if (descriptor_ < 0) {
        error_ = "Failed to open input device '" + devicePath + "': "
            + std::strerror(errno);
    }
}

EvdevKeyReader::~EvdevKeyReader()
{
    if (descriptor_ >= 0) {
        ::close(descriptor_);
    }
}

bool EvdevKeyReader::valid() const
{
    return descriptor_ >= 0;
}

const std::string& EvdevKeyReader::error() const
{
    return error_;
}

bool EvdevKeyReader::discardPendingEvents()
{
    // ENABLE 前清掉残留按键，避免启动点动时误继承之前的按键状态。
    KeySnapshot ignored;
    if (!poll(ignored)) {
        return false;
    }
    keys_ = {};
    return true;
}

bool EvdevKeyReader::fail(const std::string& message, KeySnapshot& snapshot)
{
    error_ = message;
    keys_ = {};
    snapshot = {};
    return false;
}

bool EvdevKeyReader::poll(KeySnapshot& snapshot,
                          std::vector<EvdevKeyEvent>* observedKeyEvents)
{
    if (observedKeyEvents != nullptr) {
        observedKeyEvents->clear();
    }
    if (!valid()) {
        snapshot = {};
        return false;
    }

    while (true) {
        pollfd descriptorState{};
        descriptorState.fd = descriptor_;
        descriptorState.events = POLLIN;

        const int result = ::poll(&descriptorState, 1, 0);
        if (result < 0) {
            if (errno == EINTR) {
                snapshot = keys_;
                return true;
            }
            return fail(std::string("Input device poll failed: ") + std::strerror(errno),
                        snapshot);
        }
        if (result == 0) {
            snapshot = keys_;
            return true;
        }
        // 设备断开、句柄失效或内核报告错误时必须失败闭锁，由上层执行停止清理。
        if ((descriptorState.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return fail("Input device disconnected or became unavailable", snapshot);
        }
        if ((descriptorState.revents & POLLIN) == 0) {
            snapshot = keys_;
            return true;
        }

        input_event events[32]{};
        const ssize_t bytesRead = ::read(descriptor_, events, sizeof(events));
        if (bytesRead == 0) {
            return fail("Input device reached end of stream", snapshot);
        }
        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                snapshot = keys_;
                return true;
            }
            return fail(std::string("Input device read failed: ") + std::strerror(errno),
                        snapshot);
        }
        if (bytesRead % static_cast<ssize_t>(sizeof(input_event)) != 0) {
            return fail("Input device returned a partial event record", snapshot);
        }

        const std::size_t eventCount =
            static_cast<std::size_t>(bytesRead) / sizeof(input_event);
        for (std::size_t i = 0; i < eventCount; ++i) {
            // SYN_DROPPED 表示中间按键事件丢失，当前按键状态不再可信。
            if (events[i].type == EV_SYN && events[i].code == SYN_DROPPED) {
                return fail("Input device dropped events; key state is no longer reliable",
                            snapshot);
            }
            if (events[i].type == EV_KEY && observedKeyEvents != nullptr) {
                observedKeyEvents->push_back({events[i].code, events[i].value});
            }
            applyEvdevKeyEvent(events[i].type, events[i].code, events[i].value, keys_);
        }
    }
}
