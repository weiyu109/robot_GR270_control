@echo off
title Inexbot Robot Jog Demo
cd /d "%~dp0build"
keyboard_jog_demo.exe 192.168.1.13 6001 20
echo.
echo Robot demo exited. Press any key to close this window.
pause >nul
