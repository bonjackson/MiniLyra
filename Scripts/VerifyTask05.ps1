[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(10, 1800)][int]$TimeoutSeconds = 180,
    [ValidateRange(1024, 65535)][int]$Port = 18777
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
$experienceId = 'MiniExperienceDefinition:DA_MiniPracticeExperience'

if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "UnrealEditor-Cmd.exe was not found under $EngineRoot"
}
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw "Project file was not found: $projectFile"
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
    param(
        [System.Diagnostics.Process]$Process,
        [string]$Role,
        [string]$LogPath
    )
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited before the two-process probe completed (exit code $($Process.ExitCode)). See $LogPath"
    }
}

function Start-ProbeProcess {
    param(
        [string]$Scenario,
        [string]$Role,
        [string]$LogPath,
        [bool]$InvalidExperience
    )

    # Start-Process joins ArgumentList into one command line. Quote path arguments
    # explicitly so the script also works when the project lives under a spaced path.
    $url = if ($Role -eq 'Server') {
        '/Game/Mini/Maps/L_MiniPractice?listen'
    } else {
        "127.0.0.1:$Port"
    }
    $arguments = @(
        ('"{0}"' -f $projectFile),
        $url,
        '-game',
        '-MiniProbeExperienceFlow',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    )
    if ($Role -eq 'Server') {
        $arguments += "-port=$Port"
    }
    if ($InvalidExperience) {
        $arguments += '-MiniProbeInvalidExperience'
    }

    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 05 $Scenario $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Invoke-ProbeScenario {
    param(
        [string]$Scenario,
        [bool]$InvalidExperience
    )

    $serverLog = Join-Path $logDirectory "Task05-$Scenario-Server.log"
    $clientLog = Join-Path $logDirectory "Task05-$Scenario-Client.log"
    foreach ($path in @($serverLog, $clientLog)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path
        }
    }

    $server = $null
    $client = $null
    $startedAt = [DateTime]::UtcNow
    try {
        $server = Start-ProbeProcess -Scenario $Scenario -Role 'Server' `
            -LogPath $serverLog -InvalidExperience $InvalidExperience

        # The editor's startup can be slow on a cold DDC. Wait for the UDP listener
        # before asking the second process to connect; one timeout covers both phases.
        # UE prefixes each log line with a timestamp/frame, including LogNet.
        $listenerPattern = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
        while ($true) {
            Assert-Running -Process $server -Role "$Scenario server" -LogPath $serverLog
            if ((Read-LogText -Path $serverLog) -match $listenerPattern) {
                break
            }
            if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
                throw "$Scenario server did not start listening on port $Port within $TimeoutSeconds seconds. See $serverLog"
            }
            Start-Sleep -Milliseconds 250
        }

        $client = Start-ProbeProcess -Scenario $Scenario -Role 'Client' `
            -LogPath $clientLog -InvalidExperience $InvalidExperience

        $expectedServer = "MiniFlowProbe PASS: NetMode=ListenServer ID=$experienceId LateSubscriber=1"
        $expectedClient = "MiniFlowProbe PASS: NetMode=Client ID=$experienceId LateSubscriber=1"
        while ($true) {
            Assert-Running -Process $server -Role "$Scenario server" -LogPath $serverLog
            Assert-Running -Process $client -Role "$Scenario client" -LogPath $clientLog
            $serverText = Read-LogText -Path $serverLog
            $clientText = Read-LogText -Path $clientLog

            if ($InvalidExperience) {
                $serverPassed = [bool]($serverText -match '(?m)^.*MiniFlowProbe FAIL_EXPECTED: NetMode=ListenServer\b.*Unknown Experience ID.*$') -and
                    $serverText.Contains('LoadingAssets -> Failed Reason=Unknown Experience ID')
                $clientPassed = [bool]($clientText -match '(?m)^.*MiniFlowProbe FAIL_EXPECTED: NetMode=Client\b.*Unknown Experience ID.*$') -and
                    $clientText.Contains('Experience ID replicated NetMode=Client ID=MiniExperienceDefinition:DA_MiniDefinitelyMissing') -and
                    $clientText.Contains('LoadingAssets -> Failed Reason=Unknown Experience ID')
            } else {
                $serverPassed = $serverText.Contains($expectedServer) -and
                    $serverText.Contains("MiniGameMode selected map Experience $experienceId") -and
                    $serverText.Contains("ID=$experienceId ExecutingActions -> Loaded")
                $clientPassed = $clientText.Contains($expectedClient) -and
                    $clientText.Contains("Experience ID replicated NetMode=Client ID=$experienceId") -and
                    $clientText.Contains("ID=$experienceId ExecutingActions -> Loaded")
            }
            if ($serverPassed -and $clientPassed) {
                Write-Host "Task 05 $Scenario two-process probe passed. Server: $serverLog; Client: $clientLog"
                return
            }
            if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
                throw "$Scenario probe timed out after $TimeoutSeconds seconds (server marker=$serverPassed, client marker=$clientPassed). Logs: $serverLog; $clientLog"
            }
            Start-Sleep -Milliseconds 250
        }
    }
    finally {
        # Only stop the exact processes launched above; leave other UE sessions alone.
        foreach ($process in @($client, $server)) {
            if ($null -ne $process) {
                try {
                    $process.Refresh()
                    if (-not $process.HasExited) {
                        Stop-Process -Id $process.Id -Force -ErrorAction Stop
                        $process.WaitForExit(10000) | Out-Null
                    }
                }
                catch [System.InvalidOperationException] {
                    # It exited between Refresh and Stop-Process.
                }
            }
        }
    }
}

Invoke-ProbeScenario -Scenario 'Valid' -InvalidExperience $false
Invoke-ProbeScenario -Scenario 'Invalid' -InvalidExperience $true
Write-Host 'Task 05 two-process verification passed.'
