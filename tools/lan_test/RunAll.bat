@echo off
rem PC-A(開発機)用。ワンクリックで全自動デプロイ＆テスト起動を行います。
powershell.exe -ExecutionPolicy Bypass -NoProfile -File "%~dp0controller.ps1"
pause
