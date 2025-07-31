@echo off
echo Building ConfigGUI with MinGW...

REM Create build directory
if not exist build mkdir build
cd build

REM Configure with CMake for MinGW
"C:\Program Files\CMake\bin\cmake.exe" .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:\Qt\6.9.1\mingw_64"

REM Build the project
"C:\Program Files\CMake\bin\cmake.exe" --build . --config Release

echo Build complete!
echo Executable location: build\ConfigGUI.exe
pause 