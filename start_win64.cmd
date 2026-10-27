@echo off
setlocal
cd /d "%~dp0"

rem Local Win64 development runtime: Qt 6, OpenCV 4.10 and the installed
rem Sapera x64 runtime are required. Sapera is loaded from Windows System32.
set "PATH=C:\Qt\6.9.0\msvc2022_64\bin;C:\source\opencv\build\x64\vc16\bin;%PATH%"

start "" "%~dp0build-win64-release\SherlockSrvr.exe"
