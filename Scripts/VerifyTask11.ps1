[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18795
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
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask10', '-MiniProbeTask11',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 11 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask11Probe FAIL:' -or
        $logText -match 'MiniInputProbe [^\r\n]*FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a probe or Experience failure. See $LogPath"
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
    $camera = [regex]::Matches($Text, 'MiniTask11Probe CAMERA_READY: Cycle=(\d+) Pawn=(\S+) Mode=(\S+) FOV=([\d.]+)')
    $input = [regex]::Matches($Text, 'MiniInputProbe CYCLE_READY: Cycle=(\d+) Pawn=(\S+)')
    if ($camera.Count -ne 4 -or $input.Count -ne 4) {
        throw "Expected four camera and four input cycles; found $($camera.Count) and $($input.Count). See $LogPath"
    }
    $pawns = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for ($index = 0; $index -lt 4; $index++) {
        $cycle = $index + 1
        $pawn = $camera[$index].Groups[2].Value
        if ([int]$camera[$index].Groups[1].Value -ne $cycle -or
            [int]$input[$index].Groups[1].Value -ne $cycle -or
            $input[$index].Groups[2].Value -ne $pawn -or
            -not $pawns.Add($pawn) -or
            $camera[$index].Groups[3].Value -notmatch 'MiniCameraMode_ThirdPerson' -or
            [Math]::Abs([double]$camera[$index].Groups[4].Value - 85.0) -gt 2.0) {
            throw "Camera cycle $cycle did not follow the locally possessed Pawn or default mode. See $LogPath"
        }
    }
    $respawns = [regex]::Matches($Text, 'MiniTask11Probe RESPAWN_CAMERA: Cycle=(\d+) OldPawn=(\S+) NewPawn=(\S+)')
    if ($respawns.Count -ne 3) {
        throw "Expected three camera ownership transfers; found $($respawns.Count). See $LogPath"
    }
    for ($index = 0; $index -lt 3; $index++) {
        if ([int]$respawns[$index].Groups[1].Value -ne $index + 1 -or
            $respawns[$index].Groups[2].Value -ne $camera[$index].Groups[2].Value -or
            $respawns[$index].Groups[3].Value -ne $camera[$index + 1].Groups[2].Value) {
            throw "Respawn $($index + 1) did not transfer the camera to the replacement Pawn. See $LogPath"
        }
    }
    foreach ($marker in @('AIM_PASS', 'COLLISION_PASS', 'REMOTE_PASS', 'ANIM_LOCAL_MOVE',
        'ANIM_REMOTE_MOVE', 'ANIM_REMOTE_IDLE', 'ANIM_JUMP', 'ANIM_LAND')) {
        Assert-ExactlyOne $Text "MiniTask11Probe $marker`: Pawn=\S+" $marker $LogPath
    }
    Assert-ExactlyOne $Text 'MiniTask11Probe REMOTE_PASS: Pawn=\S+ Distance=[\d.]+ YawChange=[\d.]+ Role=1' `
        'replicated simulated-proxy movement and orientation' $LogPath
    Assert-ExactlyOne $Text 'MiniTask11Probe PASS: CameraCycles=4 Respawns=3 Aim=1 Collision=1 Remote=1 Anim=1' `
        'Task 11 final PASS' $LogPath
    Assert-ExactlyOne $Text 'MiniInputProbe PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1' `
        'Task 10 input lifecycle PASS' $LogPath
}

function Assert-ServerProbe([string]$Text, [string]$LogPath) {
    Assert-ExactlyOne $Text 'MiniTask11Probe REMOTE_DRIVE_START: Pawn=\S+' 'server remote drive start' $LogPath
    Assert-ExactlyOne $Text 'MiniTask11Probe REMOTE_DRIVE_DONE: Pawn=\S+ Distance=[\d.]+ Yaw=[\d.]+' `
        'server remote drive completion' $LogPath
    $respawns = [regex]::Matches($Text, 'MiniInputProbe SERVER_RESPAWN: Cycle=\d+ OldPawn=\S+ NewPawn=\S+')
    if ($respawns.Count -ne 3) {
        throw "Expected three server respawns; found $($respawns.Count). See $LogPath"
    }
    if ([regex]::Matches($Text, 'MiniInputProbe FIRE_ENDED: Avatar=').Count -ne 8) {
        throw "Task 10 server Fire lifecycle did not complete eight times. See $LogPath"
    }
}

$serverLog = Join-Path $logDirectory 'Task11-Server.log'
$clientLog = Join-Path $logDirectory 'Task11-ClientA.log'
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
        if ($clientText.Contains('MiniTask11Probe PASS: CameraCycles=4 Respawns=3 Aim=1 Collision=1 Remote=1 Anim=1') -and
            $clientText.Contains('MiniInputProbe PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1') -and
            $serverText.Contains('MiniTask11Probe REMOTE_DRIVE_DONE:')) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $clientText.Contains('MiniTask11Probe PASS: CameraCycles=4 Respawns=3 Aim=1 Collision=1 Remote=1 Anim=1') -or
        -not $clientText.Contains('MiniInputProbe PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1') -or
        -not $serverText.Contains('MiniTask11Probe REMOTE_DRIVE_DONE:')) {
        throw "Task 11 runtime probe timed out. Logs: $serverLog; $clientLog"
    }
    Assert-ClientProbe $clientText $clientLog
    Assert-ServerProbe $serverText $serverLog
    Write-Host 'Task 11 passed: local camera modes and collision, three respawn ownership transfers, replicated remote movement and orientation, animation movement/jump/landing, and Task 10 input lifecycle.'
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
