cmake_minimum_required(VERSION 3.20)

project(InexbotKeyboardJogDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NRC_SDK_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/Inexbot_API_v2.0.4-22.07"
    CACHE PATH "Root directory of the Inexbot host-computer SDK")

# 公共输入状态和 Linux evdev 读取器与机器人 SDK 解耦，便于无硬件测试。
add_library(jog_input STATIC src/input/jog_input.cpp)
target_include_directories(jog_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# 机器人控制层与键盘/界面解耦，供后续 Qt、PySide/ROS2 桥接或其他上位机复用。
add_library(robot_jog_controller STATIC
    src/robot_control/robot_controller_common.cpp
    src/robot_control/robot_connection.cpp
    src/robot_control/robot_mode_control.cpp
    src/robot_control/robot_error_control.cpp
    src/robot_control/robot_power_control.cpp
    src/robot_control/robot_servo_recovery.cpp
    src/robot_control/robot_jog_control.cpp
    src/robot_control/robot_joint_motion.cpp
    src/robot_control/robot_cartesian_motion.cpp
    src/robot_control/robot_jog_session.cpp
    src/robot_control/robot_sdk_api.cpp)
target_include_directories(robot_jog_controller PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${NRC_SDK_ROOT}/interface")
target_link_libraries(robot_jog_controller PUBLIC jog_input)

add_executable(keyboard_jog_demo apps/keyboard_jog_demo.cpp)
target_link_libraries(keyboard_jog_demo PRIVATE robot_jog_controller)

if(MSVC)
    target_sources(keyboard_jog_demo PRIVATE src/input/windows_console_key_reader.cpp)
    add_executable(key_monitor apps/key_monitor.cpp)
    target_compile_options(keyboard_jog_demo PRIVATE /utf-8)
    target_compile_options(key_monitor PRIVATE /utf-8)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_msvc2017_x64_v2.0.4")
    else()
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_msvc2017_x86_v2.0.4")
    endif()
    target_link_libraries(keyboard_jog_demo PRIVATE "${NRC_LIB_DIR}/nrc_host.lib" user32)
    target_link_libraries(key_monitor PRIVATE user32)
elseif(MINGW)
    target_sources(keyboard_jog_demo PRIVATE src/input/windows_console_key_reader.cpp)
    add_executable(key_monitor apps/key_monitor.cpp)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_mingw64_v2.0.4")
    else()
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_mingw32_v2.0.4")
    endif()
    target_link_libraries(keyboard_jog_demo PRIVATE
        "${NRC_LIB_DIR}/libnrc_host.dll.a" user32)
    target_link_libraries(key_monitor PRIVATE user32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # 当前随包 Linux SDK 只提供 x86-64 GCC 9 动态库。
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR "The bundled Inexbot Linux SDK supports x86-64 only.")
    endif()

    set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/linux/linux_gcc-9_x64_v2.0.4")
    set(NRC_HOST_LIBRARY "${NRC_LIB_DIR}/libnrc_host.so")
    if(NOT EXISTS "${NRC_HOST_LIBRARY}")
        message(FATAL_ERROR "Inexbot Linux library not found: ${NRC_HOST_LIBRARY}")
    endif()

    add_library(linux_evdev_key_reader STATIC src/input/linux_evdev_key_reader.cpp)
    target_include_directories(linux_evdev_key_reader PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(linux_evdev_key_reader PUBLIC jog_input)

    add_executable(keyboard_input_test apps/keyboard_input_test.cpp)
    target_link_libraries(keyboard_input_test PRIVATE linux_evdev_key_reader)

    target_link_libraries(keyboard_jog_demo PRIVATE
        linux_evdev_key_reader
        "${NRC_HOST_LIBRARY}")
    set_target_properties(keyboard_jog_demo PROPERTIES BUILD_RPATH "${NRC_LIB_DIR}")

    include(CTest)
    if(BUILD_TESTING)
        add_executable(jog_input_tests tests/jog_input_tests.cpp)
        target_link_libraries(jog_input_tests PRIVATE linux_evdev_key_reader)
        add_executable(robot_jog_controller_tests tests/robot_jog_controller_tests.cpp)
        target_link_libraries(robot_jog_controller_tests PRIVATE
            robot_jog_controller
            "${NRC_HOST_LIBRARY}")
        add_test(NAME jog_input_tests COMMAND jog_input_tests)
        add_test(NAME robot_jog_controller_tests COMMAND robot_jog_controller_tests)
        add_test(NAME keyboard_input_test_help COMMAND keyboard_input_test --help)
    endif()
else()
    message(FATAL_ERROR "Unsupported platform for this demo.")
endif()

if(WIN32)
    add_custom_command(TARGET keyboard_jog_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NRC_LIB_DIR}/nrc_host.dll"
                "$<TARGET_FILE_DIR:keyboard_jog_demo>/nrc_host.dll")
endif()
