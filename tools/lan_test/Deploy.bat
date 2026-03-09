@echo off
powershell.exe -ExecutionPolicy Bypass -NoProfile -File "%~dp0deploy.ps1"
pause
