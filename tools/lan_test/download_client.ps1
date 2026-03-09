<#
.SYNOPSIS
 ホストPCで立ち上がっている HTTP サーバーからビルド成果物をダウンロードします。
 **このスクリプトはVM側で実行してください。**
#>

$hostIp = "192.168.206.1" # VMから見たホストPCのIP（通常はVMnet8のアダプターIP等）
$httpPort = 8080
$targetDir = $PSScriptRoot # ゲームディレクトリ (CCCaster_v10.exeを置く場所) または "C:\MBAACC\cccaster" などを指定

$filesToDownload = @("CCCaster_v10.exe", "libcccaster_hook.dll")

Write-Host "=== Downloading Artifacts from Host ===" -ForegroundColor Cyan
Write-Host "Target Directory: $targetDir"

foreach ($file in $filesToDownload) {
    $url = "http://$hostIp`:$httpPort/$file"
    $dest = Join-Path $targetDir $file
    
    Write-Host "Downloading $file from $url ..."
    try {
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
        Write-Host "  -> Success!" -ForegroundColor Green
    } catch {
        Write-Host "  -> Failed: $_" -ForegroundColor Red
        Write-Host "  Host側で deploy.ps1 が実行され、HTTPサーバーが起動しているか確認してください。" -ForegroundColor Yellow
    }
}

Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "完了しました。run_client.ps1 を実行してテストを開始できます。"
pause
