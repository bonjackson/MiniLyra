[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateRange(60, 600)][int]$TimeoutSeconds = 150,
    [ValidateRange(1024, 65535)][int]$Port = 18921,
    [switch]$WithMedia,
    [switch]$SkipStandalone
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else {
    Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
}
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$prefix = if ($PackagedExe) { 'Task21-Packaged' } else { 'Task21' }
if ($WithMedia) { $prefix += '-WithMedia' }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$screenshots = Join-Path $projectRoot 'Saved\Screenshots'
New-Item -ItemType Directory -Path $logs, $cache, $screenshots -Force | Out-Null
if ($WithMedia) {
    foreach ($owner in 1, 2) {
        $path = Join-Path $screenshots "Task21-Owner$owner-Playing.png"
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
    }
}

function Start-Task21Probe([string]$Url, [string]$Role, [string]$Mode = 'Core') {
    $log = Join-Path $logs "$prefix-$Role.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f $projectFile) }
    $arguments += $Url
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @(
        "-MiniProbeTask21=$Mode", '-unattended', '-nosplash', '-nop4',
        '-ddc=InstalledNoZenLocalFallback', ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log)
    )
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    if ($WithMedia -and $Role -in 'ClientA', 'ClientB') {
        $arguments += @('-MiniProbeTask21Media', ('"-MiniProbeTask21MediaOutput={0}"' -f $screenshots),
            '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 21 $Role started: PID=$($process.Id), log=$log"
    return @{ Process = $process; Log = $log; Role = $Role }
}

function Read-Task21Healthy($Entry) {
    $Entry.Process.Refresh()
    if ($Entry.Process.HasExited) { throw "$($Entry.Role) exited early: $($Entry.Log)" }
    $text = if (Test-Path -LiteralPath $Entry.Log -PathType Leaf) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask21Probe FAIL:|MiniArena.*(INVALID|FAILED):|Experience state .* -> Failed|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID') {
        throw "$($Entry.Role) failed: $($Entry.Log)"
    }
    return $text
}

function Assert-Task21Order([string]$Text, [string[]]$Markers, [string]$Path) {
    $previous = -1
    foreach ($marker in $Markers) {
        $match = [regex]::Match($Text, $marker)
        if (-not $match.Success -or $match.Index -le $previous) { throw "Missing/out-of-order '$marker': $Path" }
        $previous = $match.Index
    }
}

$entries = @()
try {
    $server = Start-Task21Probe '/Game/Mini/Maps/L_MiniPractice?listen?Experience=DA_MiniArenaDiagnosticsExperience' 'Server'
    $entries += $server
    # Boot the early client alongside the host so its renderer startup does
    # not consume the short diagnostic Warmup. UDP connection retries wait
    # for the host port; the production phase timer remains unchanged.
    $clientA = Start-Task21Probe "127.0.0.1:$Port" 'ClientA'
    $entries += $clientA
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $listener = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
    do {
        $serverText = Read-Task21Healthy $server
        if ($serverText -match $listener) { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($serverText -notmatch $listener) { throw "Listen server did not open port $Port" }
    do {
        $serverText = Read-Task21Healthy $server
        $clientAText = Read-Task21Healthy $clientA
        if ($serverText.Contains('MiniTask21Probe SERVER_PLAYING: LateJoinNow=1')) { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $serverText.Contains('MiniTask21Probe SERVER_PLAYING: LateJoinNow=1')) { throw 'Playing did not start.' }
    $clientB = Start-Task21Probe "127.0.0.1:$Port" 'ClientB'
    $entries += $clientB
    $pass = 'MiniTask21Probe CORE_PASS: Listen=1 Clients=2 LateJoinPlaying=1 SameDeadline=1 ClockErrorLE1=1 ClientMutationsRejected=1 Order=1 Durations=1 IndependentASC=1 PlayerPhaseSpecs=0 RealActionRevoke=1 ClientRulesGone=1 Specs=0 Timer=0 OldDeadlinePassed=1'
    do {
        $serverText = Read-Task21Healthy $server
        $clientAText = Read-Task21Healthy $clientA
        $clientBText = Read-Task21Healthy $clientB
        if ($serverText.Contains($pass)) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $serverText.Contains($pass)) { throw "Core did not PASS: $($server.Log)" }
    Assert-Task21Order $serverText @(
        'SERVER_READY:', 'SERVER_PHASE: Index=1 ', 'SERVER_JOIN: Owner=1 PhaseIndex=1 ',
        'SERVER_PHASE: Index=2 ', 'SERVER_PLAYING: LateJoinNow=1 ', 'SERVER_JOIN: Owner=2 PhaseIndex=2 ',
        'SERVER_PHASE: Index=3 ', 'SERVER_CYCLE:', 'SERVER_ACTION_REVOKED:', [regex]::Escape($pass)
    ) $server.Log
    foreach ($owner in 1, 2) {
        $text = if ($owner -eq 1) { $clientAText } else { $clientBText }
        $path = if ($owner -eq 1) { $clientA.Log } else { $clientB.Log }
        $markers = @("CLIENT_MUTATIONS_REJECTED: Owner=$owner ")
        if ($owner -eq 1) { $markers += "CLIENT_CHECKPOINT: Owner=1 Checkpoint=0 " }
        foreach ($checkpoint in 1, 2, 3, 4, 5) { $markers += "CLIENT_CHECKPOINT: Owner=$owner Checkpoint=$checkpoint " }
        Assert-Task21Order $text $markers $path
        if ($owner -eq 2 -and $text.Contains('CLIENT_CHECKPOINT: Owner=2 Checkpoint=0 ')) { throw 'Late client incorrectly replayed Warmup.' }
        if ($WithMedia) {
            $image = Join-Path $screenshots "Task21-Owner$owner-Playing.png"
            if (-not (Test-Path -LiteralPath $image -PathType Leaf) -or (Get-Item -LiteralPath $image).Length -lt 1024) {
                throw "Missing rendered HUD screenshot: $image"
            }
        }
    }
    Write-Host 'Task 21 Core PASS: actual listen server, early client and Playing late join, same deadline, synchronized clocks, rejected client mutation APIs, phase order/timing, active Action revoke and network component removal.'
    if ($WithMedia) { Write-Host "Playing owner/late-join HUD screenshots: $screenshots. Inspect both images before claiming visual quality." }
} finally {
    foreach ($entry in $entries) {
        $entry.Process.Refresh()
        if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
    }
}

if (-not $SkipStandalone) {
    & (Join-Path $PSScriptRoot 'VerifyTask21Standalone.ps1') -EngineRoot $EngineRoot -PackagedExe $PackagedExe -TimeoutSeconds $TimeoutSeconds
}
