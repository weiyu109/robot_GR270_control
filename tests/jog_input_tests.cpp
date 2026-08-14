#include "jog_input.h"
#include "linux_evdev_key_reader.h"

#include <linux/input.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testArrowPressReleaseAndRepeat()
{
    KeySnapshot keys;

    applyEvdevKeyEvent(EV_KEY, KEY_UP, 1, keys);
    expect(keys.up, "KEY_UP press should set up");

    applyEvdevKeyEvent(EV_KEY, KEY_UP, 2, keys);
    expect(keys.up, "KEY_UP repeat should preserve held state");

    applyEvdevKeyEvent(EV_KEY, KEY_UP, 0, keys);
    expect(!keys.up, "KEY_UP release should clear up");
}

void testIgnoredEventsDoNotChangeState()
{
    KeySnapshot keys;
    keys.left = true;

    applyEvdevKeyEvent(EV_REL, REL_X, 1, keys);
    applyEvdevKeyEvent(EV_KEY, KEY_A, 1, keys);

    expect(keys.left, "unrelated events should preserve existing state");
    expect(!keys.up && !keys.down && !keys.right && !keys.plus && !keys.minus
               && !keys.escape,
           "unrelated events should not set tracked keys");
}

void testZKeyPressReleaseAndMapping()
{
    KeySnapshot keys;

    applyEvdevKeyEvent(EV_KEY, KEY_EQUAL, 1, keys);
    expect(!keys.plus, "equal without Shift must not start Z positive");
    applyEvdevKeyEvent(EV_KEY, KEY_LEFTSHIFT, 1, keys);
    expect(keys.plus, "Shift+= should set Z positive");
    expect(makeJogRequests(keys)[2] == JogRequest{3, true},
           "plus should map to Z positive");
    applyEvdevKeyEvent(EV_KEY, KEY_LEFTSHIFT, 0, keys);
    expect(!keys.plus, "releasing Shift while equal is held should stop Z positive");
    applyEvdevKeyEvent(EV_KEY, KEY_EQUAL, 0, keys);

    applyEvdevKeyEvent(EV_KEY, KEY_MINUS, 1, keys);
    expect(keys.minus, "main minus should set Z negative");
    expect(makeJogRequests(keys)[2] == JogRequest{3, false},
           "minus should map to Z negative");
    applyEvdevKeyEvent(EV_KEY, KEY_LEFTSHIFT, 1, keys);
    expect(!keys.minus, "Shift+minus underscore must not start Z negative");
    applyEvdevKeyEvent(EV_KEY, KEY_LEFTSHIFT, 0, keys);
    applyEvdevKeyEvent(EV_KEY, KEY_MINUS, 0, keys);

    applyEvdevKeyEvent(EV_KEY, KEY_KPPLUS, 1, keys);
    expect(keys.plus, "keypad plus should set Z positive");
    applyEvdevKeyEvent(EV_KEY, KEY_KPMINUS, 1, keys);
    expect(makeJogRequests(keys)[2] == JogRequest{},
           "opposite Z keys should cancel Z");
    applyEvdevKeyEvent(EV_KEY, KEY_KPPLUS, 0, keys);
    applyEvdevKeyEvent(EV_KEY, KEY_KPMINUS, 0, keys);
}

void testDirectionMapping()
{
    KeySnapshot keys;
    keys.up = true;
    keys.right = true;

    const auto requests = makeJogRequests(keys);
    expect(requests[0] == JogRequest{1, true}, "UP should map to X positive");
    expect(requests[1] == JogRequest{2, false}, "RIGHT should map to Y negative");
}

void testOppositeKeysCancelOnlyTheirAxis()
{
    KeySnapshot keys;
    keys.up = true;
    keys.down = true;
    keys.left = true;

    const auto requests = makeJogRequests(keys);
    expect(requests[0] == JogRequest{}, "opposite X keys should cancel X");
    expect(requests[1] == JogRequest{2, true}, "LEFT should still map to Y positive");
}

void testEscapePressAndRelease()
{
    KeySnapshot keys;
    applyEvdevKeyEvent(EV_KEY, KEY_ESC, 1, keys);
    expect(keys.escape, "ESC press should request exit");
    applyEvdevKeyEvent(EV_KEY, KEY_ESC, 0, keys);
    expect(!keys.escape, "ESC release should clear exit state");
}

std::string descriptorPath(int descriptor)
{
    return "/proc/self/fd/" + std::to_string(descriptor);
}

void testReaderRejectsMissingDevice()
{
    EvdevKeyReader reader("/dev/input/keyboard-jog-demo-device-does-not-exist");
    expect(!reader.valid(), "missing input device should be rejected");
    expect(!reader.error().empty(), "missing input device should report a diagnostic");
}

void testReaderConsumesEventsAndFailsOnEof()
{
    int descriptors[2]{};
    expect(::pipe(descriptors) == 0, "pipe creation should succeed");
    EvdevKeyReader reader(descriptorPath(descriptors[0]));
    ::close(descriptors[0]);
    expect(reader.valid(), "reader should open a readable descriptor path");

    input_event events[2]{};
    events[0].type = EV_KEY;
    events[0].code = KEY_LEFT;
    events[0].value = 1;
    events[1].type = EV_KEY;
    events[1].code = KEY_LEFT;
    events[1].value = 2;
    expect(::write(descriptors[1], events, sizeof(events)) == sizeof(events),
           "complete input events should be written");

    KeySnapshot keys;
    expect(reader.poll(keys), "complete input events should be read successfully");
    expect(keys.left, "reader should apply a key press and ignore repeat state changes");

    ::close(descriptors[1]);
    expect(!reader.poll(keys), "input stream EOF should fail closed");
    expect(!reader.error().empty(), "input stream EOF should report a diagnostic");
}

void testReaderReportsRawKeyEvents()
{
    int descriptors[2]{};
    expect(::pipe(descriptors) == 0, "pipe creation should succeed");
    EvdevKeyReader reader(descriptorPath(descriptors[0]));
    ::close(descriptors[0]);
    expect(reader.valid(), "reader should open a readable descriptor path");

    input_event events[2]{};
    events[0].type = EV_KEY;
    events[0].code = KEY_UP;
    events[0].value = 1;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    expect(::write(descriptors[1], events, sizeof(events)) == sizeof(events),
           "key and synchronization events should be written");

    KeySnapshot keys;
    std::vector<EvdevKeyEvent> observed;
    expect(reader.poll(keys, &observed), "raw key event polling should succeed");
    expect(observed.size() == 1, "only EV_KEY records should be reported");
    expect(observed.size() == 1 && observed[0].code == KEY_UP,
           "reported raw key code should match the input event");
    expect(observed.size() == 1 && observed[0].value == 1,
           "reported raw key value should match the input event");
    ::close(descriptors[1]);
}

void testReaderRejectsPartialRecord()
{
    int descriptors[2]{};
    expect(::pipe(descriptors) == 0, "pipe creation should succeed");
    EvdevKeyReader reader(descriptorPath(descriptors[0]));
    ::close(descriptors[0]);
    expect(reader.valid(), "reader should open a readable descriptor path");

    const char partial = 0;
    expect(::write(descriptors[1], &partial, sizeof(partial)) == sizeof(partial),
           "partial input record should be written");

    KeySnapshot keys;
    expect(!reader.poll(keys), "partial input record should fail closed");
    expect(!reader.error().empty(), "partial input record should report a diagnostic");
    ::close(descriptors[1]);
}

void testReaderDiscardsEventsQueuedBeforeJoggingStarts()
{
    int descriptors[2]{};
    expect(::pipe(descriptors) == 0, "pipe creation should succeed");
    EvdevKeyReader reader(descriptorPath(descriptors[0]));
    ::close(descriptors[0]);
    expect(reader.valid(), "reader should open a readable descriptor path");

    input_event event{};
    event.type = EV_KEY;
    event.code = KEY_UP;
    event.value = 1;
    expect(::write(descriptors[1], &event, sizeof(event)) == sizeof(event),
           "queued key event should be written");
    expect(reader.discardPendingEvents(), "queued events should be discarded safely");

    KeySnapshot keys;
    expect(reader.poll(keys), "reader should remain usable after discarding events");
    expect(!keys.up, "queued key press should not enter the jogging state");
    ::close(descriptors[1]);
}

void testReaderFailsClosedWhenKernelDropsEvents()
{
    int descriptors[2]{};
    expect(::pipe(descriptors) == 0, "pipe creation should succeed");
    EvdevKeyReader reader(descriptorPath(descriptors[0]));
    ::close(descriptors[0]);
    expect(reader.valid(), "reader should open a readable descriptor path");

    input_event event{};
    event.type = EV_SYN;
    event.code = SYN_DROPPED;
    expect(::write(descriptors[1], &event, sizeof(event)) == sizeof(event),
           "SYN_DROPPED event should be written");

    KeySnapshot keys;
    expect(!reader.poll(keys), "SYN_DROPPED should fail closed");
    expect(!reader.error().empty(), "SYN_DROPPED should report a diagnostic");
    ::close(descriptors[1]);
}

} // namespace

int main()
{
    testArrowPressReleaseAndRepeat();
    testIgnoredEventsDoNotChangeState();
    testZKeyPressReleaseAndMapping();
    testDirectionMapping();
    testOppositeKeysCancelOnlyTheirAxis();
    testEscapePressAndRelease();
    testReaderRejectsMissingDevice();
    testReaderConsumesEventsAndFailsOnEof();
    testReaderReportsRawKeyEvents();
    testReaderRejectsPartialRecord();
    testReaderDiscardsEventsQueuedBeforeJoggingStarts();
    testReaderFailsClosedWhenKernelDropsEvents();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All jog input tests passed\n";
    return EXIT_SUCCESS;
}
