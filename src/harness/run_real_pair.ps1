# ============================================================================
# run_real_pair.ps1 — 実機(MBAA)2窓の自動テスト
#
#   .\src\harness\run_real_pair.ps1
#   .\src\harness\run_real_pair.ps1 -SimDelay '50,90' -SimLoss 20 -WaitSeconds 90
#
# dual_test.bat との違い:
#   1. 必ずビルド成果物をデプロイしてから起動する（古い DLL を掴む事故を防ぐ）
#   2. CCCASTER_SCRIPT_INPUT=1 で入力を自動生成する（人の操作に依存しない）
#   3. 両プロセスの [REC] ログを突き合わせて決定性を判定する
#
# 人が操作する必要はない。ゲームウィンドウが2つ起動し、終了時に閉じられる。
# ============================================================================
param(
    [string] $SimDelay     = '',      # 例 '50,90'。空なら遅延注入なし
    [int]    $SimLoss      = 0,       # パケットロス率 %
    [int]    $Port         = 7500,
    [int]    $WaitSeconds  = 75
)

$ErrorActionPreference = 'SilentlyContinue'
$root  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$test  = Join-Path $root '_TEST_MBAACC'
$binDll = Join-Path $root 'build\bin\libcccaster_hook.dll'
$binExe = Join-Path $root 'build\bin\CCCaster_v10.exe'

if (-not (Test-Path $binDll)) { Write-Output "ビルド成果物がありません: $binDll"; exit 1 }

# ── [1] デプロイ（これを飛ばすと古い DLL がテストされる）──
Write-Output '[1] デプロイ'
foreach ($i in 1,2) {
    $dst = Join-Path $test "MBAACC_$i\cccaster"
    if (-not (Test-Path $dst)) { Write-Output "  テスト環境がありません: $dst"; exit 1 }
    Copy-Item -Force $binDll,$binExe $dst
}
$want = (Get-FileHash $binDll -Algorithm MD5).Hash
foreach ($i in 1,2) {
    $got = (Get-FileHash (Join-Path $test "MBAACC_$i\cccaster\libcccaster_hook.dll") -Algorithm MD5).Hash
    if ($got -ne $want) { Write-Output "  MBAACC_$i のデプロイに失敗"; exit 1 }
}
Write-Output "  OK (md5=$($want.Substring(0,8)))"

# ── [2] クリーンアップ ──
Write-Output '[2] 既存プロセス停止とログ削除'
Get-Process -Name 'MBAA','CCCaster_v10' | Stop-Process -Force
Start-Sleep -Seconds 2
foreach ($i in 1,2) {
    Remove-Item -Force (Join-Path $test "MBAACC_$i\cccaster\cccaster_hook_log.txt")
}

# ── [3] 起動（入力は自動生成）──
$env:CCCASTER_SCRIPT_INPUT = '1'
Write-Output '[3] 起動 (CCCASTER_SCRIPT_INPUT=1 — 手動操作は不要)'

$common = @()
if ($SimDelay -ne '') { $common += @('--sim-delay', $SimDelay) }
if ($SimLoss  -gt 0)  { $common += @('--sim-loss', "$SimLoss") }

Start-Process -FilePath (Join-Path $test 'MBAACC_1\cccaster\CCCaster_v10.exe') `
    -WorkingDirectory (Join-Path $test 'MBAACC_1\cccaster') `
    -ArgumentList (@('--headless','--host','--port',"$Port") + $common)

Start-Sleep -Seconds 3

Start-Process -FilePath (Join-Path $test 'MBAACC_2\cccaster\CCCaster_v10.exe') `
    -WorkingDirectory (Join-Path $test 'MBAACC_2\cccaster') `
    -ArgumentList (@('--headless','--ip','127.0.0.1','--port',"$Port") + $common)

Write-Output "[4] $WaitSeconds 秒待機"
Start-Sleep -Seconds $WaitSeconds

Write-Output '[5] プロセス停止'
Get-Process -Name 'MBAA','CCCaster_v10' | Stop-Process -Force
Start-Sleep -Seconds 1

# ── [6] 判定 ──
$logs = @{}
foreach ($i in 1,2) { $logs[$i] = Join-Path $test "MBAACC_$i\cccaster\cccaster_hook_log.txt" }

function Read-Rec($path) {
    $map = @{}
    if (-not (Test-Path $path)) { return $map }
    foreach ($line in Get-Content $path) {
        if ($line -notmatch '^\[REC\] ') { continue }
        $a = $line.Substring(6).Split(' ')
        if ($a.Count -lt 5) { continue }
        $map[[uint32]$a[0]] = ($a[1..4] -join ' ')
    }
    return $map
}

Write-Output ''
Write-Output '===== 進行 ====='
foreach ($i in 1,2) {
    $name = if ($i -eq 1) { 'HOST  ' } else { 'CLIENT' }
    $phase = (Get-Content $logs[$i] | Where-Object { $_ -match '^\[SceneRunner\] phase=' } | Select-Object -Last 1)
    Write-Output "  ${name}: $phase"
}

$h = Read-Rec $logs[1]
$c = Read-Rec $logs[2]

Write-Output ''
Write-Output '===== 決定性チェック ====='
Write-Output ("  host  : {0} フレーム配信" -f $h.Count)
Write-Output ("  client: {0} フレーム配信" -f $c.Count)

if ($h.Count -eq 0 -or $c.Count -eq 0) {
    Write-Output '  [NG] 配信記録がありません（入力がゲームに届いていない）'
    Write-Output 'DONE'
    exit 1
}

$common2  = $h.Keys | Where-Object { $c.ContainsKey($_) } | Sort-Object
$mismatch = @($common2 | Where-Object { $h[$_] -ne $c[$_] })

Write-Output ("  共通フレーム: {0}" -f $common2.Count)
if ($mismatch.Count -eq 0) {
    Write-Output '  [OK] 共通フレームの入力列は完全に一致'
} else {
    Write-Output ("  [NG] {0} フレームで不一致。先頭5件:" -f $mismatch.Count)
    foreach ($f in ($mismatch | Select-Object -First 5)) {
        Write-Output ("    netFrame={0}  host=[{1}]  client=[{2}]" -f $f, $h[$f], $c[$f])
    }
}
Write-Output 'DONE'
