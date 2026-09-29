[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18798
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
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask14',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 14 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask14Probe FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a Task 14 or Experience failure. See $LogPath"
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

function Assert-OwnerFlow([string]$ServerText, [string]$ClientText,
    [int]$Owner, [string]$ServerLog, [string]$ClientLog) {
    $serverInitial = Assert-One $ServerText `
        "MiniTask14Probe SERVER_PRELOAD: Owner=$Owner Rifle=(\S+) Pistol=(\S+) Count=2" `
        "server preload for owner $Owner" $ServerLog
    $clientInitial = Assert-One $ClientText `
        "MiniTask14Probe CLIENT_SNAPSHOT: Owner=$Owner Rifle=(\S+) Pistol=(\S+) Count=2 Private=1 Stats=1 AuthorityRejected=1" `
        "owner $Owner initial client snapshot" $ClientLog
    if ($clientInitial.Groups[1].Value -ne $serverInitial.Groups[1].Value -or
        $clientInitial.Groups[2].Value -ne $serverInitial.Groups[2].Value) {
        throw "Owner $Owner initial client snapshot differs from server. See $ClientLog"
    }

    $serverChanged = Assert-One $ServerText `
        "MiniTask14Probe SERVER_STATS_CHANGED: Owner=$Owner Rifle=(\S+) Ammo=29 Pistol=(\S+) Count=2" `
        "server Rifle stat change for owner $Owner" $ServerLog
    $clientChanged = Assert-One $ClientText `
        "MiniTask14Probe CLIENT_STATS_CHANGED: Owner=$Owner Rifle=(\S+) Ammo=29 Pistol=(\S+) Count=2 Stable=1" `
        "owner $Owner replicated Rifle stat change" $ClientLog
    if ($serverChanged.Groups[1].Value -ne $serverInitial.Groups[1].Value -or
        $serverChanged.Groups[2].Value -ne $serverInitial.Groups[2].Value -or
        $clientChanged.Groups[1].Value -ne $serverInitial.Groups[1].Value -or
        $clientChanged.Groups[2].Value -ne $serverInitial.Groups[2].Value -or
        $serverChanged.Index -le $serverInitial.Index -or
        $clientChanged.Index -le $clientInitial.Index) {
        throw "Owner $Owner Rifle stat change did not preserve item identities. See $ClientLog"
    }

    $serverRemoved = Assert-One $ServerText `
        "MiniTask14Probe SERVER_REMOVED: Owner=$Owner OldRifle=(\S+) Pistol=(\S+) Count=1" `
        "server removal for owner $Owner" $ServerLog
    $clientRemoved = Assert-One $ClientText `
        "MiniTask14Probe CLIENT_REMOVED: Owner=$Owner OldRifle=(\S+) Pistol=(\S+) Count=1 Stale=0 RemoteDestroyed=1" `
        "owner $Owner replicated removal" $ClientLog
    if ($serverRemoved.Groups[1].Value -ne $serverInitial.Groups[1].Value -or
        $serverRemoved.Groups[2].Value -ne $serverInitial.Groups[2].Value -or
        $clientRemoved.Groups[1].Value -ne $serverInitial.Groups[1].Value -or
        $clientRemoved.Groups[2].Value -ne $serverInitial.Groups[2].Value -or
        $serverRemoved.Index -le $serverChanged.Index -or
        $clientRemoved.Index -le $clientChanged.Index) {
        throw "Owner $Owner removal lost identity or left stale entries. See $ClientLog"
    }

    $serverReadded = Assert-One $ServerText `
        "MiniTask14Probe SERVER_READDED: Owner=$Owner OldRifle=(\S+) NewRifle=(\S+) Pistol=(\S+) Count=2" `
        "server re-add for owner $Owner" $ServerLog
    $clientReadded = Assert-One $ClientText `
        "MiniTask14Probe CLIENT_READDED: Owner=$Owner OldRifle=(\S+) NewRifle=(\S+) Pistol=(\S+) Count=2 Stale=0 Stats=1" `
        "owner $Owner replicated re-add" $ClientLog
    if ($serverReadded.Groups[1].Value -ne $serverInitial.Groups[1].Value -or
        $serverReadded.Groups[2].Value -eq $serverInitial.Groups[1].Value -or
        $serverReadded.Groups[3].Value -ne $serverInitial.Groups[2].Value -or
        $clientReadded.Groups[1].Value -ne $serverInitial.Groups[1].Value -or
        $clientReadded.Groups[2].Value -ne $serverReadded.Groups[2].Value -or
        $clientReadded.Groups[3].Value -ne $serverInitial.Groups[2].Value) {
        throw "Owner $Owner re-add did not create and replicate a new Rifle instance. See $ClientLog"
    }
    [void](Assert-One $ClientText `
        "MiniTask14Probe PASS: Owner=$Owner Initial=2 Removed=1 Readded=2 Private=1 Stale=0" `
        "owner $Owner final PASS" $ClientLog)
    return $serverInitial.Groups[1].Value
}

$serverLog = Join-Path $logDirectory 'Task14-Server.log'
$clientALog = Join-Path $logDirectory 'Task14-ClientA.log'
$clientBLog = Join-Path $logDirectory 'Task14-ClientB.log'
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
        if ($clientAText -match 'MiniTask14Probe PASS: Owner=\d+ Initial=2 Removed=1 Readded=2 Private=1 Stale=0' -and
            $clientBText -match 'MiniTask14Probe PASS: Owner=\d+ Initial=2 Removed=1 Readded=2 Private=1 Stale=0' -and
            [regex]::Matches($serverText, 'MiniTask14Probe SERVER_READDED:').Count -eq 2) {
            break
        }
        Start-Sleep -Milliseconds 500
    }
    $ownerA = Assert-One $clientAText `
        'MiniTask14Probe PASS: Owner=(\d+) Initial=2 Removed=1 Readded=2 Private=1 Stale=0' `
        'ClientA PASS' $clientALog
    $ownerB = Assert-One $clientBText `
        'MiniTask14Probe PASS: Owner=(\d+) Initial=2 Removed=1 Readded=2 Private=1 Stale=0' `
        'ClientB PASS' $clientBLog
    if ($ownerA.Groups[1].Value -eq $ownerB.Groups[1].Value -or
        $ownerA.Groups[1].Value -notin @('1', '2') -or
        $ownerB.Groups[1].Value -notin @('1', '2')) {
        throw 'The two clients did not receive distinct owner-only inventory probes.'
    }
    $initialA = Assert-OwnerFlow $serverText $clientAText `
        ([int]$ownerA.Groups[1].Value) $serverLog $clientALog
    $initialB = Assert-OwnerFlow $serverText $clientBText `
        ([int]$ownerB.Groups[1].Value) $serverLog $clientBLog
    if ($initialA -eq $initialB) {
        throw 'The two clients share a Rifle instance ID.'
    }
    Write-Host 'Task 14 passed: two private owner snapshots, replicated Rifle stat changes, server removal and re-add, unique item identities, no stale inventory references.'
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
