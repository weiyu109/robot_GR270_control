#ifndef KEYBOARD_JOG_DEMO_LINUX_EVDEV_KEY_READER_H
#define KEYBOARD_JOG_DEMO_LINUX_EVDEV_KEY_READER_H

#include "input/key_reader.h"

#include <string>
#include <vector>

struct EvdevKeyEvent {
    unsigned short code{0};
    int value{0};
};

class EvdevKeyReader final : public KeyReader
{
public:
    explicit EvdevKeyReader(const std::string& devicePath);
    ~EvdevKeyReader();

    EvdevKeyReader(const EvdevKeyReader&) = delete;
    EvdevKeyReader& operator=(const EvdevKeyReader&) = delete;

    bool valid() const override;
    const std::string& error() const override;
    bool discardPendingEvents() override;
    bool poll(KeySnapshot& snapshot) override;
    bool poll(KeySnapshot& snapshot,
              std::vector<EvdevKeyEvent>* observedKeyEvents);

private:
    bool fail(const std::string& message, KeySnapshot& snapshot);

    int descriptor_{-1};
    std::string error_;
    KeySnapshot keys_;
};

#endif
