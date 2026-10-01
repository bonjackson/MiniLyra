[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateSet('All', 'Score', 'TimeDraw', 'TimeWin', 'Logout', 'Revoke', 'Legacy')]
    [string]$Mode = 'All',
    [ValidateRange(90, 900)][int]$TimeoutSeconds = 240,
    [ValidateRange(1024, 65529)][int]$Port = 18922,
    [switch]$WithMedia
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else {
    Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
}
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$screenshots = Join-Path $projectRoot 'Saved\Screenshots'
$signals = Join-Path $projectRoot 'Saved\Task22ProbeSignals'
New-Item -ItemType Directory -Path $logs, $cache, $screenshots, $signals -Force | Out-Null
$prefix = if ($PackagedExe) { 'Task22-Packaged' } else { 'Task22' }
if ($WithMedia) { $prefix += '-WithMedia' }

function Start-Task22Process {
    param([string]$Url, [string]$Role, [string]$ProbeMode, [int]$ProbePort,
        [string]$DeferredUrl = '', [string]$JoinSignal = '')
    $log = Join-Path $logs "$prefix-$ProbeMode-$Role.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f $projectFile) }
    $arguments += $Url
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @(
        "-MiniProbeTask22=$ProbeMode", "-MiniTask22TimeoutSeconds=$TimeoutSeconds",
        '-unattended', '-nosplash', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log)
    )
    if ($Role -eq 'Server') { $arguments += "-port=$ProbePort" }
    if ($DeferredUrl) {
        $arguments += @("-MiniTask22DeferredURL=$DeferredUrl", ('"-MiniTask22JoinSignal={0}"' -f $JoinSignal))
    }
    if ($WithMedia -and $Role -like 'Client*') {
        $arguments += @('-MiniProbeTask22Media', ('"-MiniProbeTask22MediaOutput={0}"' -f $screenshots),
            '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 22 $ProbeMode $Role started: PID=$($process.Id), log=$log"
    return @{ Process = $process; Log = $log; Role = $Role; AllowExit = $false }
}

function Read-Task22Healthy($Entry) {
    $Entry.Process.Refresh()
    $text = if (Test-Path -LiteralPath $Entry.Log -PathType Leaf) {
        [string](Get-Content -LiteralPath $Entry.Log -Raw)
    } else { '' }
    if ($text -match 'MiniTask22Probe FAIL:|MiniMatch.*(REJECTED|FAILED):|Experience state .* -> Failed|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID') {
        throw "$($Entry.Role) failed: $($Entry.Log)"
    }
    if ($Entry.Process.HasExited -and -not $Entry.AllowExit) { throw "$($Entry.Role) exited early: $($Entry.Log)" }
    return $text
}

function Wait-Task22Marker($Watched, $Entries, [string]$Marker, [DateTime]$Deadline) {
    do {
        foreach ($entry in $Entries) { $null = Read-Task22Healthy $entry }
        $text = Read-Task22Healthy $Watched
        if ($text.Contains($Marker)) { return $text }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Missing '$Marker': $($Watched.Log)"
}

function Assert-Task22Markers([string]$Text, [string[]]$Markers, [string]$Path) {
    $previous = -1
    foreach ($marker in $Markers) {
        $match = [regex]::Match($Text, [regex]::Escape($marker))
        if (-not $match.Success -or $match.Index -le $previous) { throw "Missing/out-of-order '$marker': $Path" }
        $previous = $match.Index
    }
}

function Invoke-Task22Network([string]$Scenario, [int]$ScenarioPort) {
    $entries = @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $experience = if ($Scenario -eq 'Score') { 'DA_MiniArenaExperience' } else { 'DA_MiniFFADiagnosticsExperience' }
    $signal = Join-Path $signals "$prefix-$Scenario-Join.txt"
    if (Test-Path -LiteralPath $signal -PathType Leaf) { Remove-Item -LiteralPath $signal }
    try {
        $server = Start-Task22Process "/Game/Mini/Maps/L_MiniPractice?listen?Experience=$experience" 'Server' $Scenario $ScenarioPort
        $entries += $server
        if ($Scenario -eq 'Score') {
            # Warm the result client's assets before the five-second production result.
            # It does not connect to the host until SERVER_FROZEN creates this signal.
            $clientB = Start-Task22Process '/Game/Mini/Maps/L_MiniPractice?Experience=DA_MiniPracticeExperience' 'ClientB' $Scenario $ScenarioPort "127.0.0.1:$ScenarioPort" $signal
            $entries += $clientB
        }
        $null = Wait-Task22Marker $server $entries 'MiniTask22Probe SERVER_SINGLE_WAIT_PASS:' $deadline
        $clientA = Start-Task22Process "127.0.0.1:$ScenarioPort" 'ClientA' $Scenario $ScenarioPort
        $entries += $clientA
        $null = Wait-Task22Marker $server $entries 'MiniTask22Probe SERVER_PLAYING:' $deadline
        if ($Scenario -eq 'TimeDraw') {
            $clientB = Start-Task22Process "127.0.0.1:$ScenarioPort" 'ClientB' $Scenario $ScenarioPort
            $entries += $clientB
        }
        if ($Scenario -eq 'Score') {
            $null = Wait-Task22Marker $server $entries 'MiniTask22Probe SERVER_FROZEN:' $deadline
            [IO.File]::WriteAllText($signal, 'Join the real server now.')
        }
        if ($Scenario -eq 'Logout') {
            # The owner RPC executes the engine's real disconnect command.
            $clientA.AllowExit = $true
            $null = Wait-Task22Marker $server $entries 'MiniTask22Probe SERVER_WAITING_AGAIN:' $deadline
            $clientB = Start-Task22Process "127.0.0.1:$ScenarioPort" 'ClientB' $Scenario $ScenarioPort
            $entries += $clientB
        }
        $pass = "MiniTask22Probe $($Scenario.ToUpperInvariant())_PASS:"
        $serverText = Wait-Task22Marker $server $entries $pass $deadline
        Assert-Task22Markers $serverText @('SERVER_READY:', 'SERVER_SINGLE_WAIT_PASS:', 'SERVER_PLAYING:', $pass) $server.Log
        $clientAText = Read-Task22Healthy $clientA
        if (-not $clientAText.Contains('CLIENT_CHECKPOINT: Owner=1 Checkpoint=Playing ')) {
            throw "First client's replicated round/HUD acknowledgement missing: $($clientA.Log)"
        }
        switch ($Scenario) {
            'Score' {
                Assert-Task22Markers $serverText @('SERVER_GE_DEATH: Cause=1 ', 'SERVER_GE_DEATH: Cause=2 ',
                    'SERVER_GE_DEATH: Cause=0 ', 'SERVER_FROZEN:', 'SERVER_CLIENT_ACK: Owner=2 Checkpoint=FrozenResult ', $pass) $server.Log
                $clientBText = Read-Task22Healthy $clientB
                if (-not $clientBText.Contains('CLIENT_CHECKPOINT: Owner=2 Checkpoint=FrozenResult ') -or
                    $clientBText.Contains('CLIENT_CHECKPOINT: Owner=2 Checkpoint=Playing ')) {
                    throw "Result late join replayed Playing or missed frozen result/HUD: $($clientB.Log)"
                }
                $combatCount = [regex]::Matches($serverText, 'SERVER_GE_DEATH: Cause=0 ').Count
                if ($combatCount -ne 10) { throw "Expected exactly ten genuine combat GE deaths, got $combatCount." }
            }
            'TimeDraw' {
                if (-not $clientAText.Contains('Checkpoint=TimeoutResult ')) { throw 'Draw result did not reach client HUD.' }
                $clientBText = Read-Task22Healthy $clientB
                Assert-Task22Markers $clientBText @('CLIENT_CHECKPOINT: Owner=2 Checkpoint=LatePlaying ',
                    'CLIENT_CHECKPOINT: Owner=2 Checkpoint=TimeoutResult ') $clientB.Log
            }
            'TimeWin' {
                Assert-Task22Markers $serverText @('SERVER_GE_DEATH: Cause=0 ', 'SERVER_RESPAWN:',
                    'SERVER_CLIENT_ACK: Owner=1 Checkpoint=TimeoutResult ', $pass) $server.Log
            }
            'Logout' {
                Assert-Task22Markers $serverText @('SERVER_GE_DEATH:', 'SERVER_WAITING_AGAIN:',
                    'SERVER_CLIENT_ACK: Owner=2 Checkpoint=RejoinRound ', $pass) $server.Log
                if (-not $clientAText.Contains('CLIENT_LEAVE: Owner=1 ActualDisconnect=1')) { throw 'No genuine client disconnect evidence.' }
            }
            'Revoke' {
                Assert-Task22Markers $serverText @('SERVER_GE_DEATH:', 'SERVER_ACTION_REVOKED:',
                    'SERVER_CLIENT_ACK: Owner=1 Checkpoint=Removed ', $pass) $server.Log
                if (-not $clientAText.Contains('Checkpoint=Removed ')) { throw 'Network component removal did not reach client HUD.' }
            }
        }
        if ($WithMedia) {
            $image = Join-Path $screenshots "Task22-$Scenario-Owner1-Playing.png"
            if (-not (Test-Path -LiteralPath $image -PathType Leaf) -or (Get-Item -LiteralPath $image).Length -lt 1024) {
                throw "Missing rendered Playing HUD screenshot: $image"
            }
        }
        Write-Host "Task 22 $Scenario PASS: real server GE/lifecycle plus replicated client statistics, deadline and HUD. Log: $($server.Log)"
    } finally {
        foreach ($entry in $entries) {
            $entry.Process.Refresh()
            if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
        }
        if (Test-Path -LiteralPath $signal -PathType Leaf) { Remove-Item -LiteralPath $signal }
    }
}

function Invoke-Task22Legacy {
    foreach ($experience in 'DA_MiniPracticeExperience', 'DA_MiniArenaDiagnosticsExperience') {
        $entries = @()
        try {
            $entry = Start-Task22Process "/Game/Mini/Maps/L_MiniPractice?Experience=$experience" $experience 'Legacy' $Port
            $entries += $entry
            $null = Wait-Task22Marker $entry $entries 'MiniTask22Probe LEGACY_PASS:' ([DateTime]::UtcNow.AddSeconds($TimeoutSeconds))
            Write-Host "Task 22 disabled FFA regression PASS: $experience. Log: $($entry.Log)"
        } finally {
            foreach ($entry in $entries) {
                $entry.Process.Refresh()
                if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
            }
        }
    }
}

[string[]]$scenarios = if ($Mode -eq 'All') { @('Score', 'TimeDraw', 'TimeWin', 'Logout', 'Revoke', 'Legacy') } else { @($Mode) }
for ($index = 0; $index -lt $scenarios.Count; $index++) {
    if ($scenarios[$index] -eq 'Legacy') { Invoke-Task22Legacy }
    else { Invoke-Task22Network $scenarios[$index] ($Port + $index) }
}
if ($WithMedia) { Write-Host "HUD screenshots: $screenshots. Inspect the images before reporting visual acceptance." }
