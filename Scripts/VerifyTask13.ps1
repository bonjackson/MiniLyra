[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18797
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

function Read-ProbeLog([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $content = Get-Content -LiteralPath $Path -Raw
        if ($null -ne $content) { return ,([string]$content) }
    }
    return ,([string]::Empty)
}

function Start-Probe([string]$Url, [string]$Role, [string]$LogPath, [string[]]$ExtraArguments) {
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath
    }
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask13',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 13 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask13Probe FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a Task 13 or Experience failure. See $LogPath"
    }
    return $logText
}

function Assert-One([string]$Text, [string]$Pattern, [string]$Description, [string]$LogPath) {
    $matches = [regex]::Matches($Text, $Pattern)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one $Description; found $($matches.Count). See $LogPath"
    }
    return $matches[0]
}

function Assert-ClientProbe([string]$Text, [string]$LogPath) {
    $ready = Assert-One $Text `
        'MiniTask13Probe CLIENT_READY: Pawn=(\S+) ASC=(\S+) Abilities=(\d+) Health=100\.0 Max=100\.0' `
        'client initial state' $LogPath
    [void](Assert-One $Text `
        'MiniTask13Probe CLIENT_DAMAGE: Cycle=1 Health=75\.0 Max=100\.0 Pawn=\S+' `
        'replicated nonlethal health' $LogPath)
    $previousPawn = $ready.Groups[1].Value
    $asc = $ready.Groups[2].Value
    $abilities = $ready.Groups[3].Value
    foreach ($cycle in 1, 2) {
        $dead = Assert-One $Text `
            "MiniTask13Probe CLIENT_DEAD: Cycle=$cycle Health=0\.0 DeadTag=1 Pawn=(\S+)" `
            "client replicated death $cycle" $LogPath
        if ($dead.Groups[1].Value -ne $previousPawn) {
            throw "Death $cycle was observed on the wrong client Pawn. See $LogPath"
        }
        $respawn = Assert-One $Text `
            "MiniTask13Probe CLIENT_RESPAWN: Cycle=$cycle OldPawn=(\S+) NewPawn=(\S+) ASC=(\S+) Abilities=(\d+) Health=100\.0" `
            "client respawn $cycle" $LogPath
        if ($respawn.Groups[1].Value -ne $previousPawn -or
            $respawn.Groups[2].Value -eq $previousPawn -or
            $respawn.Groups[3].Value -ne $asc -or
            $respawn.Groups[4].Value -ne $abilities) {
            throw "Respawn $cycle changed ASC or ability count, or reused the dead Pawn. See $LogPath"
        }
        $previousPawn = $respawn.Groups[2].Value
        [void](Assert-One $Text `
            "MiniTask13Probe CLIENT_INPUT_AIM: Cycle=$cycle Pawn=$([regex]::Escape($previousPawn))" `
            "client aim after respawn $cycle" $LogPath)
        [void](Assert-One $Text `
            "MiniTask13Probe CLIENT_INPUT_RELEASED: Cycle=$cycle Pawn=$([regex]::Escape($previousPawn))" `
            "client camera recovery after respawn $cycle" $LogPath)
    }
    $forced = Assert-One $Text `
        'MiniTask13Probe CLIENT_FORCED_RECOVERY: OldPawn=(\S+) NewPawn=(\S+) ASC=(\S+) Abilities=(\d+) Health=100\.0 DeadTag=0' `
        'client forced respawn recovery' $LogPath
    if ($forced.Groups[1].Value -ne $previousPawn -or
        $forced.Groups[2].Value -eq $previousPawn -or
        $forced.Groups[3].Value -ne $asc -or
        $forced.Groups[4].Value -ne $abilities) {
        throw "Forced client recovery changed ASC or abilities, or reused the dead Pawn. See $LogPath"
    }
    $previousPawn = $forced.Groups[2].Value
    [void](Assert-One $Text `
        "MiniTask13Probe CLIENT_FORCED_AIM: Pawn=$([regex]::Escape($previousPawn))" `
        'client aim after forced respawn' $LogPath)
    [void](Assert-One $Text `
        "MiniTask13Probe CLIENT_FORCED_RELEASED: Pawn=$([regex]::Escape($previousPawn))" `
        'client camera recovery after forced respawn' $LogPath)
    [void](Assert-One $Text `
        'MiniTask13Probe PASS: Damage=1 Invalid=1 Deaths=3 Respawns=3 Reuse=1 InputCamera=3 ForcedCleanup=1' `
        'final Task 13 PASS' $LogPath)
}

function Assert-ServerProbe([string]$Text, [string]$LogPath) {
    $ready = Assert-One $Text `
        'MiniTask13Probe SERVER_READY: Pawn=(\S+) ASC=(\S+) Actor=\S+' `
        'server-owned probe actor' $LogPath
    [void](Assert-One $Text `
        'MiniTask13Probe SERVER_INVALID_PASS: Self=1 Null=1 Health=100\.0' `
        'self and invalid target rejection' $LogPath)
    [void](Assert-One $Text `
        'MiniTask13Probe SERVER_DAMAGE: Cycle=1 Health=75\.0 Max=100\.0 Pawn=\S+' `
        'authoritative nonlethal health' $LogPath)
    $previousPawn = $ready.Groups[1].Value
    $asc = $ready.Groups[2].Value
    $abilities = $null
    $deadPawns = @()
    foreach ($cycle in 1, 2) {
        $dead = Assert-One $Text `
            "MiniTask13Probe SERVER_DEAD: Cycle=$cycle Health=0\.0 RepeatRejected=1 Pawn=(\S+)" `
            "server lethal hit and repeat rejection $cycle" $LogPath
        if ($dead.Groups[1].Value -ne $previousPawn) {
            throw "Server death $cycle occurred on the wrong Pawn. See $LogPath"
        }
        $deadPawns += $dead.Groups[1].Value
        $respawn = Assert-One $Text `
            "MiniTask13Probe SERVER_RESPAWN: Cycle=$cycle OldPawn=(\S+) NewPawn=(\S+) ASC=(\S+) Abilities=(\d+) Health=100\.0 Delay=([\d.]+)" `
            "server replacement Pawn $cycle" $LogPath
        if ($respawn.Groups[1].Value -ne $previousPawn -or
            $respawn.Groups[2].Value -eq $previousPawn -or
            $respawn.Groups[3].Value -ne $asc -or
            ($null -ne $abilities -and $respawn.Groups[4].Value -ne $abilities) -or
            [double]::Parse($respawn.Groups[5].Value,
                [System.Globalization.CultureInfo]::InvariantCulture) -lt 2.5) {
            throw "Server respawn $cycle did not preserve ASC/abilities or honor delay. See $LogPath"
        }
        $abilities = $respawn.Groups[4].Value
        $previousPawn = $respawn.Groups[2].Value
    }
    $forced = Assert-One $Text `
        'MiniTask13Probe SERVER_FORCED_CLEANUP: OldPawn=(\S+) NewPawn=(\S+) ASC=(\S+) Abilities=(\d+) Health=100\.0 DeadTag=0' `
        'server forced death cleanup' $LogPath
    if ($forced.Groups[1].Value -ne $previousPawn -or
        $forced.Groups[2].Value -eq $previousPawn -or
        $forced.Groups[3].Value -ne $asc -or
        $forced.Groups[4].Value -ne $abilities) {
        throw "Forced server recovery changed ASC or abilities, or reused the dead Pawn. See $LogPath"
    }
    $confirmedRespawns = [regex]::Matches($Text,
        'MiniHealth RESPAWNED: OldPawn=\S+ NewPawn=(\S+) Health=100\.0')
    if ($confirmedRespawns.Count -ne 3 -or
        $confirmedRespawns[2].Groups[1].Value -ne $forced.Groups[2].Value) {
        throw "Delayed corpse cleanup did not confirm the forced replacement Avatar. See $LogPath"
    }
    $deathStarts = [regex]::Matches($Text, 'MiniHealth DEATH_STARTED: Pawn=(\S+)')
    if ($deathStarts.Count -ne 3 -or
        $deathStarts[0].Groups[1].Value -ne $deadPawns[0] -or
        $deathStarts[1].Groups[1].Value -ne $deadPawns[1] -or
        $deathStarts[2].Groups[1].Value -ne $previousPawn) {
        throw "Expected one authoritative death transition for each old Pawn. See $LogPath"
    }
}

$serverLog = Join-Path $logDirectory 'Task13-Server.log'
$clientLog = Join-Path $logDirectory 'Task13-ClientA.log'
$server = $null
$client = $null
try {
    $server = Start-Probe '/Game/Mini/Maps/L_MiniPractice?listen' 'listen server' $serverLog @("-port=$Port")
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $listenerPattern = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        if ($serverText -match $listenerPattern) { break }
        Start-Sleep -Milliseconds 500
    }
    if ($serverText -notmatch $listenerPattern) {
        throw "Server did not start listening. See $serverLog"
    }

    $client = Start-Probe "127.0.0.1:$Port" 'ClientA' $clientLog @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        $clientText = Assert-Healthy $client 'ClientA' $clientLog
        if ($clientText.Contains('MiniTask13Probe PASS: Damage=1 Invalid=1 Deaths=3 Respawns=3 Reuse=1 InputCamera=3 ForcedCleanup=1') -and
            [regex]::Matches($serverText, 'MiniTask13Probe SERVER_RESPAWN:').Count -eq 2 -and
            [regex]::Matches($serverText, 'MiniTask13Probe SERVER_FORCED_CLEANUP:').Count -eq 1 -and
            [regex]::Matches($serverText, 'MiniHealth DEATH_STARTED:').Count -eq 3 -and
            [regex]::Matches($serverText, 'MiniHealth RESPAWNED:').Count -eq 3) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $clientText.Contains('MiniTask13Probe PASS: Damage=1 Invalid=1 Deaths=3 Respawns=3 Reuse=1 InputCamera=3 ForcedCleanup=1') -or
        [regex]::Matches($serverText, 'MiniTask13Probe SERVER_RESPAWN:').Count -ne 2 -or
        [regex]::Matches($serverText, 'MiniTask13Probe SERVER_FORCED_CLEANUP:').Count -ne 1 -or
        [regex]::Matches($serverText, 'MiniHealth DEATH_STARTED:').Count -ne 3 -or
        [regex]::Matches($serverText, 'MiniHealth RESPAWNED:').Count -ne 3) {
        throw "Task 13 runtime probe timed out. Logs: $serverLog; $clientLog"
    }
    Assert-ClientProbe $clientText $clientLog
    Assert-ServerProbe $serverText $serverLog
    Write-Host 'Task 13 passed: authoritative damage/refusal, replicated health/death, two delayed respawns, forced death cleanup and replacement, reused ASC, and restored input/camera.'
}
finally {
    foreach ($process in @($client, $server)) {
        if ($null -eq $process) { continue }
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit(10000) | Out-Null
            }
        }
        catch [System.InvalidOperationException] { }
    }
}
