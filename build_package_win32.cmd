@echo off
setlocal
rem Build the latest x86 Release and deploy it as a new folder under dist.
rem Local dependency paths below match this project's Win32 VS Code tasks.
set "TASK_NO_PAUSE=0"
if /i "%~1"=="-NoPause" set "TASK_NO_PAUSE=1"
set "TASK_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "TASK_QT=E:/Bruce/qt_opencv_sr/install/qt-5.15.19-msvc2019-x86"
set "TASK_OPENCV=E:/Bruce/qt_opencv_sr/install/opencv-4.14.0-msvc2019-x86/x86/vc16/lib"
set "TASK_SAPERA=C:/Program Files/Teledyne DALSA/Sapera"
set "TASK_RESULT=1"
pushd "%~dp0"
if errorlevel 1 goto :finish
if not exist "%TASK_VCVARS%" goto :failed
call "%TASK_VCVARS%" amd64_x86 -vcvars_ver=14.29
if errorlevel 1 goto :failed

echo [1/3] Configure Win32 Release...
cmake -S . -B build-win32-release -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%TASK_QT%" "-DOpenCV_DIR=%TASK_OPENCV%" "-DSAPERA_ROOT=%TASK_SAPERA%" -DPVA_SAPERA_TARGET_RUNTIME_AVAILABLE=ON
if errorlevel 1 goto :failed

echo [2/3] Build latest executable...
cmake --build build-win32-release --target pva_dualcam_cpp --clean-first --parallel
if errorlevel 1 goto :failed

echo [3/3] Package executable and runtime DLLs...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0cpp\package_win32.ps1" -NoPause
if errorlevel 1 goto :failed
set "TASK_RESULT=0"
echo Build and packaging completed. Output folder is under dist.
goto :done

:failed
echo Build or packaging failed. Do not distribute an incomplete package.
:done
popd
:finish
if "%TASK_NO_PAUSE%"=="0" pause
exit /b %TASK_RESULT%

