@echo off
rem VM側でこれをダブルクリックして実行状態（待受状態）にします
rem HttpListenerを使用するためには管理者権限が必要です
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo 管理者権限を要求しています...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"
powershell.exe -ExecutionPolicy Bypass -NoProfile -File "agent.ps1"
pause
