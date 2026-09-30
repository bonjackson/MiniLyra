[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 420,
    [ValidateRange(1024, 65535)][int]$Port = 18919,
    [switch]$WithMedia
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw 'FPS.uproject is missing beside the Scripts directory.'
}
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Missing editor: $editor" }
$logs = Join-Path $projectRoot 'Saved\Logs'
$logPrefix = if ($WithMedia) { 'Task19-WithMedia' } else { 'Task19' }
$cache = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logs, $cache -Force | Out-Null
$screenshots = @(1, 2 | ForEach-Object {
    $owner = $_
    'HUD', 'Menu', 'FeatureOff' | ForEach-Object {
        Join-Path $projectRoot "Saved\Screenshots\Task19-Owner$owner-$_.png"
    }
})
if ($WithMedia) {
    foreach ($path in $screenshots) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
    }
}

function Start-Probe([string]$Url, [string]$Role) {
    $log = Join-Path $logs "$logPrefix-$Role.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask19',
        '-unattended', '-nosplash', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log)
    )
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    if ($WithMedia) {
        $arguments += @('-MiniProbeTask19Media', '-windowed', '-ResX=960', '-ResY=540', '"-ExecCmds=t.MaxFPS 30"')
    } else {
        $arguments += @('-nullrhi', '-nosound')
    }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 19 $Role started: PID=$($process.Id), log=$log"
    return @{ Process = $process; Log = $log; Role = $Role }
}

function Read-Healthy($Entry) {
    $Entry.Process.Refresh()
    if ($Entry.Process.HasExited) { throw "$($Entry.Role) exited early: $($Entry.Log)" }
    $log = if (Test-Path -LiteralPath $Entry.Log -PathType Leaf) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($log -match 'MiniTask19Probe FAIL:|Experience state .* -> Failed|MiniAddWidgets (LOAD_FAILED|INVALID_CLASS_OR_TAG|NO_LAYER|NO_EXTENSION_SUBSYSTEM):|Fatal error:|Assertion failed:') {
        throw "$($Entry.Role) reported a UI/Experience failure: $($Entry.Log)"
    }
    return $log
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
    $clientA = Start-Probe "127.0.0.1:$Port" 'ClientA'
    $entries += $clientA
    $clientB = Start-Probe "127.0.0.1:$Port" 'ClientB'
    $entries += $clientB
    $pass = 'MiniTask19Probe SERVER_PASS: OwnSnapshots=1 HostAuthorityRefresh=1 LateHUD=1 MenuStack=1 RootLifecycle=1 RespawnRebind=1 ObserverHit=0 FeatureDeactivated=1 Cleanup=1'
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $serverText = Read-Healthy $server
        $clientAText = Read-Healthy $clientA
        $clientBText = Read-Healthy $clientB
        $combined = $clientAText + $clientBText
        $screenshotsReady = -not $WithMedia -or @($screenshots | Where-Object {
            -not (Test-Path -LiteralPath $_ -PathType Leaf)
        }).Count -eq 0
        if ($serverText.Contains($pass) -and $combined.Contains('CLIENT_FEATURE_REAL_CLEANUP: Owner=1') -and
            $combined.Contains('CLIENT_FEATURE_REAL_CLEANUP: Owner=2') -and $screenshotsReady) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $serverText.Contains($pass)) { throw "Task 19 server did not PASS: $($server.Log)" }
    Assert-InOrder $serverText @(
        'SERVER_READY: Clients=2 HostHUD=1',
        'HOST_SNAPSHOT: Health=100 Slot=0 Ammo=30/90 OwnPawn=1',
        'HOST_AUTHORITY_CHANGE: Health=90 Slot=0 Ammo=27/84 Immediate=1 RepNotifyNeeded=0',
        'HOST_AUTHORITY_PASS: LocalRefresh=1 RemoteIsolation=1',
        'SERVER_LATE_STATE: Health=65 Pistol=7/19',
        'SERVER_MENU_BLOCK: ExtraShots=0',
        'FEATURE_DEACTIVATED: RealSubsystem=1',
        'HOST_FEATURE_OFF_ROOT_RECREATED: Viewport=1 HUDs=0 ActionCounts=0 InputBlocks=0',
        'HOST_FEATURE_OFF: Widgets=0 Listeners=0 InputBlocks=0',
        [regex]::Escape($pass)
    ) $server.Log
    $clients = @(@{ Log = $clientAText; Path = $clientA.Log }, @{ Log = $clientBText; Path = $clientB.Log })
    foreach ($owner in 1, 2) {
        $matched = @($clients | Where-Object { $_.Log.Contains("CLIENT_INITIAL_SNAPSHOT: Owner=$owner") })
        if ($matched.Count -ne 1) { throw "Expected one independent local client for Owner $owner" }
        $entry = $matched[0]
        $death = if ($owner -eq 1) { 'DEATH_SNAPSHOT' } else { 'DEATH_ISOLATED' }
        $respawn = if ($owner -eq 1) { 'RESPAWN_NEW_PAWN' } else { 'RESPAWN_PEER_ISOLATED' }
        $markers = @(
            "CLIENT_INITIAL_SNAPSHOT: Owner=$owner",
            "CLIENT_HOST_AUTHORITY_ISOLATED: Owner=$owner Health=100 Slot=0 Ammo=30/90",
            "CLIENT_HOST_AUTHORITY_ISOLATION: Owner=$owner",
            "CLIENT_HIT_ISOLATION: Owner=$owner HitMessages=$(if ($owner -eq 1) { 1 } else { 0 })",
            "CLIENT_FIRE_SNAPSHOT: Owner=$owner",
            "CLIENT_SWITCH_SNAPSHOT: Owner=$owner",
            "CLIENT_LATE_HUD_REMOVED: Owner=$owner",
            "CLIENT_STOPPED_LISTENERS_SILENT: Owner=$owner",
            "CLIENT_LATE_HUD_FRESH_SNAPSHOT: Owner=$owner",
            "CLIENT_REAL_MENU_BLOCKED: Owner=$owner",
            "CLIENT_MENU_GAMEPLAY_RESTORED: Owner=$owner",
            "CLIENT_REAL_ROOT_RELEASED: Owner=$owner",
            "CLIENT_REAL_ROOT_RECREATED: Owner=$owner"
        )
        if ($owner -eq 1) { $markers += 'CLIENT_DEATH_UI_GATE: GameplayActive=0 Bindings=0 HeldInput=0' }
        $markers += @(
            "CLIENT_${death}: Owner=$owner",
            "CLIENT_${respawn}: Owner=$owner",
            "CLIENT_FEATURE_OWNED_MENU_OPEN: Owner=$owner",
            'FEATURE_DEACTIVATED: RealSubsystem=1',
            "CLIENT_FEATURE_OFF_ROOT_RECREATED: Owner=$owner Viewport=1 HUDs=0 ActionCounts=0 InputBlocks=0",
            "CLIENT_FEATURE_COUNTS: Owner=$owner Widgets=0 VM=0 Bindings=0 Listeners=0 InputBlocks=0 PendingLoads=0",
            "CLIENT_RETAINED_MENU_STOPPED: Owner=$owner Activated=0 InputBlocks=0",
            "CLIENT_FEATURE_REAL_CLEANUP: Owner=$owner"
        )
        Assert-InOrder $entry.Log $markers $entry.Path
    }
    if ($WithMedia) {
        foreach ($path in $screenshots) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -lt 1024) {
                throw "Rendered UI screenshot missing/empty: $path"
            }
        }
        Write-Host 'Task 19 media evidence: six HUD/Menu/FeatureOff screenshots saved under Saved\Screenshots; visually inspect them before claiming visible UI quality.'
    }
    Write-Host 'Task 19 PASS: three local HUDs, immediate listen-host authority refresh, remote isolation, snapshots/weapon/health, confirmed-hit isolation, late HUD, real Menu block/close, actual Root release/rebuild, death-gate protection, respawn, real GameFeature deactivation and retained widget/listener/input cleanup.'
} finally {
    foreach ($entry in $entries) {
        $entry.Process.Refresh()
        if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
    }
}
