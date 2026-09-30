[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 360,
    [ValidateRange(1024, 65535)][int]$Port = 18817
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
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null

function Read-ProbeLog([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return [string](Get-Content -LiteralPath $Path -Raw)
    }
    return [string]::Empty
}

function Start-Probe([string]$Url, [string]$Role, [string]$LogPath, [string[]]$ExtraArguments) {
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath
    }
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask17',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 17 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask17Probe FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a Task 17 or Experience failure. See $LogPath"
    }
    return $logText
}

function Assert-Marker([string]$Log, [string]$Marker, [string]$Path) {
    if ($Log -notmatch $Marker) {
        throw "Missing '$Marker' in $Path"
    }
}

function Assert-InOrder([string]$Log, [string[]]$Markers, [string]$Path) {
    $previous = -1
    foreach ($marker in $Markers) {
        $match = [regex]::Match($Log, $marker)
        if (-not $match.Success -or $match.Index -le $previous) {
            throw "Missing or out-of-order '$marker' in $Path"
        }
        $previous = $match.Index
    }
}

$serverLog = Join-Path $logDirectory 'Task17-Server.log'
$clientALog = Join-Path $logDirectory 'Task17-ClientA.log'
$clientBLog = Join-Path $logDirectory 'Task17-ClientB.log'
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
    $pass = 'MiniTask17Probe SERVER_PASS: OwnerAmmo=1 FullNoWaste=1 EmptyFeedback=1 RifleReload=1 FireBlocked=1 PartialReserve=1 NoReserve=1 SwitchCancel=1 PistolSemiAuto=1 PistolReload=1 DeathCancel=1 IndependentAmmo=1 StaleItem=1'
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        $clientAText = Assert-Healthy $clientA 'ClientA' $clientALog
        $clientBText = Assert-Healthy $clientB 'ClientB' $clientBLog
        if ($serverText.Contains($pass) -and
            ($clientAText + $clientBText) -match 'MiniTask17Probe CLIENT_DEATH_CANCEL: Owner=1' -and
            ($clientAText + $clientBText) -match 'MiniTask17Probe CLIENT_DEATH_OBSERVER: Owner=2') {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    Assert-Marker $serverText ([regex]::Escape($pass)) $serverLog

    Assert-InOrder $serverText @(
        'MiniTask17Probe SERVER_READY:',
        'MiniTask17Probe SERVER_INITIAL:',
        'MiniTask17Probe SERVER_RIFLE_SHOT: Shots=1 Rifle=29/90 Pistol=12/36 Target=75',
        'MiniTask17Probe SERVER_FULL_RELOAD: Rifle=30/90 ReserveWasted=0',
        'MiniTask17Probe SERVER_EMPTY: Rejected=1 Shots=1 Target=75',
        'MiniTask17Probe SERVER_RELOAD_FIRE_BLOCK: Rejected=1 Ammo=0/90 Target=75',
        'MiniTask17Probe SERVER_RIFLE_RELOAD: Rifle=30/60 OwnerAgreement=1',
        'MiniTask17Probe SERVER_PARTIAL_RELOAD: Rifle=28/0 Transfer=3',
        'MiniTask17Probe SERVER_NO_RESERVE: Rifle=28/0 NoTransfer=1',
        'MiniTask17Probe SERVER_RIFLE_CANCEL_START: Reloading=1 Ammo=20/60',
        'MiniTask17Probe SERVER_SWITCH_CANCEL: Rifle=20/60 Pistol=12/36 Reloading=0',
        'MiniTask17Probe SERVER_PISTOL_SHOT: Shots=2 Pistol=11/36 Rifle=20/60 Damage=20.0 SemiAuto=1',
        'MiniTask17Probe SERVER_STALE_ITEM: WrongEquipment=1 SequenceUnchanged=1',
        'MiniTask17Probe SERVER_PISTOL_RELOAD: Pistol=12/35 Rifle=20/60',
        'MiniTask17Probe SERVER_DEATH_CANCEL_START: Reloading=0 Equipment=0 Pistol=5/35',
        'MiniTask17Probe SERVER_PASS:'
    ) $serverLog

    $clients = @(@{ Path = $clientALog; Log = $clientAText },
                 @{ Path = $clientBLog; Log = $clientBText })
    $ownerOne = @($clients | Where-Object { $_.Log -match 'MiniTask17Probe CLIENT_INITIAL: Owner=1 Rifle=30/90 Pistol=12/36' })
    $ownerTwo = @($clients | Where-Object { $_.Log -match 'MiniTask17Probe CLIENT_INITIAL: Owner=2 Rifle=30/90 Pistol=12/36' })
    if ($ownerOne.Count -ne 1 -or $ownerTwo.Count -ne 1 -or $ownerOne[0].Path -eq $ownerTwo[0].Path) {
        throw 'Expected exactly one independent owning client for each role.'
    }
    Assert-InOrder $ownerOne[0].Log @(
        'CLIENT_INITIAL: Owner=1 Rifle=30/90 Pistol=12/36',
        'CLIENT_RIFLE_SHOT: Owner=1 Rifle=29/90 Pistol=12/36',
        'CLIENT_EMPTY_FEEDBACK: Item=\S+ Count=1',
        'CLIENT_EMPTY_FEEDBACK: Owner=1 Rifle=0/90 Pistol=12/36',
        'CLIENT_BLOCKED_FIRE_PRESSED: Owner=1 Reloading=1',
        'CLIENT_RIFLE_RELOAD: Owner=1 Rifle=30/60 Pistol=12/36',
        'CLIENT_RIFLE_CANCEL_START: Owner=1 Rifle=20/60 Pistol=12/36',
        'CLIENT_SWITCH_CANCEL: Owner=1 Rifle=20/60 Pistol=12/36',
        'CLIENT_PISTOL_SHOT: Owner=1 Rifle=20/60 Pistol=11/36',
        'CLIENT_PISTOL_RELOAD: Owner=1 Rifle=20/60 Pistol=12/35',
        'CLIENT_DEATH_CANCEL_START: Owner=1 Rifle=20/60 Pistol=5/35',
        'CLIENT_DEATH_CANCEL: Owner=1'
    ) $ownerOne[0].Path
    Assert-InOrder $ownerTwo[0].Log @(
        'CLIENT_INITIAL: Owner=2 Rifle=30/90 Pistol=12/36',
        'CLIENT_RIFLE_SHOT: Owner=2 Rifle=30/90 Pistol=12/36',
        'CLIENT_FULL_RELOAD: Owner=2 Rifle=30/90 Pistol=12/36',
        'CLIENT_PARTIAL_RELOAD: Owner=2 Rifle=28/0 Pistol=12/36',
        'CLIENT_NO_RESERVE: Owner=2 Rifle=28/0 Pistol=12/36',
        'CLIENT_PISTOL_SHOT: Owner=2 Rifle=28/0 Pistol=12/36',
        'CLIENT_DEATH_OBSERVER: Owner=2'
    ) $ownerTwo[0].Path

    Write-Host 'Task 17 PASS: two private owners, full/empty/partial/zero-reserve ammo, server reload and cancellation, owner empty event, pistol semi-auto hit, stale-item rejection, and client/server agreement.'
}
finally {
    foreach ($process in @($server, $clientA, $clientB)) {
        if ($null -ne $process) {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
            }
        }
    }
}
