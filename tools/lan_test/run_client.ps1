<#
.SYNOPSIS
 VMマシンの内部で直接ダブルクリック、または手動で実行するクライアント側の起動スクリプト。
 ホストマシンのIPへ接続します。
#>

$hostIp = "192.168.206.1" # VMから見たホストPCのIP（必要に応じて書き換えてください）
$port = 10800

Write-Host "=== Starting Client (VM) ===" -ForegroundColor Cyan
Write-Host "Target Host: $hostIp`:$port"

# --headless で起動し、autoTestMode を有効化する
$args = "--headless --ip $hostIp --port $port"
Write-Host "Command: cccaster_v10.exe $args" -ForegroundColor Yellow

Start-Process "cmd.exe" -ArgumentList "/c CCCaster_v10.exe $args & pause"
Write-Host "Client started. Attempting connection..." -ForegroundColor Green
