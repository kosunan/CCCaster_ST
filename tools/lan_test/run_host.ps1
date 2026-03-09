<#
.SYNOPSIS
 開発機（Host）側の自動テストを起動するスクリプト。
#>

$config = Get-Content -Raw -Path "$PSScriptRoot\config.json" | ConvertFrom-Json
$hostGameDir = $config.host_pc.game_dir
$port = $config.test.port

Write-Host "=== Starting Host ($($config.host_pc.ip)) ===" -ForegroundColor Cyan
Write-Host "Working Directory: $hostGameDir"

if (-Not (Test-Path $hostGameDir)) {
    Write-Host "Host game directory not found!" -ForegroundColor Red
    exit 1
}

Push-Location $hostGameDir

# --headless で起動し、autoTestMode を有効化する
$args = "--headless --host --port $port"
Write-Host "Command: cccaster_v10.exe $args" -ForegroundColor Yellow

Start-Process "cmd.exe" -ArgumentList "/c CCCaster_v10.exe $args & pause"
Write-Host "Host started. Waiting for connection..." -ForegroundColor Green

Pop-Location
