param([switch]$Check, [string]$TestRoot='')
$ErrorActionPreference = 'Stop'
if ($args -contains '--check') { $Check = $true }
$taskRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$taskNames = @('CCCaster_Steam.exe', 'CCCaster_Steam_GUI.exe', 'libcccaster_steam_hook.dll')
$taskReport = [ordered]@{ time = (Get-Date -Format o); check = [bool]$Check; stopped = @(); copied = @(); removed = @(); success = $false }
$taskLog = $null

function Assert-LocalPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Outside workspace: $full"
    }
    $cursor = $full
    while ($cursor -and $cursor -ne $taskRoot) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked path is not allowed: $cursor"
            }
        }
        $cursor = Split-Path -Parent $cursor
    }
}

function Get-LocalFiles([string]$Directory) {
    Assert-LocalPath $Directory
    foreach ($item in Get-ChildItem -LiteralPath $Directory -Force) {
        Assert-LocalPath $item.FullName
        if ($item.PSIsContainer) { Get-LocalFiles $item.FullName }
        else { $item }
    }
}

function Get-TargetProcesses {
    @(Get-CimInstance Win32_Process | Where-Object {
        $executable = $_.ExecutablePath
        $executable -and @($taskTargets | Where-Object {
            $executable.StartsWith($_ + '\', [StringComparison]::OrdinalIgnoreCase)
        }).Count -gt 0
    })
}

try {
    # Complete all source and destination checks before stopping any process.
    $taskSources = @{}
    foreach ($name in $taskNames) {
        $source = Join-Path $taskRoot "build/bin/$name"
        Assert-LocalPath $source
        $bytes = [IO.File]::ReadAllBytes($source)
        if ($bytes.Length -lt 64 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) { throw "Invalid executable: $source" }
        $pe = [BitConverter]::ToInt32($bytes, 60)
        if ($pe -lt 0 -or $pe -gt ($bytes.Length - 6) -or
            [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
            [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x14c) { throw "Not a 32bit PE: $source" }
        $taskSources[$name] = @{ path = $source; hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash }
    }
    $taskRuntimeSource = Join-Path $taskRoot 'build/bin/gui-licenses'
    Assert-LocalPath $taskRuntimeSource
    $taskRuntimeManifest = Get-Content -LiteralPath (Join-Path $taskRuntimeSource 'files.sha256.json') -Raw | ConvertFrom-Json
    foreach ($entry in $taskRuntimeManifest.files) {
        $path = [IO.Path]::GetFullPath((Join-Path $taskRuntimeSource $entry.path))
        Assert-LocalPath $path
        if (!$path.StartsWith($taskRuntimeSource + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid runtime manifest path' }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Runtime source mismatch: $path" }
    }
    $taskAssetSource = Join-Path $taskRoot 'build/bin/game-assets'
    Assert-LocalPath $taskAssetSource
    $taskAssetManifest = Get-Content -LiteralPath (Join-Path $taskAssetSource 'files.sha256.json') -Raw | ConvertFrom-Json
    if (@($taskAssetManifest.files).Count -ne 9) { throw 'Expected 9 stage select images' }
    $taskAssetPaths = @()
    foreach ($entry in $taskAssetManifest.files) {
        if ($entry.path -notmatch '^GRP/BgSelect/stsel_(view|jp|en)/chr_stsel_\1(55|57|58)\.dds$' -or
            $taskAssetPaths -contains $entry.path) { throw 'Invalid stage select manifest path' }
        $taskAssetPaths += $entry.path
        $path = Join-Path $taskAssetSource $entry.path
        Assert-LocalPath $path
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Stage image source mismatch: $path" }
    }
    $taskRuntime = if($TestRoot){[IO.Path]::GetFullPath($TestRoot)}else{Join-Path $taskRoot 'test/runtime'}
    Assert-LocalPath $taskRuntime
    $taskTargets = @(1..3 | ForEach-Object { Join-Path $taskRuntime "MBAACC_$_" })
    $taskReport.test_root = $taskRuntime
    $taskOld = @()
    foreach ($target in $taskTargets) {
        Assert-LocalPath $target
        if (!(Test-Path -LiteralPath (Join-Path $target 'MBAA.exe') -PathType Leaf)) { throw "Game missing: $target" }
        $destination = Join-Path $target 'cccaster_st'
        Assert-LocalPath $destination
        if (!(Test-Path -LiteralPath $destination -PathType Container)) { throw "Destination missing: $destination" }
        foreach ($entry in $taskAssetManifest.files) {
            $path = Join-Path $target $entry.path
            Assert-LocalPath $path
            # 同名の独自画像があるコピーは上書きせず、プロセス停止前に検出する。
            if ((Test-Path -LiteralPath $path) -and
                (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
                throw "Existing custom stage image preserved: $path"
            }
        }
        foreach ($file in Get-LocalFiles $target) {
            # Only CCCaster executables/DLLs and their suffix backups; never INI backups.
            if ($file.Name -match '(?i)cccaster.*\.(exe|dll)($|\.)' -and
                !($file.DirectoryName -eq $destination -and $taskNames -contains $file.Name)) {
                $taskOld += $file.FullName
            }
        }
    }
    $taskProcesses = @(Get-TargetProcesses)
    Write-Host 'Source: ' (Join-Path $taskRoot 'build/bin')
    Write-Host "Targets: $taskRuntime (MBAACC_1, MBAACC_2, MBAACC_3)"
    $taskProcesses | ForEach-Object { Write-Host "STOP PID $($_.ProcessId): $($_.ExecutablePath)" }
    $taskOld | ForEach-Object { Write-Host "REMOVE: $_" }
    if ($Check) {
        Write-Host 'Preflight passed. No processes stopped and no files changed.'
        exit 0
    }
    $logDir = Join-Path $taskRoot 'test/logs'
    Assert-LocalPath $logDir
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $taskLog = Join-Path $logDir ('deploy_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '.json')
    foreach ($process in $taskProcesses) {
        # Re-query PID and path to avoid acting on a recycled PID.
        $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.ProcessId)"
        if (!$current) { continue }
        if ($current.ExecutablePath -ne $process.ExecutablePath) { throw 'Process identity changed; rerun deployment.' }
        Stop-Process -Id $current.ProcessId -Force
        $taskReport.stopped += @{ pid = $current.ProcessId; path = $current.ExecutablePath }
        if (Get-Process -Id $current.ProcessId -ErrorAction SilentlyContinue) {
            Wait-Process -Id $current.ProcessId -Timeout 10
        }
    }
    if (@(Get-TargetProcesses).Count) { throw 'A target process is still running; rerun deployment.' }
    foreach ($target in $taskTargets) {
        foreach ($name in $taskNames) {
            $destination = Join-Path $target "cccaster_st/$name"
            Assert-LocalPath $destination
            Copy-Item -LiteralPath $taskSources[$name].path -Destination $destination -Force
            $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            if ($hash -ne $taskSources[$name].hash) { throw "Hash mismatch: $destination" }
            $taskReport.copied += @{ path = $destination; sha256 = $hash }
        }
        $runtimeDestination = [IO.Path]::GetFullPath((Join-Path $target 'cccaster_st/gui-licenses'))
        Assert-LocalPath $runtimeDestination
        if (!$runtimeDestination.StartsWith($target + '\cccaster_st\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid runtime destination' }
        if (Test-Path -LiteralPath $runtimeDestination) {
            # 専用のライセンス添付フォルダーだけを置換する。
            @(Get-LocalFiles $runtimeDestination) | Out-Null
            Remove-Item -LiteralPath $runtimeDestination -Recurse -Force
        }
        # WebView2専用フォルダーを残さない。対象内のゲーム・設定は保持する。
        $obsoleteRuntime = [IO.Path]::GetFullPath((Join-Path $target 'cccaster_st/gui-runtime'))
        Assert-LocalPath $obsoleteRuntime
        if (!$obsoleteRuntime.StartsWith($target + '\cccaster_st\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid obsolete runtime path' }
        if (Test-Path -LiteralPath $obsoleteRuntime) {
            @(Get-LocalFiles $obsoleteRuntime) | Out-Null
            Remove-Item -LiteralPath $obsoleteRuntime -Recurse -Force
            $taskReport.removed += $obsoleteRuntime
        }
        Copy-Item -LiteralPath $taskRuntimeSource -Destination $runtimeDestination -Recurse -Force
        foreach ($entry in $taskRuntimeManifest.files) {
            $path = Join-Path $runtimeDestination $entry.path
            if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Runtime destination mismatch: $path" }
        }
        $taskReport.copied += @{ path = $runtimeDestination; version = $taskRuntimeManifest.version; verifiedFiles = @($taskRuntimeManifest.files).Count }
        foreach ($entry in $taskAssetManifest.files) {
            $path = Join-Path $target $entry.path
            Assert-LocalPath $path
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            if (!(Test-Path -LiteralPath $path)) {
                Copy-Item -LiteralPath (Join-Path $taskAssetSource $entry.path) -Destination $path
            }
            $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
            if ($hash -ne $entry.sha256) { throw "Stage image destination mismatch: $path" }
            $taskReport.copied += @{ path = $path; sha256 = $hash }
        }
    }
    foreach ($old in $taskOld) {
        Assert-LocalPath $old
        Remove-Item -LiteralPath $old -Force
        $taskReport.removed += $old
    }
    foreach ($target in $taskTargets) {
        $binaries = @(Get-LocalFiles $target | Where-Object { $_.Name -match '(?i)cccaster.*\.(exe|dll)($|\.)' })
        if ($binaries.Count -ne 3) { throw "Unexpected remaining CCCaster binaries: $target" }
        foreach ($binary in $binaries) {
            if ((Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash -ne $taskSources[$binary.Name].hash) {
                throw "Final hash mismatch: $($binary.FullName)"
            }
        }
    }
    $taskReport.success = $true
    Write-Host 'Deployment complete: all 9 binaries, 27 stage images and GUI licenses verified; old CCCaster binaries removed.'
} catch {
    $taskReport.error = $_.Exception.Message
    Write-Host ('Deployment failed: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if ($taskLog) {
        $taskReport | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskLog -Encoding UTF8
        Write-Host "Log: $taskLog"
    }
}
