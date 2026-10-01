[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateSet('All', 'Arena', 'Practice')][string]$Mode = 'All',
    [ValidateRange(120, 900)][int]$TimeoutSeconds = 360,
    [ValidateRange(1024, 65535)][int]$Port = 18923,
    [switch]$WithMedia
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else {
    Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
}
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$screenshots = Join-Path $projectRoot 'Saved\Screenshots'
New-Item -ItemType Directory -Path $logs, $cache, $screenshots -Force | Out-Null
$prefix = if ($PackagedExe) { 'Task23-Packaged' } else { 'Task23' }
if ($WithMedia) { $prefix += '-WithMedia' }
$mediaPaths = @(foreach ($owner in 1, 2, 3) {
    foreach ($checkpoint in 'ArenaReady', 'Result1', 'Result2') { Join-Path $screenshots "Task23-Owner$owner-$checkpoint.png" }
}) + @(Join-Path $screenshots 'Task23-ArenaOverview.png')
if ($WithMedia) {
    foreach ($path in $mediaPaths) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
}

function Start-Task23Peer([string]$Url, [string]$Role, [string]$Scenario) {
    $log = Join-Path $logs "$prefix-$Scenario-$Role.log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')) }
    $arguments += $Url
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @("-MiniProbeTask23=$Scenario", "-MiniTask23TimeoutSeconds=$TimeoutSeconds",
        '-unattended', '-nosplash', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log))
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    if ($WithMedia -and $Role -eq 'ClientA') {
        $arguments += @('-MiniProbeTask23Media', ('"-MiniProbeTask23MediaOutput={0}"' -f $screenshots),
            '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task23 $Scenario $Role started. PID=$($process.Id) Log=$log"
    return @{Process=$process; Log=$log; Role=$Role}
}
function Read-Task23Healthy($Entry) {
    $Entry.Process.Refresh()
    $text = if (Test-Path -LiteralPath $Entry.Log) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask23Probe FAIL:|Experience state .* -> Failed|Mini(Phase|Match).*REJECTED:|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|BroadcastNetworkFailure:') {
        throw "Task23 $($Entry.Role) failed: $($Entry.Log)"
    }
    if ($Entry.Process.HasExited) { throw "Task23 $($Entry.Role) exited early: $($Entry.Log)" }
    return $text
}
function Wait-Task23Marker($Entry, $Entries, [string]$Marker, [DateTime]$Deadline) {
    do {
        foreach ($peer in $Entries) { $null = Read-Task23Healthy $peer }
        $text = Read-Task23Healthy $Entry
        if ($text.Contains($Marker)) { return $text }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Task23 missing '$Marker': $($Entry.Log)"
}
[string[]]$scenarios = if ($Mode -eq 'All') { @('Arena', 'Practice') } else { @($Mode) }
foreach ($scenario in $scenarios) {
    $entries = @()
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        if ($scenario -eq 'Arena') {
            # No Experience option: the new map must provide its production override.
            $server = Start-Task23Peer '/Game/Mini/Maps/L_MiniArena?listen' 'Server' $scenario
            $entries += $server
            $null = Wait-Task23Marker $server $entries 'MiniTask23Probe SERVER_JOIN_READY:' $deadline
            foreach ($role in 'ClientA', 'ClientB', 'ClientC') {
                $entries += Start-Task23Peer "127.0.0.1:$Port" $role $scenario
            }
            $serverText = Wait-Task23Marker $server $entries 'MiniTask23Probe ARENA_PASS:' $deadline
            $previous = -1
            foreach ($marker in @('SERVER_MAP_PASS:', 'SERVER_WARMUP_FALL_PASS:', 'SERVER_BLOCKED_FALL_PASS:',
                'SERVER_BLOCKED_CLEANUP_PASS:', 'SERVER_RECOVERY_REQUEUED_PASS:',
                'SERVER_JOIN_READY:', 'SERVER_FOUR_PLAYING_PASS:', 'SERVER_PROTECTED_FALL_PASS:',
                'SERVER_ALL_FALLS_PASS:', 'SERVER_RESULT_PASS: CompletedRound=1 ',
                'SERVER_NEXT_ROUND_PASS: Round=2 ', 'SERVER_RESULT_PASS: CompletedRound=2 ',
                'SERVER_NEXT_ROUND_PASS: Round=3 ', 'ARENA_PASS:')) {
                $match = [regex]::Match($serverText, [regex]::Escape($marker))
                if (-not $match.Success -or $match.Index -le $previous) { throw "Missing/out-of-order Task23 evidence '$marker'." }
                $previous = $match.Index
            }
            if ([regex]::Matches($serverText, 'SERVER_COMBAT_GE:').Count -ne 20 -or
                [regex]::Matches($serverText, 'SERVER_ENVIRONMENT_DEATH:').Count -ne 4) {
                throw 'Task23 did not contain exactly twenty combat GE deaths and four environment deaths.'
            }
            if ($serverText -notmatch 'ExperienceId=MiniExperienceDefinition:DA_MiniArenaExperience' -or $serverText.Contains('DiagnosticsExperience')) {
                throw 'Task23 must run the production Arena selected by its map.'
            }
            foreach ($entry in $entries | Where-Object Role -ne 'Server') {
                $clientText = Read-Task23Healthy $entry
                if ($clientText -notmatch 'Experience state NetMode=Client ID=MiniExperienceDefinition:DA_MiniArenaExperience ExecutingActions -> Loaded' -or
                    -not $clientText.Contains('MiniTask23Probe CLIENT_CHECKPOINT:')) {
                    throw "Task23 client did not acknowledge real production replicated state: $($entry.Log)"
                }
                foreach ($checkpoint in 'ArenaReady', 'EnvironmentDeaths', 'Result1', 'Round2', 'Result2', 'Round3') {
                    if (-not $clientText.Contains("Checkpoint=$checkpoint ")) {
                        throw "Client missed Task23 '$checkpoint': $($entry.Log)"
                    }
                }
            }
            if ($WithMedia) {
                # Rendering initialization can change login order; use the
                # actual rendered peer's server-issued owner index.
                $rendered = $entries | Where-Object Role -eq 'ClientA' | Select-Object -First 1
                $renderedText = Read-Task23Healthy $rendered
                $renderedOwner = [regex]::Match($renderedText, 'CLIENT_CHECKPOINT: Owner=(\d+) Checkpoint=ArenaReady ').Groups[1].Value
                if (-not $renderedOwner) { throw 'Rendered peer did not publish its actual owner index.' }
                $requiredMedia = @('ArenaReady', 'Result1', 'Result2') | ForEach-Object { Join-Path $screenshots "Task23-Owner$renderedOwner-$_.png" }
                $requiredMedia += Join-Path $screenshots 'Task23-ArenaOverview.png'
                foreach ($path in $requiredMedia) {
                    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -lt 1024) {
                        throw "Task23 rendered evidence missing: $path"
                    }
                }
            }
            Write-Host 'Task23 Arena PASS: production map, four independent processes, real collisions, fall lifecycle and consecutive rounds.'
        } else {
            $server = Start-Task23Peer '/Game/Mini/Maps/L_MiniPractice' 'Server' $scenario
            $entries += $server
            $null = Wait-Task23Marker $server $entries 'MiniTask23Probe PRACTICE_PASS:' $deadline
            Write-Host 'Task23 Practice PASS: map selects its independent training assembly and retains real damage/death/respawn.'
        }
    } finally {
        foreach ($entry in $entries) {
            $entry.Process.Refresh()
            if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
        }
    }
}
if ($WithMedia) { Write-Host "Actual rendered evidence: $screenshots. Inspect it before reporting visual acceptance." }
