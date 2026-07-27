# ============================================================================
# run_pair.ps1 — ハーネスを2プロセス起動して loopback UDP で繋ぐ
#
#   .\src\harness\run_pair.ps1
#   .\src\harness\run_pair.ps1 -HostLoadingFrames 60 -ClientLoadingFrames 180
#
# ロード時間を左右で変えると、証言②（ロード時間のばらつきでずれる）を再現できる。
# 記録は build_logs/harness/ に出力される。
# ============================================================================
param(
    [int]    $HostLoadingFrames   = 60,
    [int]    $ClientLoadingFrames = 60,
    [int]    $Rounds              = 2,
    [int]    $HostPort            = 7600,
    [int]    $ClientPort          = 7601,
    [int]    $TimeoutSeconds      = 90
)

$ErrorActionPreference = 'SilentlyContinue'
$root    = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe     = Join-Path $root 'build\bin\harness.exe'
$outDir  = Join-Path $root 'build_logs\harness'

if (-not (Test-Path $exe)) {
    Write-Output "harness.exe がありません: $exe"
    exit 1
}
New-Item -ItemType Directory -Force $outDir | Out-Null

Get-Process -Name 'harness' | Stop-Process -Force
Start-Sleep -Milliseconds 300

$hostRec   = Join-Path $outDir 'host_record.txt'
$clientRec = Join-Path $outDir 'client_record.txt'
$hostLog   = Join-Path $outDir 'host.log'
$clientLog = Join-Path $outDir 'client.log'

Write-Output "[pair] HOST   loading=${HostLoadingFrames}F  local=$HostPort   peer=$ClientPort"
Write-Output "[pair] CLIENT loading=${ClientLoadingFrames}F  local=$ClientPort  peer=$HostPort"

$h = Start-Process -FilePath $exe -PassThru -NoNewWindow -RedirectStandardOutput $hostLog `
    -ArgumentList '--host','--ip','127.0.0.1',
                  '--port',$ClientPort,'--local-port',$HostPort,
                  '--loading-frames',$HostLoadingFrames,'--rounds',$Rounds,
                  '--out',$hostRec

Start-Sleep -Milliseconds 500

$c = Start-Process -FilePath $exe -PassThru -NoNewWindow -RedirectStandardOutput $clientLog `
    -ArgumentList '--ip','127.0.0.1',
                  '--port',$HostPort,'--local-port',$ClientPort,
                  '--loading-frames',$ClientLoadingFrames,'--rounds',$Rounds,
                  '--out',$clientRec

Write-Output "[pair] 起動しました。最大 ${TimeoutSeconds} 秒待機します..."
$done = Wait-Process -Id $h.Id,$c.Id -Timeout $TimeoutSeconds -PassThru -ErrorAction SilentlyContinue
Get-Process -Name 'harness' | Stop-Process -Force

Write-Output ''
Write-Output '===== HOST (末尾8行) ====='
if (Test-Path $hostLog)   { Get-Content $hostLog   -Tail 8 }
Write-Output ''
Write-Output '===== CLIENT (末尾8行) ====='
if (Test-Path $clientLog) { Get-Content $clientLog -Tail 8 }

Write-Output ''
Write-Output '===== 記録 ====='
foreach ($p in @($hostRec, $clientRec)) {
    if (Test-Path $p) {
        $n = (Get-Content $p | Where-Object { $_ -notmatch '^#' } | Measure-Object -Line).Lines
        Write-Output ("  {0}: {1} 行" -f (Split-Path $p -Leaf), $n)
    } else {
        Write-Output ("  {0}: 出力なし" -f (Split-Path $p -Leaf))
    }
}
Write-Output 'DONE'
