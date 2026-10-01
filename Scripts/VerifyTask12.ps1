[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18796
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
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask12', '-MiniProbeLegacyExperience',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 12 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask12Probe FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a Task 12 or Experience failure. See $LogPath"
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
    foreach ($marker in @(
        'CLIENT_READY', 'JUMP_ACTIVE', 'JUMP_RELEASED', 'AIM_ACTIVE', 'AIM_SINGLE',
        'AIM_RELEASED', 'RELOAD_CANCEL', 'RELOAD_JUMP_PASS', 'DEAD_CANCEL',
        'INPUT_CANCEL', 'CLIENT_OLD_CLEAN', 'NEW_PAWN_CLEAN', 'NEW_AIM_ACTIVE')) {
        Assert-ExactlyOne $Text "MiniTask12Probe $marker`:" $marker $LogPath
    }
    Assert-ExactlyOne $Text `
        'MiniTask12Probe PASS: Jump=1 Aim=1 Reload=1 Dead=1 InputBlock=1 Respawn=1 Reuse=1' `
        'Task 12 final PASS' $LogPath
    $old = [regex]::Match($Text, 'MiniTask12Probe CLIENT_READY: Pawn=(\S+) ASC=(\S+)')
    $clean = [regex]::Match($Text,
        'MiniTask12Probe NEW_PAWN_CLEAN: OldPawn=(\S+) NewPawn=(\S+) ASC=(\S+)')
    if (-not $old.Success -or -not $clean.Success -or
        $old.Groups[1].Value -ne $clean.Groups[1].Value -or
        $old.Groups[2].Value -ne $clean.Groups[3].Value -or
        $clean.Groups[1].Value -eq $clean.Groups[2].Value) {
        throw "The replacement Pawn did not reuse the original PlayerState ASC. See $LogPath"
    }
    $newAim = [regex]::Match($Text, 'MiniTask12Probe NEW_AIM_ACTIVE: Pawn=(\S+)')
    if (-not $newAim.Success -or $newAim.Groups[1].Value -ne $clean.Groups[2].Value) {
        throw "Aim did not reactivate on the replacement Pawn. See $LogPath"
    }
}

function Assert-ServerProbe([string]$Text, [string]$LogPath) {
    Assert-ExactlyOne $Text 'MiniTask12Probe SERVER_READY: Pawn=\S+ Actor=\S+' `
        'server-owned probe actor' $LogPath
    foreach ($pair in @(
        @('State.Reloading', 'InputTag.Ability.Aim'),
        @('State.Dead', 'InputTag.Jump'),
        @('Gameplay.AbilityInputBlocked', 'InputTag.Ability.Aim'))) {
        $blocker = [regex]::Escape($pair[0])
        $ability = [regex]::Escape($pair[1])
        Assert-ExactlyOne $Text `
            "MiniTask12Probe SERVER_CANCEL_PASS: Blocker=$blocker Ability=$ability Avatar=\S+" `
            "server cancellation $($pair[0]) / $($pair[1])" $LogPath
    }
    $rejections = @(
        @('State.Reloading', 'InputTag.Ability.Aim'),
        @('State.Reloading', 'InputTag.Ability.Fire'),
        @('State.Dead', 'InputTag.Jump'),
        @('State.Dead', 'InputTag.Ability.Aim'),
        @('State.Dead', 'InputTag.Ability.Fire'),
        @('Gameplay.AbilityInputBlocked', 'InputTag.Jump'),
        @('Gameplay.AbilityInputBlocked', 'InputTag.Ability.Aim'),
        @('Gameplay.AbilityInputBlocked', 'InputTag.Ability.Fire')
    )
    foreach ($pair in $rejections) {
        $blocker = [regex]::Escape($pair[0])
        $ability = [regex]::Escape($pair[1])
        Assert-ExactlyOne $Text `
            "MiniTask12Probe SERVER_REJECT_PASS: Blocker=$blocker Ability=$ability Avatar=\S+" `
            "server rejection $($pair[0]) / $($pair[1])" $LogPath
    }
    Assert-ExactlyOne $Text 'MiniTask12Probe OLD_PAWN_CLEAN: Pawn=\S+' `
        'authoritative old-Pawn ability cleanup' $LogPath
    $respawn = [regex]::Match($Text,
        'MiniTask12Probe SERVER_RESPAWN: OldPawn=(\S+) NewPawn=(\S+)')
    if (-not $respawn.Success -or $respawn.Groups[1].Value -eq $respawn.Groups[2].Value) {
        throw "Expected one real server Pawn replacement. See $LogPath"
    }
    Assert-ExactlyOne $Text 'MiniTask12Probe SERVER_RESPAWN: OldPawn=\S+ NewPawn=\S+' `
        'server Pawn replacement' $LogPath
}

$serverLog = Join-Path $logDirectory 'Task12-Server.log'
$clientLog = Join-Path $logDirectory 'Task12-ClientA.log'
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
        if ($clientText.Contains('MiniTask12Probe PASS: Jump=1 Aim=1 Reload=1 Dead=1 InputBlock=1 Respawn=1 Reuse=1') -and
            $serverText.Contains('MiniTask12Probe SERVER_RESPAWN:') -and
            [regex]::Matches($serverText, 'MiniTask12Probe SERVER_REJECT_PASS:').Count -eq 8 -and
            [regex]::Matches($serverText, 'MiniTask12Probe SERVER_CANCEL_PASS:').Count -eq 3) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $clientText.Contains('MiniTask12Probe PASS: Jump=1 Aim=1 Reload=1 Dead=1 InputBlock=1 Respawn=1 Reuse=1') -or
        -not $serverText.Contains('MiniTask12Probe SERVER_RESPAWN:') -or
        [regex]::Matches($serverText, 'MiniTask12Probe SERVER_REJECT_PASS:').Count -ne 8 -or
        [regex]::Matches($serverText, 'MiniTask12Probe SERVER_CANCEL_PASS:').Count -ne 3) {
        throw "Task 12 runtime probe timed out. Logs: $serverLog; $clientLog"
    }
    Assert-ClientProbe $clientText $clientLog
    Assert-ServerProbe $serverText $serverLog
    Write-Host 'Task 12 passed: input-driven Jump/Aim activation and release, tag cancellation and server rejection, input cleanup, and authoritative Pawn replacement.'
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
