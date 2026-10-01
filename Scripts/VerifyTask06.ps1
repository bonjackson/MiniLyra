[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(30, 1800)][int]$TimeoutSeconds = 180,
    [ValidateRange(1024, 65535)][int]$Port = 18779
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
$experienceId = 'MiniExperienceDefinition:DA_MiniDiagnosticsExperience'
$missingId = 'MiniExperienceDefinition:DA_MiniMissingFeatureExperience'
$markerTypes = @('MiniFeatureMarkerComponent', 'MiniExperienceActionMarkerComponent')

if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "UnrealEditor-Cmd.exe was not found under $EngineRoot"
}
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null

function Read-LogText {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $content = Get-Content -LiteralPath $Path -Raw
        if ($null -ne $content) {
            return ,([string]$content)
        }
    }
    return ,([string]::Empty)
}

function Assert-Running {
    param([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath)
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited before verification completed (exit code $($Process.ExitCode)). See $LogPath"
    }
}

function Stop-ProbeProcesses {
    param([System.Diagnostics.Process[]]$Processes)
    foreach ($process in $Processes) {
        if ($null -eq $process) { continue }
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
                $process.WaitForExit(10000) | Out-Null
            }
        }
        catch [System.InvalidOperationException] {
            # The exact process exited after Refresh.
        }
    }
}

function Start-ProbeProcess {
    param([string]$Url, [string]$LogPath, [string[]]$ExtraArguments)
    if (Test-Path -LiteralPath $LogPath) {
        Remove-Item -LiteralPath $LogPath
    }
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeExperienceFlow',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 06 process started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-ValidWorldLog {
    param([string]$LogPath, [string]$NetMode)
    $logText = Read-LogText -Path $LogPath
    if (-not $logText.Contains("MiniFlowProbe PASS: NetMode=$NetMode ID=$experienceId LateSubscriber=1")) {
        throw "Task 06 $NetMode did not complete the Experience. See $LogPath"
    }
    if ($logText.Contains('MiniFeatureMarker DUPLICATE')) {
        throw "Task 06 $NetMode injected a duplicate marker. See $LogPath"
    }
    foreach ($type in $markerTypes) {
        $pattern = "MiniFeatureMarker ADDED: Type=$type NetMode=$NetMode .+ Count=1"
        $added = [regex]::Matches($logText, $pattern).Count
        if ($added -ne 1) {
            throw "Task 06 $NetMode injected $type $added times, expected one. See $LogPath"
        }
    }
    $resolved = [regex]::Matches($logText, "MiniFeature Resolved NetMode=$NetMode .* Name=MiniShooterCore ").Count
    $requested = [regex]::Matches($logText, "MiniFeature ActivationRequest NetMode=$NetMode ").Count
    if ($resolved -ne 1 -or $requested -ne 1) {
        throw "Task 06 $NetMode did not deduplicate MiniShooterCore (resolved=$resolved, requests=$requested). See $LogPath"
    }
}

# Task 05 keeps exercising the production default and invalid ID. Marker
# ownership is now in Diagnostics; verify it with its own real two processes.
& (Join-Path $PSScriptRoot 'VerifyTask05.ps1') -EngineRoot $EngineRoot `
    -TimeoutSeconds $TimeoutSeconds -Port $Port
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
    throw "Task 05 regression returned code $LASTEXITCODE"
}
$validServerLog = Join-Path $logDirectory 'Task06-Valid-Server.log'
$validClientLog = Join-Path $logDirectory 'Task06-Valid-Client.log'
$server = $null
$client = $null
$startedAt = [DateTime]::UtcNow
try {
    $server = Start-ProbeProcess -Url '/Game/Mini/Maps/L_MiniPractice?listen' `
        -LogPath $validServerLog -ExtraArguments @("-port=$Port", '-MiniProbeLegacyExperience')
    $listenerPattern = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
    while ($true) {
        Assert-Running -Process $server -Role 'diagnostics server' -LogPath $validServerLog
        if ((Read-LogText -Path $validServerLog) -match $listenerPattern) { break }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Diagnostics server failed to listen. See $validServerLog"
        }
        Start-Sleep -Milliseconds 250
    }
    $client = Start-ProbeProcess -Url "127.0.0.1:$Port" -LogPath $validClientLog `
        -ExtraArguments @('-MiniProbeLegacyExperience')
    while ($true) {
        Assert-Running -Process $server -Role 'diagnostics server' -LogPath $validServerLog
        Assert-Running -Process $client -Role 'diagnostics client' -LogPath $validClientLog
        $serverText = Read-LogText -Path $validServerLog
        $clientText = Read-LogText -Path $validClientLog
        if ($serverText.Contains("MiniFlowProbe PASS: NetMode=ListenServer ID=$experienceId LateSubscriber=1") -and
            $clientText.Contains("MiniFlowProbe PASS: NetMode=Client ID=$experienceId LateSubscriber=1")) { break }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Diagnostics activation timed out. Logs: $validServerLog; $validClientLog"
        }
        Start-Sleep -Milliseconds 250
    }
    Assert-ValidWorldLog -LogPath $validServerLog -NetMode 'ListenServer'
    Assert-ValidWorldLog -LogPath $validClientLog -NetMode 'Client'
}
finally {
    Stop-ProbeProcesses -Processes @($client, $server)
}
Write-Host 'Task 06 two-process activation and deduplication passed.'

# A scanned Experience that asks for a nonexistent required plugin must never
# become Loaded. Check failure separately on the listen server and client.
$serverLog = Join-Path $logDirectory 'Task06-MissingFeature-Server.log'
$clientLog = Join-Path $logDirectory 'Task06-MissingFeature-Client.log'
$server = $null
$client = $null
$startedAt = [DateTime]::UtcNow
try {
    $server = Start-ProbeProcess -Url '/Game/Mini/Maps/L_MiniPractice?listen' `
        -LogPath $serverLog -ExtraArguments @("-port=$Port", '-MiniProbeMissingGameFeature')
    $listenerPattern = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
    while ($true) {
        Assert-Running -Process $server -Role 'missing-feature server' -LogPath $serverLog
        if ((Read-LogText -Path $serverLog) -match $listenerPattern) { break }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Missing-feature server did not listen within $TimeoutSeconds seconds. See $serverLog"
        }
        Start-Sleep -Milliseconds 250
    }
    $client = Start-ProbeProcess -Url "127.0.0.1:$Port" -LogPath $clientLog `
        -ExtraArguments @('-MiniProbeMissingGameFeature')
    while ($true) {
        Assert-Running -Process $server -Role 'missing-feature server' -LogPath $serverLog
        Assert-Running -Process $client -Role 'missing-feature client' -LogPath $clientLog
        $serverText = Read-LogText -Path $serverLog
        $clientText = Read-LogText -Path $clientLog
        $serverPassed = $serverText.Contains("ID=$missingId LoadingFeatures -> Failed") -and
            $serverText.Contains("GameFeature plugin 'MiniDefinitelyMissing'") -and
            $serverText.Contains('MiniFlowProbe FAIL: NetMode=ListenServer')
        $clientPassed = $clientText.Contains("Experience ID replicated NetMode=Client ID=$missingId") -and
            $clientText.Contains("ID=$missingId LoadingFeatures -> Failed") -and
            $clientText.Contains("GameFeature plugin 'MiniDefinitelyMissing'") -and
            $clientText.Contains('MiniFlowProbe FAIL: NetMode=Client')
        if ($serverPassed -and $clientPassed) {
            if ($serverText.Contains(' -> Loaded') -or $clientText.Contains(' -> Loaded') -or
                $serverText.Contains('MiniFeatureMarker ADDED') -or $clientText.Contains('MiniFeatureMarker ADDED')) {
                throw "Missing-feature Experience advanced to Loaded or injected a marker. Logs: $serverLog; $clientLog"
            }
            Write-Host 'Task 06 required-plugin failure passed on both processes.'
            break
        }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Missing-feature probe timed out (server=$serverPassed, client=$clientPassed). Logs: $serverLog; $clientLog"
        }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    Stop-ProbeProcesses -Processes @($client, $server)
}

# Three world lifetimes in one process exercise scoped action removal and
# plugin release. This is a repeat-world smoke test, not an editor PIE test.
$cycleLog = Join-Path $logDirectory 'Task06-ThreeWorldCycles.log'
$cycleProcess = $null
$startedAt = [DateTime]::UtcNow
try {
    $cycleProcess = Start-ProbeProcess -Url '/Game/Mini/Maps/L_MiniPractice' `
        -LogPath $cycleLog -ExtraArguments @('-MiniProbeFeatureCycles=3')
    while ($true) {
        Assert-Running -Process $cycleProcess -Role 'three-world-cycle probe' -LogPath $cycleLog
        $logText = Read-LogText -Path $cycleLog
        if ($logText.Contains('MiniFeatureMarker DUPLICATE') -or $logText.Contains('MiniFlowProbe FAIL:')) {
            throw "Three-world-cycle probe reported a duplicate or failed Experience. See $cycleLog"
        }
        $cycles = [regex]::Matches($logText, 'MiniFeatureCycle LOADED: Index=\d+ Total=3').Count
        $released = [regex]::Matches($logText, 'MiniFeature Release NetMode=Standalone .* FinalUser=1 Activated=1').Count
        $actionsRemoved = [regex]::Matches($logText, 'MiniAction Deactivated .* Action=/Game/Mini/Diagnostics/Experiences/DA_MiniDiagnosticsExperience.*MiniTask06_ExperienceAddComponents').Count
        $cuePathsRemoved = [regex]::Matches($logText, 'MiniCuePath UNREGISTERED: .*MiniTask18_AddGameplayCuePath').Count
        $markersComplete = $true
        foreach ($type in $markerTypes) {
            $added = [regex]::Matches($logText, "MiniFeatureMarker ADDED: Type=$type NetMode=Standalone .* Count=1").Count
            $removed = [regex]::Matches($logText, "MiniFeatureMarker REMOVED: Type=$type NetMode=Standalone .* Count=0").Count
            if ($added -ne 3 -or $removed -ne 3) { $markersComplete = $false }
        }
        if ($cycles -eq 3 -and $released -eq 3 -and $actionsRemoved -eq 3 -and $cuePathsRemoved -eq 3 -and $markersComplete -and
            $logText.Contains('MiniFeatureCycle TRAVEL: Index=3 Final=1')) {
            Write-Host "Task 06 three-world activation/removal cycle passed. Log: $cycleLog"
            break
        }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Three-world-cycle probe timed out (cycles=$cycles, releases=$released, markersComplete=$markersComplete). See $cycleLog"
        }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    Stop-ProbeProcesses -Processes @($cycleProcess)
}

Write-Host 'Task 06 verification passed.'
