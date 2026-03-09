<#
.SYNOPSIS
 両PCのCCCaster_v10/MBAAプロセスを終了するクリーンアップスクリプト。
#>
param(
    [switch]$LocalOnly = $false
)

$config = Get-Content -Raw -Path "$PSScriptRoot\config.json" | ConvertFrom-Json
$procs = $config.test.processes_to_kill

Write-Host "=== Process Cleanup ===" -ForegroundColor Cyan

foreach ($proc in $procs) {
    # ローカルのプロセスキル
    $localProcs = Get-Process -Name $proc -ErrorAction SilentlyContinue
    if ($localProcs) {
        Stop-Process -Name $proc -Force -ErrorAction SilentlyContinue
        Write-Host "Killed '$proc' on Local PC." -ForegroundColor Green
    } else {
        Write-Host "No '$proc' running on Local PC."
    }
}

Write-Host "=======================" -ForegroundColor Cyan
