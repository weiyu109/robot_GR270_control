# Ubuntu Evdev Keyboard Jog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and safely start the existing Inexbot keyboard jog demo on Ubuntu 22.04 using direct evdev direction-key press/release events and the bundled GCC 9 robot SDK library.

**Architecture:** Extract key-state-to-jog mapping into a small platform-neutral unit, add a Linux evdev reader that owns one explicit input-device file descriptor, and select Windows or Linux input/signal handling in `main.cpp`. Keep robot lifecycle behavior unchanged and test input mapping without loading the robot SDK or contacting hardware.

**Tech Stack:** C++17, Linux evdev (`linux/input.h`), POSIX `poll/read/signal`, CMake 3.20+, CTest, bundled Inexbot `libnrc_host.so`.

## Global Constraints

- Target Ubuntu is 22.04 x86-64.
- Link `Inexbot_API_v2.0.4-22.07/cpp/linux/linux_gcc-9_x64_v2.0.4/libnrc_host.so`.
- Require an explicit Linux input-device path as positional argument 4.
- Use direction keys directly; do not add a deadman key.
- Do not connect to or move a robot from automated tests.
- Preserve Windows build branches and existing robot lifecycle behavior.
- Do not add third-party dependencies.
- Do not create Git commits because this directory is not a Git repository.

---

### Task 1: Testable key-state mapping

**Files:**
- Create: `jog_input.h`
- Create: `jog_input.cpp`
- Create: `tests/jog_input_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `KeySnapshot`, `JogRequest`, `makeJogRequests(const KeySnapshot&)`, and Linux-only `applyEvdevKeyEvent(unsigned short, unsigned short, int, KeySnapshot&)`.
- Consumes: Linux `EV_KEY`, `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, and `KEY_ESC` constants.

- [ ] **Step 1: Write the failing mapping tests**

Create a dependency-free test executable that checks arrow press/release, repeat preservation, ignored events, axis/sign mapping, opposite-key cancellation, orthogonal motion, and Escape state. Include the wished-for `jog_input.h` API so the first build fails because it does not exist.

- [ ] **Step 2: Run the test build and verify RED**

Run a direct compiler check so the existing Windows-only CMake configuration cannot mask the intended failure:

```bash
g++ -std=c++17 -I. tests/jog_input_tests.cpp -o /tmp/jog_input_tests
```

Expected: build failure naming missing `jog_input.h`.

- [ ] **Step 3: Implement the minimum mapping unit**

Define the existing state and request records in `jog_input.h`. Move the current two-axis mapping to `jog_input.cpp`. On Linux, map evdev values `0` and `1`; treat value `2` as a recognized no-op and ignore unrelated event types/codes.

- [ ] **Step 4: Register and run the test**

Run:

```bash
g++ -std=c++17 -I. tests/jog_input_tests.cpp jog_input.cpp -o /tmp/jog_input_tests
/tmp/jog_input_tests
```

Expected: all mapping tests pass.

### Task 2: Linux evdev device reader

**Files:**
- Create: `linux_evdev_key_reader.h`
- Create: `linux_evdev_key_reader.cpp`
- Modify: `tests/jog_input_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `applyEvdevKeyEvent(...)` and `KeySnapshot` from Task 1.
- Produces: `EvdevKeyReader(const std::string&)`, `valid()`, `error()`, and `poll(KeySnapshot&)`.

- [ ] **Step 1: Add failing reader tests**

Test that an explicitly nonexistent path produces an invalid reader with a nonempty diagnostic. Test stream handling by creating a pipe and passing `/proc/self/fd/<read-fd>` through the real path constructor: complete synthetic records update state, repeat preserves state, EOF returns failure, malformed record length returns failure, queued pre-jog events are discarded, and `SYN_DROPPED` fails closed.

- [ ] **Step 2: Verify RED**

Run:

```bash
cmake --build build --target jog_input_tests
```

Expected: failure because `EvdevKeyReader` is not defined.

- [ ] **Step 3: Implement the reader**

Open device paths with `O_RDONLY | O_NONBLOCK | O_CLOEXEC`. Use `poll(..., 0)` and drain complete `input_event` records with `read`. Return the last held snapshot on no data. Fail closed on EOF, poll error flags, partial records, or read errors other than `EAGAIN`/`EWOULDBLOCK`/`EINTR`. Close the owned descriptor in the destructor and disable copying.

- [ ] **Step 4: Verify GREEN**

Run:

```bash
cmake --build build --target jog_input_tests
ctest --test-dir build --output-on-failure
```

Expected: all input tests pass.

### Task 3: Platform-selectable main program and Linux signals

**Files:**
- Modify: `main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: platform-neutral key state/mapping and Linux `EvdevKeyReader`.
- Preserves: controller API sequence, jog refresh interval, cleanup ordering, command-line IP/port/speed positions.
- Adds on Linux: required argument 4 `input_device`, and handlers for `SIGINT`, `SIGTERM`, and `SIGHUP`.

- [ ] **Step 1: Make the existing main consume the extracted mapping**

Remove duplicate `KeySnapshot`, `JogRequest`, and `makeJogRequests` definitions from `main.cpp`; include `jog_input.h`. Keep the Windows `ConsoleKeyReader` under `_WIN32`.

- [ ] **Step 2: Add Linux main integration**

Under `__linux__`, require `argc >= 5`, construct `EvdevKeyReader` from `argv[4]` before connecting to the controller, print the global-input warning before `ENABLE`, and register signal handlers that only set a `volatile sig_atomic_t` exit flag. Select the platform reader without changing the robot loop.

- [ ] **Step 3: Add Linux SDK linking**

Default `NRC_SDK_ROOT` to `${CMAKE_CURRENT_SOURCE_DIR}/Inexbot_API_v2.0.4-22.07`. Keep MSVC/MinGW library selection and DLL copying. On Linux x86-64, link the GCC 9 `.so`, set build RPATH to its directory, omit `user32`, and do not build `key_monitor`.

- [ ] **Step 4: Build the whole project**

Run:

```bash
cmake -S . -B build
cmake --build build --parallel
```

Expected: `keyboard_jog_demo` and `jog_input_tests` build successfully.

### Task 4: Ubuntu operating instructions and final verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Documents: SDK selection, device discovery, input-group permission, build, run, controls, global-input limitation, and hardware commissioning boundary.

- [ ] **Step 1: Update README**

Document Ubuntu commands, an explicit `/dev/input/by-id/*-event-kbd` argument, `ls -l`/`readlink -f` device discovery, temporary diagnosis of permissions, persistent `input` group setup, logout/login requirement, direct direction controls, and the fact that window focus does not gate evdev.

- [ ] **Step 2: Run automated verification**

Run:

```bash
ctest --test-dir build --output-on-failure
ldd build/keyboard_jog_demo
```

Expected: all tests pass and `libnrc_host.so` resolves to the bundled GCC 9 path.

- [ ] **Step 3: Run safe CLI smoke checks**

Run the executable without argument 4 and with no robot connection attempt expected:

```bash
./build/keyboard_jog_demo
```

Then run with an unreachable loopback controller and nonexistent device:

```bash
./build/keyboard_jog_demo 127.0.0.1 1 10 /dev/input/does-not-exist
```

Expected: clear nonzero exit without any jog call or hardware motion. Record which validation stops first based on final initialization order.

- [ ] **Step 4: Inspect final scope**

Review the changed files and confirm there are no generated build artifacts treated as source, no unrelated rewrites, and no automatic real-robot invocation.
