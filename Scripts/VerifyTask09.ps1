[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18793
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
        return ,([string](Get-Content -LiteralPath $Path -Raw))
    }
    return ,([string]::Empty)
}

function Start-Probe([string]$Url, [string]$Role, [string]$LogPath, [string[]]$ExtraArguments) {
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath
    }
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeAbilities',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 09 $Role started: PID=$($process.Id)"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniAbilityProbe (?:RESPAWN_)?FAIL:' -or
        $logText -match 'Experience state .* -> Failed' -or
        $logText -match 'MiniAddAbilities missing AbilitySet') {
        throw "$Role reported an ability or Experience failure. See $LogPath"
    }
    return $logText
}

function Has-Snapshot([string]$Text, [string]$NetMode, [int]$Players,
    [int]$PawnAbilities, [int]$FeatureAbilities, [int]$Effects,
    [int]$ProbeAttributes = -1) {
    if ($ProbeAttributes -lt 0) { $ProbeAttributes = $Players }
    $expected = "MiniAbilityProbe SNAPSHOT: NetMode=$NetMode Stage="
    $pattern = [regex]::Escape($expected) + '\d+ PlayerStates=' + $Players +
        ' PawnAbilities=' + $PawnAbilities + ' FeatureAbilities=' + $FeatureAbilities +
        ' Effects=' + $Effects + ' ProbeAttributes=' + $ProbeAttributes +
        ' HealthSets=' + $Players + ' BoundAvatars=' + $Players + '\b'
    return $Text -match $pattern
}

$serverLog = Join-Path $logDirectory 'Task09-Server.log'
$clientALog = Join-Path $logDirectory 'Task09-ClientA.log'
$clientBLog = Join-Path $logDirectory 'Task09-ClientB.log'
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
        Start-Sleep -Milliseconds 250
    }
    if ($serverText -notmatch $listenerPattern) { throw "Server did not start listening. See $serverLog" }

    $clientA = Start-Probe "127.0.0.1:$Port" 'ClientA' $clientALog @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        $clientAText = Assert-Healthy $clientA 'ClientA' $clientALog
        if ($serverText.Contains('MiniAbilityProbe PASS: PlayerStateASCReused=1 OldPawnCleanupSafe=1') -and
            (Has-Snapshot $serverText 'ListenServer' 2 2 2 2) -and
            (Has-Snapshot $clientAText 'Client' 2 1 1 1) -and
            (Has-Snapshot $clientAText 'Client' 2 1 0 0 0)) { break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $serverText.Contains('MiniAbilityProbe PASS: PlayerStateASCReused=1 OldPawnCleanupSafe=1') -or
        -not (Has-Snapshot $serverText 'ListenServer' 2 2 2 2) -or
        -not (Has-Snapshot $clientAText 'Client' 2 1 1 1) -or
        -not (Has-Snapshot $clientAText 'Client' 2 1 0 0 0)) {
        throw "Two-player ability lifecycle timed out. Logs: $serverLog; $clientALog"
    }
    foreach ($marker in @('BASELINE_PASS:', 'REVOKE_PASS:', 'RESTORE_PASS:', 'RESPAWN_BOUND:', 'PROBE_SUSPENDED:', 'PROBE_RESUMED:')) {
        if (-not $serverText.Contains($marker)) { throw "Missing $marker in $serverLog" }
    }
    Write-Host 'Task 09 two-player grant, revoke, restore, and respawn passed.'

    $clientB = Start-Probe "127.0.0.1:$Port" 'late ClientB' $clientBLog @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        $clientAText = Assert-Healthy $clientA 'ClientA' $clientALog
        $clientBText = Assert-Healthy $clientB 'ClientB' $clientBLog
        if ((Has-Snapshot $serverText 'ListenServer' 3 3 3 3) -and
            (Has-Snapshot $clientAText 'Client' 3 1 1 1) -and
            (Has-Snapshot $clientBText 'Client' 3 1 1 1)) { break }
        Start-Sleep -Milliseconds 250
    }
    if (-not (Has-Snapshot $serverText 'ListenServer' 3 3 3 3) -or
        -not (Has-Snapshot $clientAText 'Client' 3 1 1 1) -or
        -not (Has-Snapshot $clientBText 'Client' 3 1 1 1)) {
        throw "Late-join replication timed out. Logs: $serverLog; $clientALog; $clientBLog"
    }
    Write-Host 'Task 09 late join and owner/simulated-proxy visibility passed.'
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
