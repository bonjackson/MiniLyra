[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18794
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
    # Start-Process joins ArgumentList into a command line; quote paths that may contain spaces.
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask10',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 10 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniInputProbe [^\r\n]*FAIL:' -or
        $logText -match 'Experience state .* -> Failed' -or
        $logText -match 'MiniAddInput missing MappingContext') {
        throw "$Role reported an input or Experience failure. See $LogPath"
    }
    return $logText
}

function Assert-ExactlyOne([string]$Text, [string]$Pattern, [string]$Description, [string]$LogPath) {
    $count = [regex]::Matches($Text, $Pattern).Count
    if ($count -ne 1) {
        throw "Expected exactly one $Description; found $count. See $LogPath"
    }
}

function Assert-ClientProbe([string]$Text, [string]$LogPath) {
    $readyPattern = 'MiniInputProbe CYCLE_READY: Cycle=(\d+) Pawn=(\S+) Registrations=(\d+) Bindings=(\d+) Simulated=(\d+)'
    $readies = [regex]::Matches($Text, $readyPattern)
    if ($readies.Count -ne 4) {
        throw "Expected four CYCLE_READY records; found $($readies.Count). See $LogPath"
    }
    $pawns = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for ($index = 0; $index -lt 4; $index++) {
        $ready = $readies[$index]
        $cycle = $index + 1
        $pawn = $ready.Groups[2].Value
        if ([int]$ready.Groups[1].Value -ne $cycle -or
            [int]$ready.Groups[3].Value -ne 1 -or
            [int]$ready.Groups[4].Value -ne 16 -or
            [int]$ready.Groups[5].Value -ne 1 -or
            -not $pawns.Add($pawn)) {
            throw "Cycle $cycle has a repeated Pawn, mapping, binding, or simulated-proxy mismatch. See $LogPath"
        }
        $escapedPawn = [regex]::Escape($pawn)
        Assert-ExactlyOne $Text "MiniInputProbe FIRE_HELD: Cycle=$cycle Pawn=$escapedPawn(?=\s|`$)" `
            "FIRE_HELD for cycle $cycle" $LogPath
        Assert-ExactlyOne $Text "MiniInputProbe FIRE_ENDED: Cycle=$cycle Pawn=$escapedPawn(?=\s|`$)" `
            "FIRE_ENDED for cycle $cycle" $LogPath

        # Cycles 1-3 press Fire once. Cycle 4 presses it five times: initial shot,
        # before each gate, and after each gate resumes. Suppressed presses must
        # never reach the ability binding.
        $expectedPresses = if ($cycle -eq 4) { 5 } else { 1 }
        $pressPattern = "MiniInput TAG_PRESSED: Pawn=$escapedPawn Tag=InputTag\.Ability\.Fire(?=\s|`$)"
        $presses = [regex]::Matches($Text, $pressPattern).Count
        if ($presses -ne $expectedPresses) {
            throw "Cycle $cycle Pawn received $presses Fire presses; expected $expectedPresses. See $LogPath"
        }
    }
    Assert-ExactlyOne $Text 'MiniInputProbe MOVE_PASS: Pawn=\S+ Distance=(?<distance>[\d.]+)' `
        'MOVE_PASS' $LogPath
    foreach ($marker in @('MENU_BLOCKED', 'MENU_NO_FIRE', 'MENU_RESTORED',
        'ACTION_BLOCKED', 'ACTION_NO_FIRE', 'JUMP_CLEARED', 'BLOCKED_RESPAWN')) {
        Assert-ExactlyOne $Text "MiniInputProbe $marker`: Pawn=\S+" $marker $LogPath
    }
    for ($cycle = 1; $cycle -le 3; $cycle++) {
        Assert-ExactlyOne $Text "MiniInputProbe UNPOSSESSED_CLEAN: Cycle=$cycle Pawn=\S+" `
            "unpossessed Pawn cleanup for cycle $cycle" $LogPath
    }
    Assert-ExactlyOne $Text 'MiniInputProbe PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1' `
        'final PASS' $LogPath
    Assert-ExactlyOne $Text 'MiniAddInput PROBE_SUSPENDED:' 'input Action suspension' $LogPath
    Assert-ExactlyOne $Text 'MiniAddInput PROBE_RESUMED:' 'input Action restoration' $LogPath
}

function Assert-ServerProbe([string]$Text, [string]$LogPath) {
    $pattern = 'MiniInputProbe SERVER_RESPAWN: Cycle=(\d+) OldPawn=(\S+) NewPawn=(\S+)'
    $events = [regex]::Matches($Text, $pattern)
    if ($events.Count -ne 3) {
        throw "Expected three SERVER_RESPAWN records; found $($events.Count). See $LogPath"
    }
    for ($index = 0; $index -lt 3; $index++) {
        $event = $events[$index]
        if ([int]$event.Groups[1].Value -ne $index + 1 -or
            $event.Groups[2].Value -eq $event.Groups[3].Value -or
            $event.Groups[3].Value -eq 'None') {
            throw "Server respawn $($index + 1) did not replace the Pawn. See $LogPath"
        }
        Assert-ExactlyOne $Text "MiniInputProbe SERVER_UNPOSSESSED: Cycle=$($index + 1) OldPawn=\S+" `
            "server unpossess for cycle $($index + 1)" $LogPath
    }
    $serverPawns = @($events[0].Groups[2].Value) + @($events | ForEach-Object { $_.Groups[3].Value })
    for ($index = 0; $index -lt 4; $index++) {
        $avatar = [regex]::Escape($serverPawns[$index])
        $expected = if ($index -eq 3) { 5 } else { 1 }
        foreach ($eventName in @('FIRE_ACTIVE', 'FIRE_ENDED')) {
            $count = [regex]::Matches($Text, "MiniInputProbe $eventName`: Avatar=$avatar Handle=").Count
            if ($count -ne $expected) {
                throw "Server Pawn $($index + 1) had $count $eventName events; expected $expected. See $LogPath"
            }
        }
    }
    foreach ($eventName in @('FIRE_ACTIVE', 'FIRE_ENDED')) {
        $count = [regex]::Matches($Text, "MiniInputProbe $eventName`: Avatar=").Count
        if ($count -ne 8) {
            throw "Server had $count total $eventName events; expected 8. See $LogPath"
        }
    }
}

$serverLog = Join-Path $logDirectory 'Task10-Server.log'
$clientLog = Join-Path $logDirectory 'Task10-ClientA.log'
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
        if ($clientText.Contains('MiniInputProbe PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1') -and
            [regex]::Matches($serverText, 'MiniInputProbe FIRE_ENDED: Avatar=').Count -ge 8) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $clientText.Contains('MiniInputProbe PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1') -or
        [regex]::Matches($serverText, 'MiniInputProbe FIRE_ENDED: Avatar=').Count -lt 8) {
        throw "Task 10 input lifecycle timed out. Logs: $serverLog; $clientLog"
    }
    Assert-ClientProbe $clientText $clientLog
    Assert-ServerProbe $serverText $serverLog
    Write-Host 'Task 10 passed: local movement and Fire input, three respawns, menu/Action revocation, and simulated-proxy isolation.'
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
