@echo off
rem Package the existing x86 Release build. Keep window open by default.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0cpp\package_win32.ps1" %*
exit /b %errorlevel%
