[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 360,
    [ValidateRange(1024, 65535)][int]$Port = 18818,
    [switch]$NoMediaAssets,
    [switch]$WithMedia
)

$ErrorActionPreference = 'Stop'
if ($WithMedia -and $NoMediaAssets) {
    throw 'Run WithMedia and NoMediaAssets as separate verification modes.'
}
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
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask18',
        '-unattended', '-nosplash', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    if ($NoMediaAssets) { $arguments += '-MiniProbeTask18NoMediaAssets' }
    if ($WithMedia -and $Role -ne 'listen server') {
        $arguments += @('-MiniProbeTask18Media', '-windowed', '-ResX=960', '-ResY=540', '"-ExecCmds=t.MaxFPS 30"')
    } else {
        $arguments += @('-nullrhi', '-nosound')
    }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 18 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask18Probe FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a Task 18 or Experience failure. See $LogPath"
    }
    return $logText
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

$variant = if ($NoMediaAssets) { '-NoMediaAssets' } elseif ($WithMedia) { '-WithMedia' } else { '' }
$serverLog = Join-Path $logDirectory "Task18$variant-Server.log"
$clientALog = Join-Path $logDirectory "Task18$variant-ClientA.log"
$clientBLog = Join-Path $logDirectory "Task18$variant-ClientB.log"
$screenshots = @(1, 2 | ForEach-Object {
    $owner = $_
    'Reload', 'Death' | ForEach-Object {
        Join-Path $projectRoot "Saved\Screenshots\Task18-Owner$owner-$_.png"
    }
})
$recordings = @(1, 2 | ForEach-Object {
    Join-Path $projectRoot "Saved\Task18Audio\Task18-Owner$_-Combat.wav"
})
if ($WithMedia) {
    foreach ($path in ($screenshots + $recordings)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path
        }
    }
}
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
    $pass = 'MiniTask18Probe SERVER_PASS: OwnerPrediction=1 FireEchoSuppressed=1 ObserverFire=1 HitConfirm=1 Impact=1 Damage=1 ReloadStart=1 ReloadCancel=1 RapidCancel=1 ReloadRecovered=1 DeathReloadCancel=1 Death=1'
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        $clientAText = Assert-Healthy $clientA 'ClientA' $clientALog
        $clientBText = Assert-Healthy $clientB 'ClientB' $clientBLog
        if ($serverText.Contains($pass) -and
            ($clientAText + $clientBText) -match 'MiniTask18Probe CLIENT_DEATH_COUNTS: Owner=1 Death=1 ReloadStop=2' -and
            ($clientAText + $clientBText) -match 'MiniTask18Probe CLIENT_DEATH_COUNTS: Owner=2 Death=1 ReloadStop=2' -and
            (-not $WithMedia -or (@($screenshots + $recordings | Where-Object {
                -not (Test-Path -LiteralPath $_ -PathType Leaf)
            }).Count -eq 0 -and
                ($clientAText + $clientBText) -match 'CLIENT_AUDIO_EXPORTED: Owner=1' -and
                ($clientAText + $clientBText) -match 'CLIENT_AUDIO_EXPORTED: Owner=2'))) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $serverText.Contains($pass)) {
        throw "Task 18 server did not report PASS. See $serverLog"
    }
    Assert-InOrder $serverText @(
        'MiniTask18Probe SERVER_READY: Owners=2',
        'MiniTask18Probe SERVER_CUE_READY: Path=1 AssetProbe=1',
        'MiniTask18Probe SERVER_INITIAL: Ready=1',
        'MiniTask18Probe SERVER_FIRE: Shots=1 Target=75 Rifle=29/90',
        'MiniTask18Probe SERVER_RELOAD_START: Reloading=1 Rifle=20/90',
        'MiniTask18Probe SERVER_RELOAD_CANCEL: Reloading=0 Rifle=20/90 Pistol=1',
        'MiniTask18Probe SERVER_RAPID_RELOAD_CANCEL: SameFrame=1 Reloading=0 PistolMagazine=10 Rifle=1',
        'MiniTask18Probe SERVER_RELOAD_RECOVERED: Reloading=1 Rifle=20/90',
        'MiniTask18Probe SERVER_DEATH_TRIGGERED: Damage=150',
        [regex]::Escape($pass)
    ) $serverLog

    $clients = @(@{ Path = $clientALog; Log = $clientAText },
                 @{ Path = $clientBLog; Log = $clientBText })
    $ownerOne = @($clients | Where-Object { $_.Log -match 'MiniTask18Probe CLIENT_INITIAL: Owner=1' })
    $ownerTwo = @($clients | Where-Object { $_.Log -match 'MiniTask18Probe CLIENT_INITIAL: Owner=2' })
    if ($ownerOne.Count -ne 1 -or $ownerTwo.Count -ne 1 -or $ownerOne[0].Path -eq $ownerTwo[0].Path) {
        throw 'Expected exactly one independent owning client for each role.'
    }
    Assert-InOrder $ownerOne[0].Log @(
        'CLIENT_CUE_READY: Owner=1 Path=1 AssetProbe=1',
        'CLIENT_INITIAL: Owner=1',
        'CLIENT_FIRE_COUNTS: Owner=1 Predicted=1 Presented=1 Confirmed=1 Suppressed=1 Observer=0 Impact=1 Damage=1 HitConfirm=1',
        'CLIENT_RELOAD_START_COUNTS: Owner=1 Start=1 Stop=0',
        'CLIENT_RELOAD_CANCEL_COUNTS: Owner=1 Start=1 Stop=1 RifleUnchanged=1',
        'CLIENT_RAPID_RELOAD_CANCEL_COUNTS: Owner=1 Start=1 Stop=1 Orphan=0',
        'CLIENT_RELOAD_RECOVERED_COUNTS: Owner=1 Start=2 Stop=1',
        'CLIENT_DEATH_COUNTS: Owner=1 Death=1 ReloadStop=2'
    ) $ownerOne[0].Path
    Assert-InOrder $ownerTwo[0].Log @(
        'CLIENT_CUE_READY: Owner=2 Path=1 AssetProbe=1',
        'CLIENT_INITIAL: Owner=2',
        'CLIENT_FIRE_COUNTS: Owner=2 Predicted=0 Presented=1 Confirmed=1 Suppressed=0 Observer=1 Impact=1 Damage=1 HitConfirm=0',
        'CLIENT_RELOAD_START_COUNTS: Owner=2 Start=1 Stop=0',
        'CLIENT_RELOAD_CANCEL_COUNTS: Owner=2 Start=1 Stop=1 RifleUnchanged=-1',
        'CLIENT_RAPID_RELOAD_CANCEL_COUNTS: Owner=2 Start=1 Stop=1 Orphan=0',
        'CLIENT_RELOAD_RECOVERED_COUNTS: Owner=2 Start=2 Stop=1',
        'CLIENT_DEATH_COUNTS: Owner=2 Death=1 ReloadStop=2'
    ) $ownerTwo[0].Path
    if ($NoMediaAssets) {
        foreach ($owner in 1, 2) {
            $entry = if ($owner -eq 1) { $ownerOne[0] } else { $ownerTwo[0] }
            if ($entry.Log -notmatch "CLIENT_NO_MEDIA_ASSETS: Owner=$owner Sounds=0 Montages=0 Damage=25 Death=1") {
                throw "Owner $owner did not confirm combat without media assets. See $($entry.Path)"
            }
        }
    }
    if ($WithMedia) {
        foreach ($owner in 1, 2) {
            $entry = if ($owner -eq 1) { $ownerOne[0] } else { $ownerTwo[0] }
            if (-not $NoMediaAssets) {
                Assert-InOrder $entry.Log @(
                    "CLIENT_MEDIA_FIRE: Owner=$owner ShooterSounds=[2-9][0-9]* TargetSounds=[1-9][0-9]* Montages=1",
                    "CLIENT_MEDIA_RELOAD: Owner=$owner Sounds=[3-9][0-9]* Montages=2",
                    "CLIENT_MEDIA_DEATH: Owner=$owner Sounds=[5-9][0-9]* Screenshot="
                ) $entry.Path
            }
        }
        foreach ($path in $screenshots) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-Item -LiteralPath $path).Length -lt 1024) {
                throw "Rendered Task 18 screenshot is missing or empty: $path"
            }
        }
        $python = Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\Python3\Win64\python.exe'
        foreach ($path in $recordings) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Game audio mix recording is missing: $path"
            }
            & $python (Join-Path $PSScriptRoot 'Task18InspectAudio.py') $path
            if ($LASTEXITCODE -ne 0) { throw "Game audio mix is silent or invalid: $path" }
        }
        Write-Host 'Task 18 media PASS: audio mix contains non-silent PCM, montage playback succeeded, and four reload/death screenshots were saved.'
    }
    Write-Host 'Task 18 PASS: owner prediction/echo suppression, observer fire, confirmed hit/impact/damage, reload cancellation, same-frame cancel without orphan cue, recovered reload and death cleanup.'
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
