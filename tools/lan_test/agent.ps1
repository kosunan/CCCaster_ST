<#
.SYNOPSIS
 VM側に常駐してホストからの命令を待つ HTTP エージェント
#>

$port = 8081
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://+:$port/")
$listener.Start()

$targetDir = $PSScriptRoot # スクリプトと同じ場所（MBAA.exe\cccaster内を想定）

Write-Host "=== CCCaster Test Agent ===" -ForegroundColor Cyan
Write-Host "Listening on port $port..."
Write-Host "Target Directory: $targetDir"
Write-Host "Press Ctrl+C to stop the agent."

try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response
        $method = $request.HttpMethod
        $url = $request.Url.LocalPath
        
        Write-Host "[$method] $url"
        $status = 200
        $responseText = "OK"

        try {
            if ($url -eq "/ping") {
                $responseText = "Agent is running"
            }
            elseif ($url -eq "/stop" -and $method -eq "POST") {
                Write-Host "  -> Killing processes..." -ForegroundColor Yellow
                Stop-Process -Name "CCCaster_v10" -Force -ErrorAction SilentlyContinue
                Stop-Process -Name "MBAA" -Force -ErrorAction SilentlyContinue
                $responseText = "Stopped"
            }
            elseif ($url -eq "/deploy" -and $method -eq "POST") {
                # JSON: { "filename": "...", "data": "<base64>" }
                $reader = New-Object System.IO.StreamReader($request.InputStream, $request.ContentEncoding)
                $body = $reader.ReadToEnd()
                $json = $body | ConvertFrom-Json
                
                $filename = $json.filename
                $base64Data = $json.data
                
                if ([string]::IsNullOrEmpty($filename) -or [string]::IsNullOrEmpty($base64Data)) {
                    $status = 400
                    $responseText = "Invalid payload"
                } else {
                    $bytes = [Convert]::FromBase64String($base64Data)
                    $destPath = Join-Path $targetDir $filename
                    [System.IO.File]::WriteAllBytes($destPath, $bytes)
                    Write-Host "  -> Saved $filename ($($bytes.Length) bytes)" -ForegroundColor Green
                    $responseText = "Deployed $filename"
                }
            }
            elseif ($url -eq "/run" -and $method -eq "POST") {
                # JSON: { "hostIp": "...", "port": 10800 }
                $reader = New-Object System.IO.StreamReader($request.InputStream, $request.ContentEncoding)
                $body = $reader.ReadToEnd()
                $json = $body | ConvertFrom-Json
                
                $hostIp = $json.hostIp
                $hostPort = $json.port
                
                if ([string]::IsNullOrEmpty($hostIp) -or (-not $hostPort)) {
                    $status = 400
                    $responseText = "Missing hostIp or port"
                } else {
                    $cmdArgs = "--headless --ip $hostIp --port $hostPort"
                    Write-Host "  -> Starting game: CCCaster_v10.exe $cmdArgs" -ForegroundColor Green
                    
                    # バックグラウンドの別プロセスとして完全に切り離して起動する
                    $si = New-Object System.Diagnostics.ProcessStartInfo
                    $si.FileName = "cmd.exe"
                    $si.Arguments = "/c CCCaster_v10.exe $cmdArgs"
                    $si.WorkingDirectory = $targetDir
                    $si.UseShellExecute = $true
                    $si.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Minimized
                    [System.Diagnostics.Process]::Start($si) | Out-Null
                    Pop-Location
                    
                    $responseText = "Started"
                }
            }
            else {
                $status = 404
                $responseText = "Not Found"
            }
        } catch {
            $status = 500
            $responseText = $_.Exception.Message
            Write-Host "  -> Error: $responseText" -ForegroundColor Red
        }

        $buffer = [System.Text.Encoding]::UTF8.GetBytes($responseText)
        $response.StatusCode = $status
        $response.ContentType = "text/plain"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
    }
} finally {
    $listener.Stop()
}
