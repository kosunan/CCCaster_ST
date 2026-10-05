param(
    [Alias('Check')][switch]$CheckOnly,
    [switch]$Lag,
    [ValidateRange(0,10000)][int]$LagMinMs=60,
    [ValidateRange(0,10000)][int]$LagMaxMs=96,
    [ValidateRange(1024,65535)][int]$Port=17860
)
$ErrorActionPreference='Stop'
$taskStarted=@()
try {
    if($LagMinMs -gt $LagMaxMs) {
        throw "模擬遅延の下限が上限を超えています: $LagMinMs～$LagMaxMs ms"
    }
    $taskRuntime=$PSScriptRoot
    $taskBuild=[IO.Path]::GetFullPath((Join-Path $taskRuntime '../../build/bin'))
    $taskFiles=@('CCCaster_Steam.exe','CCCaster_Steam_GUI.exe','libcccaster_steam_hook.dll')
    $taskDirs=@(1..3 | ForEach-Object {Join-Path $taskRuntime "MBAACC_$_/cccaster_st"})
    $taskPlans=@(
        "--headless --legacy-host --port $Port"
        "--headless --ip 127.0.0.1 --port $Port"
        "--spectate --ip 127.0.0.1 --port $Port"
    )
    if($Lag) {
        $taskPlans[0]+=" --sim-delay $LagMinMs,$LagMaxMs --sim-loss 5"
        $taskPlans[1]+=" --sim-delay $LagMinMs,$LagMaxMs --sim-loss 5"
    }
    Write-Host 'CCCaster Steam 対戦2窓＋観戦1窓'
    Write-Host 'MBAACC_1: ホスト / MBAACC_2: 対戦相手 / MBAACC_3: 観戦'
    $taskMode=if($Lag){"模擬遅延$LagMinMs～$LagMaxMs ms・損失5%"}else{'模擬遅延・損失なし'}
    Write-Host "接続先 127.0.0.1:$Port / $taskMode / 手操作"
    foreach($taskFile in $taskFiles) {
        if(!(Test-Path -LiteralPath (Join-Path $taskBuild $taskFile) -PathType Leaf)) {
            throw "ビルド成果物がありません: $taskBuild/$taskFile"
        }
    }
    foreach($taskDir in $taskDirs) {
        if(!(Test-Path -LiteralPath $taskDir -PathType Container) -or
           !(Test-Path -LiteralPath (Join-Path $taskDir '../MBAA.exe') -PathType Leaf)) {
            throw "テスト用ゲームコピーがありません: $taskDir/../MBAA.exe"
        }
    }
    # 起動中のゲーム/ランチャーがあれば、ファイル変更より前に中止する。
    $taskGameRoots=@($taskDirs | ForEach-Object {(Split-Path -Parent $_)+'\'})
    $taskBusy=@(Get-CimInstance Win32_Process | Where-Object {
        $taskPath=$_.ExecutablePath
        $taskPath -and @($taskGameRoots | Where-Object {
            $taskPath.StartsWith($_,[StringComparison]::OrdinalIgnoreCase)
        }).Count
    })
    if($taskBusy.Count){throw 'テスト用ゲームまたはランチャーが既に起動中です。閉じてから再実行してください。'}
    if((Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) -or
       (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)) {
        throw "ポート $Port が使用中です。-Port 17861 等で変更できます。"
    }
    if($CheckOnly) {
        Write-Host '[確認OK] 必要ファイル・対象プロセス・UDP/TCPポートを確認しました。'
        for($taskIndex=0;$taskIndex -lt 3;$taskIndex++) {
            Write-Host ("MBAACC_{0}: {1}" -f ($taskIndex+1),$taskPlans[$taskIndex])
        }
        exit 0
    }
    # 自動入力や強制再計算などを親シェルから手操作テストへ持ち越さない。
    # このPowerShell子プロセス内だけの変更で、ユーザー/システム環境は変えない。
    Get-ChildItem Env: | Where-Object {$_.Name -like 'CCCASTER_*'} | ForEach-Object {
        Remove-Item -LiteralPath ("Env:"+$_.Name)
    }
    $taskOut=[IO.Path]::GetFullPath((Join-Path $taskRuntime ('../logs/triple_test_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))))
    New-Item -ItemType Directory -Path $taskOut | Out-Null
    # 1.4のGUI・ステージ画像・ライセンスも同じビルドに揃える。
    $taskDeploy=Join-Path $taskRuntime '../../deploy.ps1'
    & pwsh -NoProfile -File $taskDeploy -TestRoot $taskRuntime
    if($LASTEXITCODE -ne 0){throw '最新版の配備に失敗しました。deployログを確認してください。'}
    $taskIniBefore=@{}
    for($taskIndex=0;$taskIndex -lt 3;$taskIndex++) {
        $taskDir=$taskDirs[$taskIndex]
        $taskBackup=Join-Path $taskOut ('before_'+($taskIndex+1))
        New-Item -ItemType Directory -Path $taskBackup | Out-Null
        Get-ChildItem -LiteralPath $taskDir -File | Where-Object {
            $_.Extension -eq '.log' -or $_.Name -like '*log*.txt'
        } | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $taskBackup}
        Get-ChildItem -LiteralPath $taskDir -Filter '*.ini' -File | ForEach-Object {
            $taskIniBefore[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash
        }
        foreach($taskFile in $taskFiles) {
            $taskSource=Join-Path $taskBuild $taskFile
            $taskDestination=Join-Path $taskDir $taskFile
            Copy-Item -LiteralPath $taskSource -Destination $taskDestination
            if((Get-FileHash -LiteralPath $taskSource).Hash -ne (Get-FileHash -LiteralPath $taskDestination).Hash) {
                throw "成果物の配置に失敗しました: $taskDestination"
            }
        }
    }
    $taskIniBefore | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOut 'ini_before.json') -Encoding UTF8
    Write-Host "起動ログ: $taskOut"
    for($taskIndex=0;$taskIndex -lt 3;$taskIndex++) {
        if($taskIndex -eq 2) {
            Write-Host '観戦のTCP受付を待っています。'
            $taskUntil=[DateTime]::UtcNow.AddSeconds(30)
            do {
                foreach($taskProcess in $taskStarted) {
                    if($taskProcess.HasExited){throw '対戦側が終了しました。起動ログを確認してください。'}
                }
                $taskListening=Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
                if($taskListening){break}
                Start-Sleep -Milliseconds 300
            } while([DateTime]::UtcNow -lt $taskUntil)
            if(!$taskListening){throw '観戦の受付が始まりませんでした。起動ログを確認してください。'}
        }
        $taskSide=$taskIndex+1
        $taskProcess=Start-Process -FilePath (Join-Path $taskDirs[$taskIndex] 'CCCaster_Steam.exe') `
            -WorkingDirectory $taskDirs[$taskIndex] -ArgumentList $taskPlans[$taskIndex] `
            -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $taskOut "launcher_$taskSide.log") `
            -RedirectStandardError (Join-Path $taskOut "launcher_$taskSide.err")
        $taskStarted+=$taskProcess
        # 途中失敗しても、今回起動した個体をログから確認できる。
        @($taskStarted | Select-Object Id) | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOut 'launchers.json') -Encoding UTF8
        Write-Host "[$taskSide/3] MBAACC_$taskSide 起動: launcher PID=$($taskProcess.Id)"
        if($taskIndex -eq 0){Start-Sleep -Seconds 3}
    }
    Start-Sleep -Seconds 1
    foreach($taskProcess in $taskStarted) {
        if($taskProcess.HasExited){throw 'ランチャーが終了しました。起動ログを確認してください。'}
    }
    Write-Host '3窓を起動しました。対戦2窓でキャラを決めると、3窓目に対戦が表示されます。'
    Write-Host '対戦側はF4で各窓の入力機器を設定できます。終了時はゲームウィンドウを閉じてください。'
    Write-Host 'バッチ終了後もゲームは継続します。自動終了の時間制限はありません。'
    exit 0
} catch {
    Write-Host "[中止] $($_.Exception.Message)" -ForegroundColor Red
    if($taskStarted.Count){Write-Host '今回起動済みのゲームはそのまま残しています。終了する場合は各ウィンドウを閉じてください。'}
    exit 1
}
