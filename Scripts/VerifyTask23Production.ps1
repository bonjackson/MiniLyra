[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 90,
    [ValidateRange(1024, 65535)][int]$Port = 19023
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$entries = @()
function Start-Task23Production([string]$Role, [string]$Url) {
    $log = Join-Path $projectRoot "Saved\Logs\Task23-Production-$Role.log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $arguments = @(('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')), $Url, '-game',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f (Join-Path $projectRoot 'DerivedDataCache')), ('"-abslog={0}"' -f $log))
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    return @{Process=$process; Log=$log; Role=$Role}
}
function Read-Task23Production($Entry) {
    $Entry.Process.Refresh()
    if ($Entry.Process.HasExited) { throw "Production peer exited: $($Entry.Log)" }
    $text = if (Test-Path -LiteralPath $Entry.Log) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'Experience state .* -> Failed|Mini(Phase|Match).*REJECTED:|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|MiniTask23Probe |BroadcastNetworkFailure:') {
        throw "Production map failed or invoked a probe: $($Entry.Log)"
    }
    return $text
}
try {
    $server = Start-Task23Production 'Server' '/Game/Mini/Maps/L_MiniArena?listen'
    $entries += $server
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $text = Read-Task23Production $server
        if ($text.Contains('MiniSpawn COMMITTED:') -and $text -match 'Tag=GamePhase.MiniArena.Warmup .*Deadline=0\.000') { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $text.Contains('MiniSpawn COMMITTED:')) { throw 'Production single-player Warmup did not become ready.' }
    $waitUntil = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $text = Read-Task23Production $server
        if ($text.Contains('MiniMatch ROUND_STARTED:')) { throw 'Production round started with only one player.' }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $waitUntil)
    foreach ($role in 'ClientA', 'ClientB', 'ClientC') { $entries += Start-Task23Production $role "127.0.0.1:$Port" }
    do {
        $texts = @($entries | ForEach-Object { Read-Task23Production $_ })
        $text = $texts[0]
        $committedControllers = @([regex]::Matches($text, 'MiniSpawn COMMITTED: Controller=(\S+) ') |
            ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
        $clientsReady = @($texts | Select-Object -Skip 1 | Where-Object {
            $_ -match 'Experience state NetMode=Client ID=MiniExperienceDefinition:DA_MiniArenaExperience ExecutingActions -> Loaded' -and $_.Contains('MiniASC AVATAR_BOUND:')
        }).Count -eq 3
        $playing = [regex]::Match($text, 'MiniPhase BEGIN: .*Tag=GamePhase.MiniArena.Playing .*Deadline=(\d+\.\d+) Start=(\d+\.\d+)')
        if ($committedControllers.Count -eq 4 -and $clientsReady -and $playing.Success) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($committedControllers.Count -ne 4 -or -not $clientsReady -or -not $playing.Success) { throw 'Ordinary four-player map launch did not finish.' }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $duration = [double]::Parse($playing.Groups[1].Value, $culture) - [double]::Parse($playing.Groups[2].Value, $culture)
    if ([Math]::Abs($duration - 300) -gt 0.01 -or -not $text.Contains('selected map Experience MiniExperienceDefinition:DA_MiniArenaExperience') -or $text.Contains('DiagnosticsExperience')) {
        throw 'Ordinary arena map selected nonproduction rules.'
    }
    Write-Host 'Task23 production PASS: no probes or Experience URL, single-player waits, four independent processes load map-selected Arena with 300-second Playing.'
} finally {
    foreach ($entry in $entries) {
        $entry.Process.Refresh()
        if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
    }
}
