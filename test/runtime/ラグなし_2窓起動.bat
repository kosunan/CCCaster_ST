@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\src\src\harness\start_manual_real_pair.ps1" -TestRoot "%~dp0." %*
set taskExit=%errorlevel%
if /I not "%~1"=="-CheckOnly" pause
exit /b %taskExit%
