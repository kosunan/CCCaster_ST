<#
.SYNOPSIS
 HostPC側(開発機)から一発で全自動テストを実行する司令塔スクリプト
#>

$config = Get-Content -Raw -Path "$PSScriptRoot\config.json" | ConvertFrom-Json
$hostGameDir = $config.host_pc.game_dir
$buildDir = $config.host_pc.build_dir
$vmIp = $config.client_pc.ip
$agentUrl = "http://$vmIp`:8081"
$port = $config.test.port

# 自分(ホストPC)のIPアドレスを取得 (VMから繋ぐためのIP)
# config.jsonにhost_pc.ipがあればそれを優先、無ければ自動取得
$localIp = $config.host_pc.ip
if ([string]::IsNullOrEmpty($localIp) -or $localIp -eq "127.0.0.1") {
    # 簡単なIPv4取得 (最初に見つかったもの)
    $localIp = (Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias "VMware*" -ErrorAction Ignore | Select-Object -First 1).IPAddress
    if ([string]::IsNullOrEmpty($localIp)) {
        $localIp = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like "192.168.*" } | Select-Object -First 1).IPAddress
    }
}

Write-Host "=== Controller Started ===" -ForegroundColor Cyan
Write-Host "Host IP : $localIp"
Write-Host "VM Agent: $agentUrl"

# 1. ping 確認
try {
    Invoke-RestMethod -Uri "$agentUrl/ping" -Method Get -TimeoutSec 3 | Out-Null
    Write-Host "[OK] VM Agent is reachable." -ForegroundColor Green
} catch {
    Write-Host "[Error] Cannot reach VM Agent at $agentUrl. Is StartAgent.bat running on VM?" -ForegroundColor Red
    exit 1
}

# 2. リモートプロセス終了
Write-Host "`n--- Stopping remote processes ---" -ForegroundColor Cyan
try {
    Invoke-RestMethod -Uri "$agentUrl/stop" -Method Post | Out-Null
    Write-Host "[OK] Remote stopped." -ForegroundColor Green
} catch {
    Write-Host "[Warn] Failed to stop remote: $_" -ForegroundColor Yellow
}

# 3. ローカルプロセス終了
Write-Host "`n--- Stopping local processes ---" -ForegroundColor Cyan
foreach ($proc in $config.test.processes_to_kill) {
    if (Get-Process -Name $proc -ErrorAction SilentlyContinue) {
        Stop-Process -Name $proc -Force -ErrorAction SilentlyContinue
        Write-Host "Killed $proc" -ForegroundColor Green
    }
}

# 4. ホスト側へ最新ビルドをコピー
Write-Host "`n--- Deploying to Local Host ---" -ForegroundColor Cyan
$filesToDeploy = @("CCCaster_v10.exe", "libcccaster_hook.dll")
foreach ($file in $filesToDeploy) {
    $src = Join-Path $buildDir $file
    $dest = Join-Path $hostGameDir $file
    Copy-Item -Path $src -Destination $dest -Force
    Write-Host "[OK] Copied $file locally" -ForegroundColor Green
}

# 5. VM側へ最新ビルドを転送 (POST)
Write-Host "`n--- Deploying to VM Agent ---" -ForegroundColor Cyan
foreach ($file in $filesToDeploy) {
    $src = Join-Path $buildDir $file
    $bytes = [System.IO.File]::ReadAllBytes($src)
    $base64 = [Convert]::ToBase64String($bytes)
    
    $payload = @{
        filename = $file
        data = $base64
    } | ConvertTo-Json -Depth 2
    
    try {
        Write-Host "Uploading $file ..." -NoNewline
        Invoke-RestMethod -Uri "$agentUrl/deploy" -Method Post -Body $payload -ContentType "application/json" | Out-Null
        Write-Host " [OK]" -ForegroundColor Green
    } catch {
        Write-Host " [Error] $_" -ForegroundColor Red
        exit 1
    }
}

# 6. ローカルゲーム起動 (ホスト)
Write-Host "`n--- Starting Local Game ---" -ForegroundColor Cyan
Push-Location $hostGameDir
$cmdArgs = "--headless --host --port $port"
Write-Host "Command: CCCaster_v10.exe $cmdArgs" -ForegroundColor Yellow

# バックグラウンドの別プロセスとして完全に切り離して起動する
$si = New-Object System.Diagnostics.ProcessStartInfo
$si.FileName = "cmd.exe"
$si.Arguments = "/c CCCaster_v10.exe $cmdArgs"
$si.WorkingDirectory = $hostGameDir
$si.UseShellExecute = $true
$si.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Minimized
[System.Diagnostics.Process]::Start($si) | Out-Null

Pop-Location
Write-Host "[OK] Local host started! Waiting 3 seconds before starting remote client..." -ForegroundColor Green
Start-Sleep -Seconds 3

# 7. リモートゲーム起動
Write-Host "`n--- Starting Remote Game ---" -ForegroundColor Cyan
$runPayload = @{
    hostIp = $localIp
    port = $port
} | ConvertTo-Json
try {
    Invoke-RestMethod -Uri "$agentUrl/run" -Method Post -Body $runPayload -ContentType "application/json" | Out-Null
    Write-Host "[OK] Remote game started, trying to connect to $localIp`:$port" -ForegroundColor Green
} catch {
    Write-Host "[Error] Failed to start remote game: $_" -ForegroundColor Red
    exit 1
}

Write-Host "`n==================================" -ForegroundColor Cyan
Write-Host "All done! Games should be syncing and running automatically."
