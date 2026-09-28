[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18782
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'

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
            # The process exited between Refresh and Stop-Process.
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
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath
    }

    # Start-Process joins ArgumentList into one command line; quote paths with spaces.
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game',
        '-MiniProbePlayerSpawns', '-MiniProbeInitStates',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments

    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 08 $Role started: PID=$($process.Id), log=$LogPath"
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

function Assert-NoProbeFailure {
    param([string]$LogText, [string]$LogPath)
    if ($LogText.Contains('MiniInitProbe ORDER_FAIL:') -or
        $LogText -match '(?im)^.*Mini[^\r\n]*DUPLICATE[^\r\n]*$') {
        throw "An initialization order or duplicate-component probe failed. See $LogPath"
    }
    if ($LogText.Contains('To=InitState.GameplayReady')) {
        throw "A component entered GameplayReady before Tasks 09 and 10. See $LogPath"
    }
}

function Test-SpawnSnapshot {
    param([string]$LogText, [string]$NetMode, [int]$Players)
    $expected = "MiniSpawnProbe SNAPSHOT: NetMode=$NetMode PlayerStates=$Players " +
        "Characters=$Players ValidCharacters=$Players LocalPawn=1 CharacterMarkers=$Players"
    return $LogText.Contains($expected)
}

function Test-InitSnapshot {
    param(
        [string]$LogText,
        [string]$NetMode,
        [int]$Players,
        [int]$Simulated,
        [int]$ProbeReleased
    )
    $expected = "MiniInitProbe SNAPSHOT: NetMode=$NetMode PlayerStates=$Players Characters=$Players " +
        "ExtensionDataInitialized=$Players HeroDataInitialized=$Players " +
        "SimulatedDataInitialized=$Simulated SimulatedWithoutLocalInputInitialized=$Simulated " +
        "GameplayReady=0 ProbeReleased=$ProbeReleased RepeatNotified=$Players"
    return $LogText.Contains($expected)
}

function Assert-Transitions {
    param([string]$LogText, [string]$LogPath, [int]$Players)
    $pattern = 'MiniInitState TRANSITION: Feature=(PawnExtension|Hero) Role=\S+ Pawn=(\S+) From=\S* To=(InitState\.\w+)'
    $transitions = [regex]::Matches($LogText, $pattern)
    $expectedStates = @('InitState.Spawned', 'InitState.DataAvailable', 'InitState.DataInitialized')
    $counts = @{}
    $pawns = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)

    foreach ($transition in $transitions) {
        $feature = $transition.Groups[1].Value
        $pawn = $transition.Groups[2].Value
        $state = $transition.Groups[3].Value
        if ($expectedStates -cnotcontains $state) {
            throw "Unexpected $feature transition to $state on $pawn. See $LogPath"
        }
        [void]$pawns.Add($pawn)
        $key = "$feature|$pawn|$state"
        if (-not $counts.ContainsKey($key)) { $counts[$key] = 0 }
        $counts[$key]++
    }

    if ($pawns.Count -ne $Players) {
        throw "Expected transitions for $Players pawns, found $($pawns.Count). See $LogPath"
    }
    foreach ($pawn in $pawns) {
        foreach ($feature in @('PawnExtension', 'Hero')) {
            foreach ($state in $expectedStates) {
                $key = "$feature|$pawn|$state"
                if (-not $counts.ContainsKey($key) -or $counts[$key] -ne 1) {
                    $found = if ($counts.ContainsKey($key)) { $counts[$key] } else { 0 }
                    throw "Expected exactly one $feature $state transition for $pawn, found $found. See $LogPath"
                }
            }
		$deferredPattern = "MiniInitState DEFERRED: Feature=$feature Pawn=$([regex]::Escape($pawn)) Target=InitState\.GameplayReady WaitingFor=\S+"
		if ([regex]::Matches($LogText, $deferredPattern).Count -ne 1) {
			throw "Expected exactly one deferred GameplayReady record for $feature on $pawn. See $LogPath"
		}
        }
    }
}

function Assert-ReleaseOrder {
    param([string]$LogText, [string]$LogPath, [string]$ExpectedOrder, [int]$Players)
    $pattern = 'MiniInitProbe (FIRST_RELEASE|SECOND_RELEASE): Order=(DataFirst|PlayerStateFirst) Pawn=(\S+)'
    $events = [regex]::Matches($LogText, $pattern)
    $first = @{}
    $second = @{}
    $actualReleases = @{}

    foreach ($event in $events) {
        $kind = $event.Groups[1].Value
        $order = $event.Groups[2].Value
        $pawn = $event.Groups[3].Value
        if ($order -cne $ExpectedOrder) {
            throw "Unexpected release order '$order' for $pawn; expected $ExpectedOrder. See $LogPath"
        }
        $destination = if ($kind -eq 'FIRST_RELEASE') { $first } else { $second }
        if ($destination.ContainsKey($pawn)) {
            throw "Repeated $kind for $pawn. See $LogPath"
        }
        $destination[$pawn] = $event.Index
    }

    $releasePattern = 'MiniInitProbe RELEASE: Dependency=(PawnData|PlayerState) Pawn=(\S+)'
    foreach ($release in [regex]::Matches($LogText, $releasePattern)) {
        $pawn = $release.Groups[2].Value
        if (-not $actualReleases.ContainsKey($pawn)) { $actualReleases[$pawn] = @() }
        $actualReleases[$pawn] += [pscustomobject]@{
            Dependency = $release.Groups[1].Value
            Index = $release.Index
        }
    }

    if ($first.Count -ne $Players -or $second.Count -ne $Players) {
        throw "Expected $Players FIRST_RELEASE and SECOND_RELEASE events; found $($first.Count) and $($second.Count). See $LogPath"
    }
    foreach ($pawn in $first.Keys) {
        if (-not $second.ContainsKey($pawn) -or $first[$pawn] -ge $second[$pawn]) {
            throw "FIRST_RELEASE did not precede SECOND_RELEASE for $pawn. See $LogPath"
        }
        $releases = $actualReleases[$pawn]
        $expectedFirst = if ($ExpectedOrder -eq 'DataFirst') { 'PawnData' } else { 'PlayerState' }
        $expectedSecond = if ($ExpectedOrder -eq 'DataFirst') { 'PlayerState' } else { 'PawnData' }
        if ($releases.Count -ne 2 -or
            $releases[0].Dependency -cne $expectedFirst -or
            $releases[1].Dependency -cne $expectedSecond -or
            $releases[0].Index -ge $first[$pawn] -or
            $first[$pawn] -ge $releases[1].Index -or
            $releases[1].Index -ge $second[$pawn]) {
            throw "Actual dependency release order is wrong for $pawn. See $LogPath"
        }
        $escapedPawn = [regex]::Escape($pawn)
        foreach ($feature in @('PawnExtension', 'Hero')) {
            $transitionPattern = "MiniInitState TRANSITION: Feature=$feature Role=\S+ Pawn=$escapedPawn From=\S* To=InitState\.DataAvailable"
            $dataAvailable = [regex]::Matches($LogText, $transitionPattern)
            if ($dataAvailable.Count -ne 1 -or $dataAvailable[0].Index -le $releases[1].Index) {
                throw "$feature advanced before both dependencies were released for $pawn. See $LogPath"
            }
        }
    }
}

$serverLog = Join-Path $logDirectory 'Task08-Server.log'
$clientALog = Join-Path $logDirectory 'Task08-ClientA.log'
$clientBLog = Join-Path $logDirectory 'Task08-ClientB.log'
$server = $null
$clientA = $null
$clientB = $null
try {
    $server = Start-ProbeProcess -Url '/Game/Mini/Maps/L_MiniPractice?listen' `
        -Role 'listen server' -LogPath $serverLog -ExtraArguments @("-port=$Port")
    Wait-ForListener -Process $server -LogPath $serverLog -Role 'listen server'

    $clientA = Start-ProbeProcess -Url "127.0.0.1:$Port" `
        -Role 'ClientA (DataFirst)' -LogPath $clientALog `
        -ExtraArguments @('-MiniProbeInitOrder=DataFirst')
    $startedAt = [DateTime]::UtcNow
    while ($true) {
        Assert-Running -Process $server -Role 'listen server' -LogPath $serverLog
        Assert-Running -Process $clientA -Role 'ClientA' -LogPath $clientALog
        $serverText = Read-LogText -Path $serverLog
        $clientAText = Read-LogText -Path $clientALog
        Assert-NoProbeFailure -LogText $serverText -LogPath $serverLog
        Assert-NoProbeFailure -LogText $clientAText -LogPath $clientALog
        if ((Test-SpawnSnapshot -LogText $serverText -NetMode 'ListenServer' -Players 2) -and
            (Test-SpawnSnapshot -LogText $clientAText -NetMode 'Client' -Players 2) -and
            (Test-InitSnapshot -LogText $serverText -NetMode 'ListenServer' -Players 2 -Simulated 0 -ProbeReleased 0) -and
            (Test-InitSnapshot -LogText $clientAText -NetMode 'Client' -Players 2 -Simulated 1 -ProbeReleased 2)) {
            Write-Host 'Task 08 two-player initialization passed.'
            break
        }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Two-player initialization timed out. Logs: $serverLog; $clientALog"
        }
        Start-Sleep -Milliseconds 250
    }

    $clientB = Start-ProbeProcess -Url "127.0.0.1:$Port" `
        -Role 'late ClientB (PlayerStateFirst)' -LogPath $clientBLog `
        -ExtraArguments @('-MiniProbeInitOrder=PlayerStateFirst')
    $startedAt = [DateTime]::UtcNow
    while ($true) {
        Assert-Running -Process $server -Role 'listen server' -LogPath $serverLog
        Assert-Running -Process $clientA -Role 'ClientA' -LogPath $clientALog
        Assert-Running -Process $clientB -Role 'late ClientB' -LogPath $clientBLog
        $serverText = Read-LogText -Path $serverLog
        $clientAText = Read-LogText -Path $clientALog
        $clientBText = Read-LogText -Path $clientBLog
        Assert-NoProbeFailure -LogText $serverText -LogPath $serverLog
        Assert-NoProbeFailure -LogText $clientAText -LogPath $clientALog
        Assert-NoProbeFailure -LogText $clientBText -LogPath $clientBLog
        if ((Test-SpawnSnapshot -LogText $serverText -NetMode 'ListenServer' -Players 3) -and
            (Test-SpawnSnapshot -LogText $clientAText -NetMode 'Client' -Players 3) -and
            (Test-SpawnSnapshot -LogText $clientBText -NetMode 'Client' -Players 3) -and
            (Test-InitSnapshot -LogText $serverText -NetMode 'ListenServer' -Players 3 -Simulated 0 -ProbeReleased 0) -and
            (Test-InitSnapshot -LogText $clientAText -NetMode 'Client' -Players 3 -Simulated 2 -ProbeReleased 3) -and
            (Test-InitSnapshot -LogText $clientBText -NetMode 'Client' -Players 3 -Simulated 2 -ProbeReleased 3)) {
            break
        }
        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $TimeoutSeconds) {
            throw "Three-player initialization timed out. Logs: $serverLog; $clientALog; $clientBLog"
        }
        Start-Sleep -Milliseconds 250
    }

    Assert-Transitions -LogText $serverText -LogPath $serverLog -Players 3
    Assert-Transitions -LogText $clientAText -LogPath $clientALog -Players 3
    Assert-Transitions -LogText $clientBText -LogPath $clientBLog -Players 3
    Assert-ReleaseOrder -LogText $clientAText -LogPath $clientALog -ExpectedOrder 'DataFirst' -Players 3
    Assert-ReleaseOrder -LogText $clientBText -LogPath $clientBLog -ExpectedOrder 'PlayerStateFirst' -Players 3
    if ($serverText.Contains('MiniInitProbe FIRST_RELEASE:') -or
        $serverText.Contains('MiniInitProbe SECOND_RELEASE:')) {
        throw "The server unexpectedly applied client-only order gates. See $serverLog"
    }

    Write-Host 'Task 08 late join, both initialization orders, simulated proxies and idempotent transitions passed.'
}
finally {
    Stop-ProbeProcesses -Processes @($clientB, $clientA, $server)
}
