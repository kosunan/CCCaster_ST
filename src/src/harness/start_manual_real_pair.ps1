param(
    [Parameter(Mandatory=$true)][string]$TestRoot,
    [switch]$Lag,
    [switch]$CheckOnly,
    [ValidateRange(1024,65535)][int]$Port=17860
)
$ErrorActionPreference='Stop'
try {
    $taskTest=(Resolve-Path -LiteralPath $TestRoot).Path
    $taskTargets=@()
    $taskProcesses=@(Get-CimInstance Win32_Process)
    foreach($taskSide in 1,2) {
        $taskGame=Join-Path $taskTest "MBAACC_$taskSide\MBAA.exe"
        $taskDir=Join-Path $taskTest "MBAACC_$taskSide\cccaster"
        $taskLauncher=Join-Path $taskDir 'CCCaster_Steam.exe'
        foreach($taskRequired in @($taskGame,$taskLauncher,(Join-Path $taskDir 'libcccaster_steam_hook.dll'))) {
            if(!(Test-Path -LiteralPath $taskRequired -PathType Leaf)){throw "必要なファイルがありません: $taskRequired"}
        }
        if(@($taskProcesses | Where-Object {$_.ExecutablePath -eq $taskGame -or $_.ExecutablePath -eq $taskLauncher}).Count) {
            throw "MBAACC_$taskSide は既に起動中です。既存の対戦を終了してから再実行してください。"
        }
        $taskTargets += [pscustomobject]@{Side=$taskSide;Directory=$taskDir;Launcher=$taskLauncher}
    }
    if(@(Get-NetUDPEndpoint -ErrorAction Stop | Where-Object {$_.LocalPort -eq $Port}).Count) {
        throw "UDPポート $Port は使用中です。既存の対戦を終了するか、-Port 17861 等を指定してください。"
    }
    # 手操作を使う。親シェルに残った自動試験・強制再計算を子プロセスへ渡さない。
    foreach($taskVariable in @(Get-ChildItem Env: | Where-Object {
        $_.Name -like 'CCCASTER_TEST_*' -or $_.Name -in @(
            'CCCASTER_SCRIPT_INPUT','CCCASTER_COMBAT_STRESS','CCCASTER_TIME_SCALE',
            'CCCASTER_MEM_TRACE','CCCASTER_FRAME_TIMING_TRACE','CCCASTER_TRACE_RNG',
            'CCCASTER_PACE_TRACE','CCCASTER_STEAM_IPC_NAME')
    })) { Remove-Item -LiteralPath "Env:$($taskVariable.Name)" }
    if($Lag){$env:CCCASTER_TEST_NETWORK='60,96,5'}
    $taskMode=if($Lag){'ラグあり（模擬遅延60～96ms・損失5%）'}else{'ラグなし（模擬遅延・損失なし）'}
    Write-Host "Steam 2窓手操作テスト: $taskMode"
    Write-Host "接続先: 127.0.0.1:$Port / 自動入力: OFF"
    if($CheckOnly){Write-Host '起動前チェック成功。ゲームは起動していません。';exit 0}
    $taskLogs=Join-Path $taskTest ('manual_logs\'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
    New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
    foreach($taskTarget in $taskTargets) {
        $taskSide=$taskTarget.Side
        $taskOldLog=Join-Path $taskTarget.Directory 'cccaster_hook_log.txt'
        if(Test-Path -LiteralPath $taskOldLog) {
            Copy-Item -LiteralPath $taskOldLog -Destination (Join-Path $taskLogs "before_game_$taskSide.log")
        }
        $taskArgs=if($taskSide -eq 1){@('--headless','--host','--port',"$Port")}else{@('--headless','--ip','127.0.0.1','--port',"$Port")}
        $taskProcess=Start-Process -FilePath $taskTarget.Launcher -WorkingDirectory $taskTarget.Directory `
            -ArgumentList $taskArgs -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $taskLogs "launcher_$taskSide.log") `
            -RedirectStandardError (Join-Path $taskLogs "launcher_$taskSide.err")
        Write-Host "MBAACC_$taskSide 起動: launcher PID=$($taskProcess.Id)"
        if($taskSide -eq 1){Start-Sleep -Seconds 3}
    }
    Write-Host '2窓の接続・キャラクター選択画面への移動は自動です。以降は手操作してください。'
    Write-Host 'F4で各窓の入力設定を行えます。対戦終了はゲームのウィンドウを閉じてください。'
    Write-Host 'このバッチ画面を閉じても、ゲームは継続します。時間制限はありません。'
    Write-Host "起動ログ: $taskLogs"
    Write-Host 'ゲームログ: 各MBAACCフォルダー内のcccaster\cccaster_hook_log.txt'
    exit 0
} catch {
    Write-Host "起動できませんでした: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
