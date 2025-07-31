@echo off
echo Setting up Qt6 and CMake environment variables...

REM Add Qt6 to PATH
setx PATH "%PATH%;C:\Qt\6.9.1\mingw_64\bin"

REM Add CMake to PATH
setx PATH "%PATH%;C:\Program Files\CMake\bin"

REM Set CMAKE_PREFIX_PATH
setx CMAKE_PREFIX_PATH "C:\Qt\6.9.1\mingw_64"

echo Environment variables set!
echo Please restart your Command Prompt for changes to take effect.
pause 