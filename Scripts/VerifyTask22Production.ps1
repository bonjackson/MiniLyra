[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 90,
    [ValidateRange(1024, 65535)][int]$Port = 19022
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$entries = @()
function Start-ProductionPeer([string]$Role, [string]$Url) {
    $log = Join-Path $projectRoot "Saved\Logs\Task22-Production-$Role.log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $arguments = @(
        ('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')), $Url, '-game',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f (Join-Path $projectRoot 'DerivedDataCache')),
        ('"-abslog={0}"' -f $log)
    )
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    return @{Process=$process; Log=$log; Role=$Role}
}
function Read-HealthyProduction($Entry) {
    $Entry.Process.Refresh()
    if ($Entry.Process.HasExited) { throw "$($Entry.Role) exited early: $($Entry.Log)" }
    $text = if (Test-Path -LiteralPath $Entry.Log) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'Experience state .* -> Failed|Mini(Phase|Match) .*REJECTED:|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|MiniTask22Probe |BroadcastNetworkFailure:') {
        throw "Production arena failed or invoked a diagnostic probe: $($Entry.Log)"
    }
    return $text
}
try {
    $server = Start-ProductionPeer 'Server' '/Game/Mini/Maps/L_MiniPractice?listen?Experience=DA_MiniArenaExperience'
    $entries += $server
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $text = Read-HealthyProduction $server
        if ($text.Contains('MiniSpawn COMMITTED:') -and $text -match 'Tag=GamePhase.MiniArena.Warmup .*Deadline=0\.000') { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $text.Contains('MiniSpawn COMMITTED:') -or $text -notmatch 'Tag=GamePhase.MiniArena.Warmup .*Deadline=0\.000') { throw 'Single-player production Warmup did not become ready.' }
    $waitingEnd = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $text = Read-HealthyProduction $server
        if ($text.Contains('Tag=GamePhase.MiniArena.Playing') -or $text.Contains('MiniMatch ROUND_STARTED:')) { throw 'Production started with fewer than two participants.' }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $waitingEnd)
    $client = Start-ProductionPeer 'Client' "127.0.0.1:$Port"
    $entries += $client
    do {
        $text = Read-HealthyProduction $server
        $clientText = Read-HealthyProduction $client
        $playing = [regex]::Match($text, 'MiniPhase BEGIN: .*Tag=GamePhase.MiniArena.Playing .*Deadline=(\d+\.\d+) Start=(\d+\.\d+)')
        $clientReady = $clientText -match 'Experience state NetMode=Client ID=MiniExperienceDefinition:DA_MiniArenaExperience ExecutingActions -> Loaded' -and $clientText.Contains('MiniASC AVATAR_BOUND:')
        if ($playing.Success -and $text.Contains('MiniMatch ROUND_STARTED: Round=1 Players=2 ScoreLimit=10') -and $clientReady) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $playing.Success -or -not $text.Contains('MiniMatch ROUND_STARTED: Round=1 Players=2 ScoreLimit=10') -or -not $clientReady) { throw 'Production two-player round did not start.' }
    $duration = [double]::Parse($playing.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) - [double]::Parse($playing.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
    if ([Math]::Abs($duration - 300.0) -gt 0.01 -or -not $text.Contains('ExperienceId=MiniExperienceDefinition:DA_MiniArenaExperience') -or $text.Contains('DiagnosticsExperience')) { throw "Production configuration mismatch: duration=$duration" }
    Write-Host "Task 22 production PASS: normal URL, single-player indefinite Warmup, two-player start, 300-second Playing, score limit 10, no probes. Logs: $($server.Log), $($client.Log)"
} finally {
    foreach ($entry in $entries) { $entry.Process.Refresh(); if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force } }
}
