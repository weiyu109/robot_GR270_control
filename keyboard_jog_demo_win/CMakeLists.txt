cmake_minimum_required(VERSION 3.20)

project(InexbotKeyboardJogDemoWin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(NRC_SDK_ROOT
    "${PROJECT_ROOT}/Inexbot_API_v2.0.4-22.07"
    CACHE PATH "Root directory of the Inexbot host-computer SDK")

# Windows 子目录只保留兼容构建入口，所有正式模块统一来自项目根目录。
add_library(jog_input STATIC "${PROJECT_ROOT}/src/input/jog_input.cpp")
target_include_directories(jog_input PUBLIC "${PROJECT_ROOT}/include")

add_library(robot_jog_controller STATIC
    "${PROJECT_ROOT}/src/robot_control/robot_controller_common.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_connection.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_mode_control.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_error_control.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_power_control.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_servo_recovery.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_jog_control.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_joint_motion.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_cartesian_motion.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_state_query.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_tcp_control.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_queue_motion.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_jog_session.cpp"
    "${PROJECT_ROOT}/src/robot_control/robot_sdk_api.cpp")
target_include_directories(robot_jog_controller PUBLIC
    "${PROJECT_ROOT}/include"
    "${NRC_SDK_ROOT}/interface")
target_link_libraries(robot_jog_controller PUBLIC jog_input)

add_library(robot_terminal STATIC
    "${PROJECT_ROOT}/src/terminal/robot_debug_menu.cpp"
    "${PROJECT_ROOT}/src/terminal/keyboard_teleop_action.cpp")
target_include_directories(robot_terminal PUBLIC "${PROJECT_ROOT}/include")
target_link_libraries(robot_terminal PUBLIC robot_jog_controller)

add_executable(keyboard_jog_demo
    main.cpp
    "${PROJECT_ROOT}/src/input/windows_console_key_reader.cpp")
target_include_directories(keyboard_jog_demo PRIVATE "${PROJECT_ROOT}/include")
target_link_libraries(keyboard_jog_demo PRIVATE robot_terminal user32)

if(MSVC)
    target_compile_options(keyboard_jog_demo PRIVATE /utf-8)
    target_compile_options(robot_terminal PRIVATE /utf-8)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_msvc2017_x64_v2.0.4")
    else()
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_msvc2017_x86_v2.0.4")
    endif()
    target_link_libraries(robot_jog_controller PUBLIC "${NRC_LIB_DIR}/nrc_host.lib")
elseif(MINGW)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_mingw64_v2.0.4")
    else()
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_mingw32_v2.0.4")
    endif()
    target_link_libraries(robot_jog_controller PUBLIC "${NRC_LIB_DIR}/libnrc_host.dll.a")
else()
    message(FATAL_ERROR "keyboard_jog_demo_win must be built with MSVC or MinGW.")
endif()

add_custom_command(TARGET keyboard_jog_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NRC_LIB_DIR}/nrc_host.dll"
            "$<TARGET_FILE_DIR:keyboard_jog_demo>/nrc_host.dll")
