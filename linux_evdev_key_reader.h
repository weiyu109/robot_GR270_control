#ifndef KEYBOARD_JOG_DEMO_LINUX_EVDEV_KEY_READER_H
#define KEYBOARD_JOG_DEMO_LINUX_EVDEV_KEY_READER_H

#include "jog_input.h"

#include <string>
#include <vector>

struct EvdevKeyEvent {
    unsigned short code{0};
    int value{0};
};

class EvdevKeyReader
{
public:
    explicit EvdevKeyReader(const std::string& devicePath);
    ~EvdevKeyReader();

    EvdevKeyReader(const EvdevKeyReader&) = delete;
    EvdevKeyReader& operator=(const EvdevKeyReader&) = delete;

    bool valid() const;
    const std::string& error() const;
    bool discardPendingEvents();
    bool poll(KeySnapshot& snapshot,
              std::vector<EvdevKeyEvent>* observedKeyEvents = nullptr);

private:
    bool fail(const std::string& message, KeySnapshot& snapshot);

    int descriptor_{-1};
    std::string error_;
    KeySnapshot keys_;
};

#endif
