@echo off
echo Building and deploying ConfigGUI...

REM Build the application
call build.bat

REM Create deployment directory
if not exist deploy mkdir deploy
cd deploy

REM Copy executable
copy ..\build\ConfigGUI.exe .

REM Copy required Qt6 DLLs
copy "C:\Qt\6.9.1\mingw_64\bin\Qt6Core.dll" .
copy "C:\Qt\6.9.1\mingw_64\bin\Qt6Gui.dll" .

REM Check if Qt6Widgets.dll exists and copy it
if exist "C:\Qt\6.9.1\mingw_64\bin\Qt6Widgets.dll" (
    copy "C:\Qt\6.9.1\mingw_64\bin\Qt6Widgets.dll" .
    echo Qt6Widgets.dll copied successfully
) else (
    echo WARNING: Qt6Widgets.dll not found in Qt installation
    echo This may cause the application to fail to run
    echo Please install Qt6 Widgets module
)

REM Copy MinGW runtime DLLs
copy "C:\msys64\mingw64\bin\libgcc_s_seh-1.dll" .
copy "C:\msys64\mingw64\bin\libstdc++-6.dll" .
copy "C:\msys64\mingw64\bin\libwinpthread-1.dll" .

REM Create platforms directory and copy platform plugins
if not exist platforms mkdir platforms
copy "C:\Qt\6.9.1\mingw_64\plugins\platforms\qwindows.dll" platforms\
copy "C:\Qt\6.9.1\mingw_64\plugins\platforms\qdirect2d.dll" platforms\
copy "C:\Qt\6.9.1\mingw_64\plugins\platforms\qminimal.dll" platforms\
copy "C:\Qt\6.9.1\mingw_64\plugins\platforms\qoffscreen.dll" platforms\

REM Create imageformats directory and copy image format plugins
if not exist imageformats mkdir imageformats
copy "C:\Qt\6.9.1\mingw_64\plugins\imageformats\qico.dll" imageformats\

REM Create a simple README for users
echo ConfigGUI - Embedded Project Configuration Tool > README.txt
echo. >> README.txt
echo Usage: >> README.txt
echo 1. Connect your embedded device via USB-C >> README.txt
echo 2. Run ConfigGUI.exe >> README.txt
echo 3. Select COM port and click Connect >> README.txt
echo 4. Use the terminal to send commands >> README.txt
echo. >> README.txt
echo Required files: >> README.txt
echo - ConfigGUI.exe >> README.txt
echo - Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll >> README.txt
echo - platforms/qwindows.dll (Windows GUI plugin) >> README.txt
echo - MinGW runtime DLLs >> README.txt

echo.
echo Deployment complete!
echo Distributable files are in the 'deploy' folder
echo Users only need to run ConfigGUI.exe
pause 