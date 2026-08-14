# Ubuntu Evdev Keyboard Jog Design

## Goal

Make `keyboard_jog_demo` build and run on Ubuntu 22.04 x86-64 with the bundled Inexbot GCC 9 SDK library while preserving deterministic key-release stopping and safe cleanup. Automated validation must not connect to or move a real robot.

## Scope

- Preserve the existing robot connection, confirmation, teach-mode, servo recovery, coordinate selection, speed, jog refresh, cleanup, and mode-restoration behavior.
- Add a Linux keyboard backend based on the kernel evdev API in `<linux/input.h>`.
- Use the four direction keys directly, matching the requested Windows interaction.
- Add Linux signal handling for `SIGINT`, `SIGTERM`, and `SIGHUP`.
- Add a Linux CMake branch that links the bundled GCC 9 `libnrc_host.so`.
- Add focused CTest coverage using synthetic input events; tests do not load the robot SDK or open a controller connection.
- Update the README with Ubuntu build, device discovery, permissions, and run instructions.

The auxiliary Windows-only `key_monitor` remains available on Windows and is not built on Linux. No Python binding, GUI toolkit, ROS integration, automatic udev-rule installation, or controller protocol change is included.

## Runtime Interface

The existing positional arguments remain unchanged and a fourth positional argument is added on Linux:

```text
keyboard_jog_demo [controller_ip] [controller_port] [speed_percent] <input_device>
```

Example:

```bash
./build/keyboard_jog_demo 192.168.1.13 6001 10 /dev/input/by-id/usb-...-event-kbd
```

The Linux program requires an explicit input-device path. It does not silently choose the first `/dev/input/event*` node, because selecting the wrong device is unsafe and makes failures difficult to diagnose.

## Input Architecture

Platform-independent key state and jog-request mapping are kept separate from the OS reader. The Linux reader opens the configured input device read-only and nonblocking, waits with `poll`, and consumes complete `input_event` records.

Only `EV_KEY` records for these keys affect state:

- `KEY_UP`: Cartesian X+
- `KEY_DOWN`: Cartesian X-
- `KEY_LEFT`: Cartesian Y+
- `KEY_RIGHT`: Cartesian Y-
- `KEY_ESC`: request exit

An evdev value of `1` means pressed, `0` means released, and `2` is key-repeat. Repeat does not change the held state. Opposite directions on one axis cancel that axis. X and Y may move simultaneously, matching the implementation in `main.cpp` rather than the contradictory early README bullet.

The device is not grabbed with `EVIOCGRAB`. This preserves terminal Ctrl+C and avoids monopolizing the keyboard. Evdev events are global rather than focus-scoped: after the operator types `ENABLE`, direction keys can request motion even when another window has focus. The program prints this limitation before accepting the confirmation. This is an accepted difference from the Windows focus-event behavior.

## Failure and Cleanup Behavior

The application treats all of the following as stop conditions:

- direction release or same-axis opposite-direction conflict
- Escape press
- `SIGINT`, `SIGTERM`, or `SIGHUP`
- input-device EOF, disconnect, `POLLERR`, `POLLHUP`, `POLLNVAL`, `SYN_DROPPED`, short record, or unrecoverable read error
- robot jog API failure

Stop processing remains in the normal program loop and cleanup lambda. Signal handlers only set a signal-safe exit flag; they do not call the robot SDK.

The Linux input device is opened before connecting to the controller. If the path is missing or unreadable, the program exits without touching robot state.

`SIGKILL`, host power loss, controller/network failure, and failures inside the proprietary SDK cannot be handled by this process. Real-hardware commissioning must therefore begin at low speed, unloaded, in a clear workspace, with the physical emergency stop available.

## Build Design

On Linux x86-64, CMake links:

```text
Inexbot_API_v2.0.4-22.07/cpp/linux/linux_gcc-9_x64_v2.0.4/libnrc_host.so
```

The SDK root defaults to the bundled directory relative to the project instead of a Windows absolute path. Windows MSVC and MinGW branches remain intact. `user32` and DLL-copy steps remain Windows-only. The Linux executable receives a build RPATH pointing at the selected SDK library directory so it can run directly from the build tree.

The target platform is x86-64. Configuration fails clearly for unsupported Linux architectures or a missing SDK library.

## Test Design

A small test executable is registered with CTest. It exercises production parsing/state-transition code without linking `libnrc_host.so` and without connecting to a robot.

Coverage includes:

- Arrow press/release changes held direction state.
- Repeat events preserve the current state.
- Events of other types and unrelated keys are ignored.
- One direction produces the correct axis and sign.
- Same-axis opposite keys cancel that axis.
- Orthogonal directions produce two requests.
- Escape is surfaced as an exit request.
- Input failure is surfaced as a safe stop condition.
- Kernel-reported dropped input events fail closed because held-key state is no longer reliable.

The TDD sequence is test-first: add focused tests, observe their expected compile/test failure because the Linux input mapper does not exist, implement the minimum mapper/reader boundary, then make the tests pass.

## Validation

Validation in the current environment consists of:

1. CMake configure against the bundled SDK.
2. Full Linux build.
3. CTest execution.
4. Dynamic-link inspection confirming `libnrc_host.so` resolves.
5. A no-controller invocation that fails safely before any motion.

The current Codex environment has no visible `/dev/input` directory, so physical keyboard events and device permissions cannot be validated here. Final on-machine verification requires identifying a stable `/dev/input/by-id/*-event-kbd` path and running the executable with that explicit path.

## Acceptance Criteria

- Ubuntu 22.04 x86-64 configures and builds with the bundled GCC 9 SDK library.
- All CTest cases pass without robot hardware.
- The executable reports clear errors for a missing or unreadable input device.
- Pressing a direction requests the corresponding jog motion.
- Releasing a direction transitions its active axis to stop.
- Input errors and supported termination signals leave the main loop through the cleanup path.
- Windows build branches and existing robot lifecycle behavior are not intentionally changed.
- README instructions are sufficient to locate a keyboard device, configure access, build, and launch.

## Known Limits

- Live evdev behavior cannot be exercised in the current sandbox because `/dev/input` is unavailable.
- Evdev does not expose desktop-window focus, so direction keys remain active when another window has focus after `ENABLE` is accepted.
- Real robot motion, controller firmware compatibility, axis mapping, and controller-side watchdog behavior remain hardware commissioning checks.
- The directory is not a Git repository, so the design and implementation cannot be committed here.
