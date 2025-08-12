@echo off
echo Building ConfigGUI with MinGW...

REM Create build directory
if not exist build mkdir build
cd build

REM Clean previous build artifacts
if exist CMakeCache.txt del CMakeCache.txt
if exist CMakeFiles rmdir /s /q CMakeFiles

REM Configure with CMake for MinGW
"C:\Program Files\CMake\bin\cmake.exe" .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:\Qt\6.9.1\mingw_64"

REM Build the project
"C:\Program Files\CMake\bin\cmake.exe" --build . --config Release --clean-first

echo Build complete!
echo Executable location: build\ConfigGUI.exe
pause 