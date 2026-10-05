@echo off
setlocal
cd /d "%~dp0"
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" %*
set "deploy_result=%ERRORLEVEL%"
if not "%deploy_result%"=="0" echo Deployment failed. See the error above.
exit /b %deploy_result%
