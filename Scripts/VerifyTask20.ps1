[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 420,
    [ValidateRange(1024, 65535)][int]$Port = 18920,
    [switch]$WithMedia,
    [switch]$SkipTravel
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if ($PackagedExe) { $editor = [System.IO.Path]::GetFullPath($PackagedExe) }
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) { throw 'FPS.uproject is missing.' }
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Missing game runner: $editor" }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$prefix = if ($PackagedExe) { 'Task20-Packaged' } else { 'Task20' }
if ($WithMedia) { $prefix += '-WithMedia' }
New-Item -ItemType Directory -Path $logs, $cache, (Join-Path $projectRoot 'Saved\Screenshots') -Force | Out-Null
$screenshots = @('Initial', 'Disabled', 'Refill' | ForEach-Object {
    Join-Path $projectRoot "Saved\Screenshots\Task20-Owner1-$_.png"
})
if ($WithMedia) {
    foreach ($path in $screenshots) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
    }
}

function Start-Probe([string]$Url, [string]$Role) {
    $log = Join-Path $logs "$prefix-$Role.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if ($PackagedExe) { $arguments += $Url }
    else { $arguments += @(('"{0}"' -f $projectFile), $Url, '-game') }
    $arguments += @(
        '-MiniProbeTask20', '-unattended', '-nosplash', '-nop4',
        '-ddc=InstalledNoZenLocalFallback', ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log)
    )
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    if ($WithMedia -and $Role -ne 'Server') {
        $arguments += @('-MiniProbeTask20Media', ('"-MiniProbeTask20MediaOutput={0}"' -f (Join-Path $projectRoot 'Saved\Screenshots')),
            '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 20 $Role started: PID=$($process.Id), log=$log"
    return @{ Process = $process; Log = $log; Role = $Role }
}

function Read-Healthy($Entry) {
    $Entry.Process.Refresh()
    if ($Entry.Process.HasExited) { throw "$($Entry.Role) exited early: $($Entry.Log)" }
    $text = if (Test-Path -LiteralPath $Entry.Log -PathType Leaf) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask20Probe FAIL:|MiniAddActors (FAILED|INVALID_SCOPE):|MiniPracticeTarget INVALID_DEFINITION:|Experience state .* -> Failed|Fatal error:|Assertion failed:') {
        throw "$($Entry.Role) reported a gameplay/Experience failure: $($Entry.Log)"
    }
    return $text
}

function Assert-InOrder([string]$Log, [string[]]$Markers, [string]$Path) {
    $previous = -1
    foreach ($marker in $Markers) {
        $match = [regex]::Match($Log, $marker)
        if (-not $match.Success -or $match.Index -le $previous) { throw "Missing/out-of-order '$marker': $Path" }
        $previous = $match.Index
    }
}

$entries = @()
try {
    $server = Start-Probe '/Game/Mini/Maps/L_MiniPractice?listen' 'Server'
    $entries += $server
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $listener = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
    do {
        $serverText = Read-Healthy $server
        if ($serverText -match $listener) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($serverText -notmatch $listener) { throw "Listen server never opened port $Port" }
    $clientA = Start-Probe "127.0.0.1:$Port" 'ClientA'; $entries += $clientA
    $clientB = Start-Probe "127.0.0.1:$Port" 'ClientB'; $entries += $clientB
    $pass = 'MiniTask20Probe SERVER_PASS: Clients=2 Production=1 Targets=3 Supply=1 Rifle25=4 DisabledDuplicate=0 Reset2s=1 Pistol20=1 Reload=1 Rejections=1 RealSupplyOverlap=1 GUIDs=1 PrivateHUD=1 ObserverHit=0 CoreOwnsTargets=0'
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $serverText = Read-Healthy $server
        $clientAText = Read-Healthy $clientA
        $clientBText = Read-Healthy $clientB
        $combined = $clientAText + $clientBText
        $screenshotsReady = -not $WithMedia -or @($screenshots | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -eq 0
        if ($serverText.Contains($pass) -and $combined.Contains('CLIENT_CORE_OFF_TARGETS_RETAINED: Owner=1') -and
            $combined.Contains('CLIENT_CORE_OFF_TARGETS_RETAINED: Owner=2') -and $screenshotsReady) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $serverText.Contains($pass)) { throw "Task 20 did not PASS: $($server.Log)" }
    Assert-InOrder $serverText @(
        'SERVER_READY: Clients=2 HostHUD=1 Targets=3 Supplies=1 ProductionAbilities=1 ActionSetOwned=1 Rifle=30/90 Pistol=12/36',
        'SERVER_RIFLE_HIT: Number=1 Health=75 Damage=25 Kind=PracticeTarget PlayerKill=0',
        'SERVER_RIFLE_HIT: Number=2 Health=50 Damage=25 Kind=PracticeTarget PlayerKill=0',
        'SERVER_RIFLE_HIT: Number=3 Health=25 Damage=25 Kind=PracticeTarget PlayerKill=0',
        'SERVER_RIFLE_HIT: Number=4 Health=0 Damage=25 Kind=PracticeTarget PlayerKill=0',
        'SERVER_DISABLED_SHOT: Accepted=1 Damage=0 TargetDisableCount=1 PlayerDeath=0',
        'SERVER_RESET: Health=100 Enabled=1 DisableCount=1 ResetCount=1 Delay=',
        'SERVER_PISTOL_HIT: Health=80 Damage=20 Ammo=11/36 Kind=PracticeTarget',
        'SERVER_RELOAD: SawReloading=1 Ammo=12/35 AcceptedShots=6',
        'SERVER_REJECTIONS: OldSequence=1 WrongGUID=1 InvalidView=1 FireRate=1 Empty=1 InvalidAccepted=0 SafetyMiss=1',
        'SERVER_SUPPLY_EMPTY: Rifle=0/0 Pistol=0/0 OutsideTryRefill=0 GUIDsRetained=1',
        'SERVER_SUPPLY_REFILL: RealOverlap=1 Rifle=30/90 Pistol=12/36 GUIDsRetained=1 RefillDelta=1',
        'SERVER_SUPPLY_OUTSIDE: Rifle=0/0 Pistol=0/0 OutsideTryRefill=0 TimerRefill=0',
        'SERVER_CORE_OFF: RealDeactivation=1 Targets=3 Supplies=1 ActionSetOwnershipRetained=1',
        [regex]::Escape($pass)
    ) $server.Log
    $clients = @(@{ Log = $clientAText; Path = $clientA.Log }, @{ Log = $clientBText; Path = $clientB.Log })
    foreach ($owner in 1, 2) {
        $matched = @($clients | Where-Object { $_.Log.Contains("CLIENT_INITIAL: Owner=$owner") })
        if ($matched.Count -ne 1) { throw "Expected exactly one independent local process for Owner $owner" }
        $entry = $matched[0]
        $markers = @("CLIENT_INITIAL: Owner=$owner")
        foreach ($number in 1, 2, 3, 4) {
            $health = 100 - $number * 25
            $hits = if ($owner -eq 1) { $number } else { 0 }
            $enabled = if ($number -eq 4) { 0 } else { 1 }
            $markers += "CLIENT_TARGET_STATE: Owner=$owner Number=$number Health=$health Enabled=$enabled HitConfirms=$hits PlayerKills=0"
            $markers += "CLIENT_RIFLE_${number}: Owner=$owner"
        }
        $markers += @(
            "CLIENT_DISABLED_NO_DUPLICATE: Owner=$owner", "CLIENT_RESET: Owner=$owner", "CLIENT_PISTOL_SWITCH: Owner=$owner",
            "CLIENT_PISTOL_20: Owner=$owner", "CLIENT_RELOAD_START: Owner=$owner", "CLIENT_RELOAD_COMPLETE: Owner=$owner",
            "CLIENT_REJECTIONS_NO_EXTRA_HIT: Owner=$owner", "CLIENT_SUPPLY_EMPTY: Owner=$owner",
            "CLIENT_SUPPLY_REFILLED_GUIDS: Owner=$owner", "CLIENT_SUPPLY_LEFT: Owner=$owner", "CLIENT_SUPPLY_OUTSIDE_EMPTY: Owner=$owner"
        )
        $hits = if ($owner -eq 1) { 5 } else { 0 }
        $defeats = if ($owner -eq 1) { 1 } else { 0 }
        $markers += "CLIENT_CONFIRM_COUNTS: Owner=$owner LegacyHits=$hits PlayerKills=0 DamageConfirms=$hits TargetDefeats=$defeats ObserverHit=0"
        $markers += "CLIENT_CORE_OFF_TARGETS_RETAINED: Owner=$owner"
        Assert-InOrder $entry.Log $markers $entry.Path
    }
    if ($WithMedia) {
        foreach ($path in $screenshots) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -lt 1024) { throw "Missing screenshot: $path" }
        }
        Write-Host 'Task 20 rendered evidence saved under Saved\Screenshots. Inspect Initial, Disabled and Refill before claiming visible presentation quality.'
    }
    Write-Host 'Task 20 core PASS: real listen server and two independent clients; production loadout, target damage/reset, confirmation isolation, reload/security checks, actual supply overlap, GUID preservation and private HUD.'
} finally {
    foreach ($entry in $entries) {
        $entry.Process.Refresh()
        if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
    }
}
if (-not $SkipTravel) {
    & (Join-Path $PSScriptRoot 'VerifyTask20Travel.ps1') -EngineRoot $EngineRoot -PackagedExe $PackagedExe -TimeoutSeconds $TimeoutSeconds
}
