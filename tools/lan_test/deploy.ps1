<#
.SYNOPSIS
 ビルド成果物 (CCCaster_v10.exe, libcccaster_hook.dll) を開発機に配置し、
 その後、一時的な HTTP サーバーを立ち上げて VM 側からダウンロードできるようにします。
#>

$config = Get-Content -Raw -Path "$PSScriptRoot\config.json" | ConvertFrom-Json

$buildDir = $config.host_pc.build_dir
$hostGameDir = $config.host_pc.game_dir
$filesToCopy = @("CCCaster_v10.exe", "libcccaster_hook.dll")
$httpPort = 8080

Write-Host "=== Deploying Build Artifacts ===" -ForegroundColor Cyan

# ホストPCのゲームフォルダへコピー
if (Test-Path $hostGameDir) {
    Write-Host "Deploying to Host Directory: $hostGameDir"
    foreach ($file in $filesToCopy) {
        $src = Join-Path $buildDir $file
        $dest = Join-Path $hostGameDir $file
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination $dest -Force
            Write-Host "  -> Copied $file" -ForegroundColor Green
        } else {
            Write-Host "  -> Source missing: $src" -ForegroundColor Red
        }
    }
} else {
    Write-Host "Host directory not found: $hostGameDir" -ForegroundColor Yellow
}

Write-Host "`n=== Starting Local HTTP Server for VM Download ===" -ForegroundColor Cyan
Write-Host "VM側で download_client.ps1 を実行してファイルを取得してください。" -ForegroundColor Yellow

# 一時的なHTTPサーバーを $buildDir で起動 (Python 3が必要)
Push-Location $buildDir
try {
    Write-Host "HTTP Server running at http://0.0.0.0:$httpPort/ (Press Ctrl+C to stop after VM download is complete)"
    python -m http.server $httpPort
} catch {
    Write-Host "Failed to start Python HTTP server. Make sure Python is installed." -ForegroundColor Red
}
Pop-Location

Write-Host "===================================" -ForegroundColor Cyan
