[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18781
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
$experienceId = 'MiniExperienceDefinition:DA_MiniPracticeExperience'
$invalidExperienceId = 'MiniExperienceDefinition:DA_MiniDefinitelyMissing'

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
            # The exact process exited between Refresh and Stop-Process.
        }
    }
}

function Start-ProbeProcess {
    param(
        [string]$Url,
        [string]$Role,
        [string]$LogPath,
        [string[]]$ExtraArguments
    )
    if (Test-Path -LiteralPath $LogPath) {
        Remove-Item -LiteralPath $LogPath
    }

    # Start-Process joins ArgumentList into one command line. Quote paths with
    # spaces explicitly, as in the existing two-process Experience verifier.
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game',
        '-MiniProbeExperienceFlow', '-MiniProbePlayerSpawns',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments

    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 07 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Wait-ForListener {
    param([System.Diagnostics.Process]$Process, [string]$LogPath, [string]$Role)
    $startedAt = [DateTime]::UtcNow
    $listenerPattern = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
    while ($true) {
        Assert-Running -Process $Process -Role $Role -LogPath $LogPath
        if ((Read-LogText -Path $LogPath) -match $listenerPattern) { return }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "$Role did not listen on port $Port within $TimeoutSeconds seconds. See $LogPath"
        }
        Start-Sleep -Milliseconds 250
    }
}

function Assert-NoDuplicateMarker {
    param([string]$LogText, [string]$LogPath)
    if ($LogText -match '(?im)^.*Mini[^\r\n]*DUPLICATE[^\r\n]*$') {
        throw "A Mini component was added twice. See $LogPath"
    }
}

function Test-CompleteSnapshot {
    param([string]$LogText, [string]$NetMode, [int]$Players)
    $expected = "MiniSpawnProbe SNAPSHOT: NetMode=$NetMode PlayerStates=$Players " +
        "Characters=$Players ValidCharacters=$Players LocalPawn=1 CharacterMarkers=$Players"
    return $LogText.Contains($expected)
}

function Assert-ServerSpawnOrder {
    param([string]$LogText, [string]$LogPath, [int]$ExpectedCommits)
    $loaded = "ID=$experienceId ExecutingActions -> Loaded"
    $loadedIndex = $LogText.IndexOf($loaded, [StringComparison]::Ordinal)
    if ($loadedIndex -lt 0) {
        throw "The server did not load $experienceId. See $LogPath"
    }

    $commits = [regex]::Matches($LogText, 'MiniSpawn COMMITTED:')
    if ($commits.Count -ne $ExpectedCommits) {
        throw "Expected $ExpectedCommits committed server spawns, found $($commits.Count). See $LogPath"
    }
    foreach ($commit in $commits) {
        if ($commit.Index -le $loadedIndex) {
            throw "A character spawned before the server Experience was Loaded. See $LogPath"
        }
    }

    $assigned = [regex]::Matches($LogText, 'MiniSpawn PawnDataAssigned:').Count
    if ($assigned -lt $ExpectedCommits) {
        throw "Only $assigned PawnData assignments were logged for $ExpectedCommits spawns. See $LogPath"
    }
}

function Assert-NoSpawnOnFailure {
    param([string]$LogText, [string]$LogPath)
    if ($LogText.Contains('MiniSpawn COMMITTED:') -or
        $LogText -match 'MiniSpawnProbe SNAPSHOT: [^\r\n]*Characters=[1-9]' -or
        $LogText -match 'MiniSpawnProbe SNAPSHOT: [^\r\n]*CharacterMarkers=[1-9]') {
        throw "A failed Experience created a Mini character. See $LogPath"
    }
}

# First complete a two-player match, then connect another process after both
# existing peers have observed the two characters. This is a real late join.
$validServerLog = Join-Path $logDirectory 'Task07-Valid-Server.log'
$validClientALog = Join-Path $logDirectory 'Task07-Valid-ClientA.log'
$validClientBLog = Join-Path $logDirectory 'Task07-Valid-ClientB.log'
$server = $null
$clientA = $null
$clientB = $null
try {
    $server = Start-ProbeProcess -Url '/Game/Mini/Maps/L_MiniPractice?listen' `
        -Role 'valid server' -LogPath $validServerLog -ExtraArguments @("-port=$Port")
    Wait-ForListener -Process $server -LogPath $validServerLog -Role 'valid server'

    $clientA = Start-ProbeProcess -Url "127.0.0.1:$Port" `
        -Role 'valid ClientA' -LogPath $validClientALog -ExtraArguments @()
    $startedAt = [DateTime]::UtcNow
    while ($true) {
        Assert-Running -Process $server -Role 'valid server' -LogPath $validServerLog
        Assert-Running -Process $clientA -Role 'valid ClientA' -LogPath $validClientALog
        $serverText = Read-LogText -Path $validServerLog
        $clientAText = Read-LogText -Path $validClientALog
        Assert-NoDuplicateMarker -LogText $serverText -LogPath $validServerLog
        Assert-NoDuplicateMarker -LogText $clientAText -LogPath $validClientALog
        if ((Test-CompleteSnapshot -LogText $serverText -NetMode 'ListenServer' -Players 2) -and
            (Test-CompleteSnapshot -LogText $clientAText -NetMode 'Client' -Players 2)) {
            Assert-ServerSpawnOrder -LogText $serverText -LogPath $validServerLog -ExpectedCommits 2
            Write-Host 'Task 07 initial two-player spawn and replication passed.'
            break
        }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Two-player snapshot timed out. Logs: $validServerLog; $validClientALog"
        }
        Start-Sleep -Milliseconds 250
    }

    $clientB = Start-ProbeProcess -Url "127.0.0.1:$Port" `
        -Role 'late ClientB' -LogPath $validClientBLog -ExtraArguments @()
    $startedAt = [DateTime]::UtcNow
    while ($true) {
        Assert-Running -Process $server -Role 'valid server' -LogPath $validServerLog
        Assert-Running -Process $clientA -Role 'valid ClientA' -LogPath $validClientALog
        Assert-Running -Process $clientB -Role 'late ClientB' -LogPath $validClientBLog
        $serverText = Read-LogText -Path $validServerLog
        $clientAText = Read-LogText -Path $validClientALog
        $clientBText = Read-LogText -Path $validClientBLog
        Assert-NoDuplicateMarker -LogText $serverText -LogPath $validServerLog
        Assert-NoDuplicateMarker -LogText $clientAText -LogPath $validClientALog
        Assert-NoDuplicateMarker -LogText $clientBText -LogPath $validClientBLog
        if ((Test-CompleteSnapshot -LogText $serverText -NetMode 'ListenServer' -Players 3) -and
            (Test-CompleteSnapshot -LogText $clientAText -NetMode 'Client' -Players 3) -and
            (Test-CompleteSnapshot -LogText $clientBText -NetMode 'Client' -Players 3)) {
            Assert-ServerSpawnOrder -LogText $serverText -LogPath $validServerLog -ExpectedCommits 3
            Write-Host 'Task 07 late join and three-player replication passed.'
            break
        }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Three-player late-join snapshot timed out. Logs: $validServerLog; $validClientALog; $validClientBLog"
        }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    Stop-ProbeProcesses -Processes @($clientB, $clientA, $server)
}

# An invalid required Experience reaches Failed on both peers. A client may
# still connect and receive the failure, but neither peer may spawn a character.
$failedServerLog = Join-Path $logDirectory 'Task07-InvalidExperience-Server.log'
$failedClientLog = Join-Path $logDirectory 'Task07-InvalidExperience-Client.log'
$server = $null
$client = $null
try {
    $server = Start-ProbeProcess -Url '/Game/Mini/Maps/L_MiniPractice?listen' `
        -Role 'invalid-Experience server' -LogPath $failedServerLog `
        -ExtraArguments @("-port=$Port", '-MiniProbeInvalidExperience')
    Wait-ForListener -Process $server -LogPath $failedServerLog -Role 'invalid-Experience server'

    $client = Start-ProbeProcess -Url "127.0.0.1:$Port" `
        -Role 'invalid-Experience client' -LogPath $failedClientLog `
        -ExtraArguments @('-MiniProbeInvalidExperience')
    $startedAt = [DateTime]::UtcNow
    while ($true) {
        Assert-Running -Process $server -Role 'invalid-Experience server' -LogPath $failedServerLog
        Assert-Running -Process $client -Role 'invalid-Experience client' -LogPath $failedClientLog
        $serverText = Read-LogText -Path $failedServerLog
        $clientText = Read-LogText -Path $failedClientLog
        Assert-NoSpawnOnFailure -LogText $serverText -LogPath $failedServerLog
        Assert-NoSpawnOnFailure -LogText $clientText -LogPath $failedClientLog
        $serverFailed = $serverText.Contains("ID=$invalidExperienceId LoadingAssets -> Failed") -and
            $serverText.Contains('MiniFlowProbe FAIL_EXPECTED: NetMode=ListenServer')
        $clientFailed = $clientText.Contains("Experience ID replicated NetMode=Client ID=$invalidExperienceId") -and
            $clientText.Contains("ID=$invalidExperienceId LoadingAssets -> Failed") -and
            $clientText.Contains('MiniFlowProbe FAIL_EXPECTED: NetMode=Client')
        $serverZero = $serverText -match 'MiniSpawnProbe SNAPSHOT: NetMode=ListenServer [^\r\n]*Characters=0 ValidCharacters=0 [^\r\n]*CharacterMarkers=0'
        $clientZero = $clientText -match 'MiniSpawnProbe SNAPSHOT: NetMode=Client [^\r\n]*Characters=0 ValidCharacters=0 [^\r\n]*CharacterMarkers=0'
        if ($serverFailed -and $clientFailed -and $serverZero -and $clientZero) {
            Write-Host 'Task 07 invalid-Experience no-spawn probe passed on both processes.'
            break
        }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Invalid-Experience no-spawn probe timed out (serverFailed=$serverFailed, clientFailed=$clientFailed, serverZero=$serverZero, clientZero=$clientZero). Logs: $failedServerLog; $failedClientLog"
        }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    Stop-ProbeProcesses -Processes @($client, $server)
}

Write-Host 'Task 07 verification passed.'
