[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$pythonScript = Join-Path $PSScriptRoot 'Task06PIE.py'
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
$logPath = Join-Path $logDirectory 'Task06-PIE.log'

if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "UnrealEditor.exe was not found under $EngineRoot"
}
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null
if (Test-Path -LiteralPath $logPath) {
    Remove-Item -LiteralPath $logPath
}

$arguments = @(
    ('"{0}"' -f $projectFile),
    ('-ExecutePythonScript="{0}"' -f $pythonScript),
    '-MiniProbeExperienceFlow', '-MiniProbeLegacyExperience', '-unattended', '-nosplash', '-nosound', '-nop4',
    '-ddc=InstalledNoZenLocalFallback',
    ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
    ('"-abslog={0}"' -f $logPath)
)

$process = $null
try {
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 06 real PIE editor started: PID=$($process.Id), log=$logPath"
    $startedAt = [DateTime]::UtcNow
    while ($true) {
        $process.Refresh()
        if ($process.HasExited) { break }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Real PIE probe timed out after $TimeoutSeconds seconds. See $logPath"
        }
        Start-Sleep -Milliseconds 500
    }

    if ($process.ExitCode -ne 0) {
        throw "Real PIE editor exited with code $($process.ExitCode). See $logPath"
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "Real PIE editor did not create $logPath"
    }
    $logText = Get-Content -LiteralPath $logPath -Raw
    if (-not $logText.Contains('MINI_TASK06_PIE_SCRIPT_DONE Cycles=3') -or
        $logText.Contains('MINI_TASK06_PIE_SCRIPT_FAILED')) {
        throw "Real PIE Python driver did not finish all three sessions. See $logPath"
    }
    if ($logText.Contains('MiniFeatureMarker DUPLICATE') -or $logText.Contains('MiniFlowProbe FAIL:')) {
        throw "Real PIE probe reported a duplicate marker or failed Experience. See $logPath"
    }

    $markerTypes = @('MiniFeatureMarkerComponent', 'MiniExperienceActionMarkerComponent')
    for ($index = 1; $index -le 3; ++$index) {
        $beginToken = "MINI_TASK06_PIE_REQUEST_BEGIN Index=$index"
        $activeToken = "MINI_TASK06_PIE_ACTIVE Index=$index"
        $endRequestToken = "MINI_TASK06_PIE_REQUEST_END Index=$index"
        $endedToken = "MINI_TASK06_PIE_ENDED Index=$index"
        foreach ($token in @($beginToken, $activeToken, $endRequestToken, $endedToken)) {
            if ($logText.Split(@($token), [StringSplitOptions]::None).Length -ne 2) {
                throw "Expected exactly one '$token'. See $logPath"
            }
        }
        $begin = $logText.IndexOf($beginToken)
        $active = $logText.IndexOf($activeToken)
        $endRequest = $logText.IndexOf($endRequestToken)
        $ended = $logText.IndexOf($endedToken)
        if (-not ($begin -lt $active -and $active -lt $endRequest -and $endRequest -lt $ended)) {
            throw "PIE cycle $index has an invalid start/end order. See $logPath"
        }
        $playLog = $logText.Substring($begin, $endRequest - $begin)
        # Plugin deactivation can finish on the ticks following PIE's end.
        # Include the quiet gap, stopping before the next world's activation.
        $teardownEnd = if ($index -lt 3) {
            $logText.IndexOf("MINI_TASK06_PIE_REQUEST_BEGIN Index=$($index + 1)")
        } else {
            $logText.IndexOf('MINI_TASK06_PIE_SCRIPT_DONE Cycles=3')
        }
        if ($teardownEnd -le $ended) { throw "PIE cycle $index has no complete teardown boundary. See $logPath" }
        $teardownLog = $logText.Substring($endRequest, $teardownEnd - $endRequest)
        if ($playLog -notmatch 'Owner=/Game/Mini/Maps/UEDPIE_\d+_L_MiniPractice') {
            throw "PIE cycle $index did not create a UEDPIE world. See $logPath"
        }
        if (-not $playLog.Contains('MiniFlowProbe PASS: NetMode=Standalone ID=MiniExperienceDefinition:DA_MiniDiagnosticsExperience LateSubscriber=1')) {
            throw "PIE cycle $index did not load the Experience. See $logPath"
        }
        foreach ($type in $markerTypes) {
            $added = [regex]::Matches($playLog, "MiniFeatureMarker ADDED: Type=$type NetMode=Standalone .* Count=1").Count
            $removed = [regex]::Matches($teardownLog, "MiniFeatureMarker REMOVED: Type=$type NetMode=Standalone .* Count=0").Count
            if ($added -ne 1 -or $removed -ne 1) {
                throw "PIE cycle $index marker $type was added $added times and removed $removed times. See $logPath"
            }
        }
        if ($teardownLog -notmatch 'MiniAction Deactivated .*Action=/Game/Mini/Diagnostics/Experiences/DA_MiniDiagnosticsExperience.*MiniTask06_ExperienceAddComponents' -or
            $teardownLog -notmatch 'MiniFeature Release .*FinalUser=1 Activated=1' -or
            -not $teardownLog.Contains('MiniCuePath UNREGISTERED:')) {
            throw "PIE cycle $index did not release actions and feature lease. See $logPath"
        }
        Write-Host "PIE cycle ${index}: Loaded; two markers added once and removed once."
    }
    Write-Host "Task 06 real three-session PIE verification passed. Log: $logPath"
}
finally {
    if ($null -ne $process) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
                $process.WaitForExit(10000) | Out-Null
            }
        }
        catch [System.InvalidOperationException] {
            # This exact process exited after Refresh.
        }
    }
}
