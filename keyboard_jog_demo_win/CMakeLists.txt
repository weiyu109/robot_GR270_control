cmake_minimum_required(VERSION 3.20)

project(InexbotKeyboardJogDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NRC_SDK_ROOT
    "C:/Users/59857/Desktop/Inexbot Teaching Studio/纳博特二次开发/上位机sdk/库/Inexbot_API_v2.0.4-22.07"
    CACHE PATH "Root directory of the Inexbot host-computer SDK")

add_executable(keyboard_jog_demo main.cpp)
add_executable(key_monitor key_monitor.cpp)
target_include_directories(keyboard_jog_demo PRIVATE "${NRC_SDK_ROOT}/interface")

if(MSVC)
    target_compile_options(keyboard_jog_demo PRIVATE /utf-8)
    target_compile_options(key_monitor PRIVATE /utf-8)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_msvc2017_x64_v2.0.4")
    else()
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_msvc2017_x86_v2.0.4")
    endif()
    target_link_libraries(keyboard_jog_demo PRIVATE "${NRC_LIB_DIR}/nrc_host.lib")
elseif(MINGW)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_mingw64_v2.0.4")
    else()
        set(NRC_LIB_DIR "${NRC_SDK_ROOT}/cpp/windows/temp_win_mingw32_v2.0.4")
    endif()
    target_link_libraries(keyboard_jog_demo PRIVATE "${NRC_LIB_DIR}/libnrc_host.dll.a")
else()
    message(FATAL_ERROR "This demo requires MSVC or MinGW on Windows.")
endif()

target_link_libraries(keyboard_jog_demo PRIVATE user32)
target_link_libraries(key_monitor PRIVATE user32)

add_custom_command(TARGET keyboard_jog_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NRC_LIB_DIR}/nrc_host.dll"
            "$<TARGET_FILE_DIR:keyboard_jog_demo>/nrc_host.dll")
