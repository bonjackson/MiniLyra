[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18799
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
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask15',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 15 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask15Probe FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a Task 15 or Experience failure. See $LogPath"
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

function Assert-Later([System.Text.RegularExpressions.Match]$Later,
    [System.Text.RegularExpressions.Match]$Earlier, [string]$Description, [string]$LogPath) {
    if ($Later.Index -le $Earlier.Index) {
        throw "$Description arrived out of order. See $LogPath"
    }
}

$serverLog = Join-Path $logDirectory 'Task15-Server.log'
$clientALog = Join-Path $logDirectory 'Task15-ClientA.log'
$clientBLog = Join-Path $logDirectory 'Task15-ClientB.log'
$server = $null
$clientA = $null
$clientB = $null
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

    $clientA = Start-Probe "127.0.0.1:$Port" 'ClientA' $clientALog @()
    $clientB = Start-Probe "127.0.0.1:$Port" 'ClientB' $clientBLog @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        $clientAText = Assert-Healthy $clientA 'ClientA' $clientALog
        $clientBText = Assert-Healthy $clientB 'ClientB' $clientBLog
        if ($serverText -match 'MiniTask15Probe SERVER_PASS: Owners=2 Switches=4 Deaths=2 Respawns=2 Revoked=1 Source=1' -and
            $clientAText -match 'MiniTask15Probe PASS: Owner=\d+ Private=1 Switches=2 Deaths=2 Respawns=2 Own=Rifle Peer=Rifle' -and
            $clientBText -match 'MiniTask15Probe PASS: Owner=\d+ Private=1 Switches=2 Deaths=2 Respawns=2 Own=Rifle Peer=Rifle') {
            break
        }
        Start-Sleep -Milliseconds 500
    }

    $serverPass = Assert-One $serverText `
        'MiniTask15Probe SERVER_PASS: Owners=2 Switches=4 Deaths=2 Respawns=2 Revoked=1 Source=1' `
        'server PASS' $serverLog
    $noRefill = Assert-One $serverText `
        'MiniTask15Probe HOST_REMOVE_NO_REFILL: Items=0 Slots=0 Equipment=0 Revoked=1 NoRefill=1' `
        'same-Pawn removal without automatic refill' $serverLog
    Assert-Later $serverPass $noRefill 'server PASS after removal check' $serverLog
    $initial = @{}
    $fresh = @{}
    $serverDeaths = @{}
    $serverRespawns = @{}
    foreach ($owner in 1..2) {
        $first = Assert-One $serverText `
            "MiniTask15Probe SERVER_INITIAL: Owner=$owner Rifle=(\S+) Pistol=(\S+) Ammo=30/90,12/36 Active=0 Source=1 Mesh=Rifle" `
            "server initial owner $owner" $serverLog
        $initial[$owner] = @{ Rifle = $first.Groups[1].Value; Pistol = $first.Groups[2].Value }
        if ($initial[$owner].Rifle -eq $initial[$owner].Pistol) {
            throw "Owner $owner received duplicate item GUIDs. See $serverLog"
        }
        $pistol = Assert-One $serverText `
            "MiniTask15Probe SERVER_SWITCH: Owner=$owner Slot=1 Item=(\S+) Revoked=1 Source=1 Mesh=Pistol Path=ClientQ" `
            "server Q/RPC Pistol switch owner $owner" $serverLog
        $rifle = Assert-One $serverText `
            "MiniTask15Probe SERVER_SWITCH: Owner=$owner Slot=0 Item=(\S+) Revoked=1 Source=1 Mesh=Rifle Path=ServerSelect" `
            "server Rifle switch owner $owner" $serverLog
        if ($pistol.Groups[1].Value -ne $initial[$owner].Pistol -or
            $rifle.Groups[1].Value -ne $initial[$owner].Rifle) {
            throw "Owner $owner slot switch changed its item identity. See $serverLog"
        }
        Assert-Later $pistol $first "Owner $owner Pistol switch" $serverLog
        Assert-Later $rifle $pistol "Owner $owner Rifle switch" $serverLog

        $dead = Assert-One $serverText `
            "MiniTask15Probe SERVER_DEAD: Owner=$owner Pawn=\S+ Active=-1 Equipment=0 Revoked=1 Mesh=None" `
            "server death owner $owner" $serverLog
        $respawn = Assert-One $serverText `
            "MiniTask15Probe SERVER_RESPAWN: Owner=$owner OldRifle=(\S+) NewRifle=(\S+) OldPistol=(\S+) NewPistol=(\S+) Delay=([\d.]+) Source=1 Mesh=Rifle" `
            "server respawn owner $owner" $serverLog
        if ($respawn.Groups[1].Value -ne $initial[$owner].Rifle -or
            $respawn.Groups[3].Value -ne $initial[$owner].Pistol -or
            $respawn.Groups[2].Value -eq $initial[$owner].Rifle -or
            $respawn.Groups[4].Value -eq $initial[$owner].Pistol -or
            [double]$respawn.Groups[5].Value -lt 2.75) {
            throw "Owner $owner respawn reused an item or occurred before the death delay. See $serverLog"
        }
        $fresh[$owner] = @{ Rifle = $respawn.Groups[2].Value; Pistol = $respawn.Groups[4].Value }
        $serverDeaths[$owner] = $dead
        $serverRespawns[$owner] = $respawn
        Assert-Later $dead $rifle "Owner $owner death" $serverLog
        Assert-Later $respawn $dead "Owner $owner respawn" $serverLog
        Assert-Later $serverPass $respawn "Owner $owner server PASS" $serverLog
    }
    Assert-Later $serverDeaths[2] $serverRespawns[1] 'second death' $serverLog
    if ($initial[1].Rifle -eq $initial[2].Rifle -or
        $initial[1].Pistol -eq $initial[2].Pistol -or
        $fresh[1].Rifle -eq $fresh[2].Rifle) {
        throw "Two owners share an item GUID. See $serverLog"
    }

    $clientResults = @{}
    foreach ($client in @(@{ Text = $clientAText; Path = $clientALog },
            @{ Text = $clientBText; Path = $clientBLog })) {
        $pass = Assert-One $client.Text `
            'MiniTask15Probe PASS: Owner=(\d+) Private=1 Switches=2 Deaths=2 Respawns=2 Own=Rifle Peer=Rifle' `
            'client PASS' $client.Path
        $owner = [int]$pass.Groups[1].Value
        if ($owner -notin @(1, 2) -or $clientResults.ContainsKey($owner)) {
            throw "Clients did not receive distinct private owner probes. See $($client.Path)"
        }
        $clientResults[$owner] = $client.Path
        $snapshot = Assert-One $client.Text `
            "MiniTask15Probe CLIENT_INITIAL: Owner=$owner Rifle=(\S+) Pistol=(\S+) Ammo=30/90,12/36 Active=0 Private=1 Own=Rifle Peer=Rifle" `
            "client initial owner $owner" $client.Path
        if ($snapshot.Groups[1].Value -ne $initial[$owner].Rifle -or
            $snapshot.Groups[2].Value -ne $initial[$owner].Pistol) {
            throw "Owner $owner private initial snapshot differs from server. See $($client.Path)"
        }
        $qPressed = Assert-One $client.Text `
            "MiniTask15Probe CLIENT_Q_PRESSED: Owner=$owner From=Rifle To=Pistol Input=Q" `
            "client Q press owner $owner" $client.Path
        $qReleased = Assert-One $client.Text `
            "MiniTask15Probe CLIENT_Q_RELEASED: Owner=$owner Active=1 Input=Q" `
            "client Q release owner $owner" $client.Path
        Assert-Later $qPressed $snapshot "Owner $owner Q press" $client.Path
        Assert-Later $qReleased $qPressed "Owner $owner Q release" $client.Path
        $previous = $snapshot
        foreach ($slot in @(1, 0)) {
            $mesh = if ($slot -eq 1) { 'Pistol' } else { 'Rifle' }
            $path = if ($slot -eq 1) { 'ClientQ' } else { 'ServerSelect' }
            $switch = Assert-One $client.Text `
                "MiniTask15Probe CLIENT_SWITCH: Owner=$owner Slot=$slot Rifle=(\S+) Pistol=(\S+) Own=$mesh Peer=$mesh Path=$path" `
                "client $mesh switch owner $owner" $client.Path
            if ($switch.Groups[1].Value -ne $initial[$owner].Rifle -or
                $switch.Groups[2].Value -ne $initial[$owner].Pistol) {
                throw "Owner $owner switch changed private item GUIDs. See $($client.Path)"
            }
            Assert-Later $switch $previous "Owner $owner $mesh switch" $client.Path
            if ($slot -eq 1) {
                Assert-Later $switch $qReleased "Owner $owner Q-path Pistol switch" $client.Path
            }
            $previous = $switch
        }
        foreach ($victim in 1..2) {
            $own = if ($owner -eq $victim) { 'None' } else { 'Rifle' }
            $peer = if ($owner -eq $victim) { 'Rifle' } else { 'None' }
            $expected = if ($victim -eq 2 -and $owner -eq 1) { $fresh[1] } else { $initial[$owner] }
            $dead = Assert-One $client.Text `
                "MiniTask15Probe CLIENT_DEAD: Owner=$owner Victim=$victim Rifle=(\S+) Pistol=(\S+) Own=$own Peer=$peer Equipment=0 Mesh=None" `
                "client observes death $victim" $client.Path
            if ($dead.Groups[1].Value -ne $expected.Rifle -or
                $dead.Groups[2].Value -ne $expected.Pistol) {
                throw "Owner $owner private QuickBar changed unexpectedly at death $victim. See $($client.Path)"
            }
            Assert-Later $dead $previous "Owner $owner observes death $victim" $client.Path
            $expected = if ($owner -eq $victim) { $fresh[$owner] } elseif ($victim -eq 2 -and $owner -eq 1) { $fresh[1] } else { $initial[$owner] }
            $freshFlag = if ($owner -eq $victim) { 1 } else { 0 }
            $respawn = Assert-One $client.Text `
                "MiniTask15Probe CLIENT_RESPAWN: Owner=$owner Victim=$victim Rifle=(\S+) Pistol=(\S+) Fresh=$freshFlag Own=Rifle Peer=Rifle" `
                "client observes respawn $victim" $client.Path
            if ($respawn.Groups[1].Value -ne $expected.Rifle -or
                $respawn.Groups[2].Value -ne $expected.Pistol) {
                throw "Owner $owner private QuickBar differs from server after respawn $victim. See $($client.Path)"
            }
            Assert-Later $respawn $dead "Owner $owner observes respawn $victim" $client.Path
            $previous = $respawn
        }
        Assert-Later $pass $previous "Owner $owner client PASS" $client.Path
    }
    if ($clientResults.Count -ne 2) {
        throw 'Only one private owner probe completed.'
    }
    Write-Host 'Task 15 passed: two private QuickBars, client Q/RPC Pistol switches, server Rifle switches, revoked grants and current SourceObject, both client appearances, immediate death cleanup, fresh respawn items, and no same-Pawn refill after item removal.'
}
finally {
    foreach ($process in @($clientB, $clientA, $server)) {
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
