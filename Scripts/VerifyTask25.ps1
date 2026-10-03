[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateSet('All', 'Stress', 'RoundTrip', 'Recovery', 'RecoveryAction', 'RecoveryLogout', 'RecoveryTravel', 'RecoveryDeath')][string]$Mode = 'All',
    [ValidateRange(120, 1800)][int]$TimeoutSeconds = 480,
    [ValidateRange(1024, 65535)][int]$Port = 18926,
    [switch]$WithMedia
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else { Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$screenshots = Join-Path $projectRoot 'Saved\Screenshots'
New-Item -ItemType Directory -Path $logs, $cache, $screenshots -Force | Out-Null
$prefix = if ($PackagedExe) { 'Task25-Packaged' } else { 'Task25' }
if ($WithMedia) { $prefix += '-WithMedia' }

function Start-Task25Peer([string]$Scenario, [string]$Role, [string]$Name, [string]$SignalDir) {
    $log = Join-Path $logs "$prefix-$Scenario-$Name.log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')) }
    if ($Scenario -eq 'RoundTrip') { $arguments += '/Game/Mini/Maps/L_MiniFrontEnd' }
    elseif ($Role -eq 'Server') { $arguments += '/Game/Mini/Maps/L_MiniArena?listen' }
    else { $arguments += "127.0.0.1:$Port" }
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @("-MiniProbeTask25=$Scenario", "-MiniTask25Role=$Role", "-MiniTask25Peer=$Name",
        "-MiniTask25Address=127.0.0.1:$Port", ('"-MiniTask25SignalDir={0}"' -f $SignalDir),
        "-MiniTask25TimeoutSeconds=$TimeoutSeconds", '-MiniTask25IsolateInput', '-unattended', '-nosplash', '-nop4',
        '-ddc=InstalledNoZenLocalFallback', ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log),
        '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=5.0,ConnectionTimeout=5.0')
    if ($Role -eq 'Server' -and $Scenario -ne 'RoundTrip') { $arguments += "-port=$Port" }
    if ($WithMedia) {
        $arguments += @('-MiniProbeTask25Media', ('"-MiniTask25MediaOutput={0}"' -f $screenshots),
            '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task25 $Scenario $Name started. PID=$($process.Id) Log=$log"
    return @{ Process=$process; Log=$log; Role=$Role; Name=$Name; Scenario=$Scenario; ExpectedKilled=$false; ActualHostTerminated=$false }
}
function Read-Task25Healthy($Entry) {
    $Entry.Process.Refresh()
    $text = if (Test-Path -LiteralPath $Entry.Log) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask25Probe FAIL:|Experience state .* -> Failed|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|MiniAddWidgets (LOAD_FAILED|INVALID_CLASS_OR_TAG|NO_LAYER|NO_EXTENSION_SUBSYSTEM):') {
        throw "Task25 $($Entry.Name) failed: $($Entry.Log)"
    }
    foreach ($warning in [regex]::Matches($text, 'CreateSavedMove: Hit limit of 96 saved moves')) {
        $armed = $text.IndexOf('MiniTask25Probe HOST_LOSS_ARMED:')
        if (-not $Entry.ActualHostTerminated -or $Entry.Scenario -ne 'Stress' -or $Entry.Role -ne 'Client' -or $armed -lt 0 -or $warning.Index -lt $armed) {
            throw "Unexpected prediction backlog before actual host termination: $($Entry.Log)"
        }
    }
    foreach ($failure in [regex]::Matches($text, '(?m)^.*BroadcastNetworkFailure:.*$')) {
        $armed = $text.IndexOf('MiniTask25Probe HOST_LOSS_ARMED:')
        if ($Entry.Scenario -ne 'Stress' -or $Entry.Role -ne 'Client' -or $armed -lt 0 -or $failure.Index -lt $armed -or
            $failure.Value -notmatch 'ConnectionLost|ConnectionTimeout') {
            throw "Unexpected actual network failure: $($Entry.Log)"
        }
    }
    if ($Entry.Process.HasExited -and -not $Entry.ExpectedKilled) { throw "Task25 $($Entry.Name) exited early: $($Entry.Log)" }
    return $text
}
function Wait-Task25Marker($Entry, $Entries, [string]$Marker, [DateTime]$Deadline) {
    do {
        foreach ($peer in $Entries) { $null = Read-Task25Healthy $peer }
        [string]$text = Read-Task25Healthy $Entry
        if ($text.Contains("MiniTask25Probe $Marker")) { return $text }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Task25 missing '$Marker': $($Entry.Log)"
}
function Assert-Task25Count([string]$Text, [string]$Pattern, [int]$Count, [string]$Label) {
    $actual = [regex]::Matches($Text, $Pattern).Count
    if ($actual -ne $Count) { throw "Task25 $Label expected $Count records, got $actual." }
}
function Assert-Task25Media($Entry, [string[]]$Stages) {
    if (-not $WithMedia) { return }
    [string]$text = Read-Task25Healthy $Entry
    foreach ($stage in $Stages) {
        $path = Join-Path $screenshots "Task25-$($Entry.Scenario)-$($Entry.Name)-$stage.png"
        if (-not $text.Contains("Stage=$stage ") -or -not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 1024) {
            throw "Task25 actual rendered evidence missing: $path"
        }
    }
}
function Assert-Task25Stress($Server, $Entries) {
    [string]$text = Read-Task25Healthy $Server
    Assert-Task25Count $text 'MiniTask25Probe STRESS_DEATH:' 40 'environment GE deaths'
    Assert-Task25Count $text 'MiniTask25Probe STRESS_RESPAWN:' 40 'real respawns'
    $ids = @([regex]::Matches($text, 'STRESS_DEATH: Iteration=\d+ PlayerId=(\d+) ') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if ($ids.Count -ne 4) { throw 'Task25 stress did not exercise four distinct real players.' }
    foreach ($id in $ids) {
        Assert-Task25Count $text ("STRESS_DEATH: Iteration=\d+ PlayerId=$id ") 10 "player $id deaths"
        Assert-Task25Count $text ("STRESS_RESPAWN: Iteration=\d+ PlayerId=$id ") 10 "player $id respawns"
    }
    foreach ($iteration in 1..10) {
        Assert-Task25Count $text ("STRESS_DEATH: Iteration=$iteration ") 4 "iteration $iteration deaths"
        Assert-Task25Count $text ("STRESS_RESPAWN: Iteration=$iteration ") 4 "iteration $iteration respawns"
        Assert-Task25Count $text ("SERVER_CLIENT_ACK: Owner=\d+ Checkpoint=Dead$iteration ") 3 "iteration $iteration actual dead owner ACKs"
        Assert-Task25Count $text ("SERVER_CLIENT_ACK: Owner=\d+ Checkpoint=Live$iteration ") 3 "iteration $iteration actual live owner ACKs"
    }
    Assert-Task25Count $text 'SERVER_CLIENT_ACK: Owner=\d+ Checkpoint=LateJoinBaseline ' 2 'pre-late-join owner ACKs'
    Assert-Task25Count $text 'SERVER_CLIENT_ACK: Owner=\d+ Checkpoint=Rejoined ' 3 'rejoined owner ACKs'
    if (-not $text.Contains('STRESS_LOGOUT_PASS: Roster=3 OldPawnEnded=1 RetainedPSASC=1 OldWork=0')) { throw 'Task25 missing real Logout lifecycle proof.' }
    $inputText = ''
    foreach ($client in $Entries | Where-Object Role -eq 'Client') {
        [string]$clientText = Read-Task25Healthy $client
        Assert-Task25Count $clientText 'MiniTask25Probe CLIENT_DEATH:' 10 "$($client.Name) deaths"
        Assert-Task25Count $clientText 'MiniTask25Probe CLIENT_RESPAWN:' 10 "$($client.Name) respawns"
        if ([regex]::Matches($clientText, 'MiniTask25Probe NEW_INPUT_PASS:').Count -lt 11) { throw "Task25 $($client.Name) did not prove new-life actual movement input." }
        Assert-Task25Media $client @('StressBaseline', 'Rejoined', 'HostLossError')
        $inputText += $clientText
    }
    Assert-Task25Count $inputText 'INPUT_ARMED:.*FireHeld=1 AmmoChanged=1 ActualInputKey=1' 1 'real held fire before death'
    Assert-Task25Count $inputText 'INPUT_ARMED:.*ReloadActive=1 AmmoChanged=1 ActualInputKey=1' 1 'real reload before death'
    Assert-Task25Count $inputText 'CLIENT_LEAVE_REQUEST:' 1 'real production leave'
    Assert-Task25Count $inputText 'CLIENT_REJOIN_REQUEST:' 1 'real production reconnect'
    Assert-Task25Count $inputText 'REJOIN_OLD_FRONT_RELEASED:' 1 'reconnect old front-end teardown'
}
function Assert-Task25RoundTrip($Entry) {
    [string]$text = Read-Task25Healthy $Entry
    Assert-Task25Count $text 'MiniTask25Probe WORLD_READY:' 15 'new loaded Worlds'
    Assert-Task25Count $text 'MiniTask25Probe OLD_WORLD_RELEASED:' 14 'real World cleanups'
    Assert-Task25Count $text 'MiniTask25Probe OLD_STATE_SILENT:' 14 'old object delay observations'
    Assert-Task25Count $text 'MiniTask25Probe LATE_LOADED_PASS:' 15 'synchronous late Loaded subscriptions'
    Assert-Task25Count $text 'WORLD_READY: Generation=\d+ Kind=Practice ' 4 'Practice Worlds'
    Assert-Task25Count $text 'WORLD_READY: Generation=\d+ Kind=Arena ' 3 'Arena Worlds'
    Assert-Task25Count $text 'MiniTask25Probe PRACTICE_TRAVEL_ARMED:' 4 'target timer and supply overlap fixtures'
    Assert-Task25Count $text 'MiniFeature ActivationRequest .*MiniShooterCore' 7 'actual feature activation leases'
    Assert-Task25Count $text 'MiniFeature ActivationSucceeded .*MiniShooterCore' 7 'actual feature activations'
    Assert-Task25Count $text 'MiniFeature Release .*MiniShooterCore.*FinalUser=1 Activated=1' 7 'actual last feature user releases'
    Assert-Task25Media $Entry @('Practice1', 'Practice2', 'Practice3', 'Practice4', 'Arena1', 'Arena2', 'Arena3')
}

[string[]]$scenarios = if ($Mode -eq 'All') { @('Stress', 'RoundTrip', 'RecoveryAction', 'RecoveryLogout', 'RecoveryTravel', 'RecoveryDeath') }
    elseif ($Mode -eq 'Recovery') { @('RecoveryAction', 'RecoveryLogout', 'RecoveryTravel', 'RecoveryDeath') } else { @($Mode) }
foreach ($scenario in $scenarios) {
    $entries = @()
    $signalDir = Join-Path $projectRoot ("Saved\Task25Signals\{0}-{1}" -f $scenario, [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $signalDir -Force | Out-Null
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $server = Start-Task25Peer $scenario 'Server' 'Server' $signalDir
        $entries += $server
        if ($scenario -eq 'Stress') {
            $null = Wait-Task25Marker $server $entries 'LATE_LOADED_PASS:' $deadline
            foreach ($name in 'ClientA', 'ClientB') { $entries += Start-Task25Peer $scenario 'Client' $name $signalDir }
            $null = Wait-Task25Marker $server $entries 'STRESS_LATE_JOIN_READY:' $deadline
            $entries += Start-Task25Peer $scenario 'Client' 'ClientC' $signalDir
            $null = Wait-Task25Marker $server $entries 'STRESS_SERVER_PASS:' $deadline
            # Every client must hold old gameplay observations before the actual host disappears.
            foreach ($client in $entries | Where-Object Role -eq 'Client') { $null = Wait-Task25Marker $client $entries 'HOST_LOSS_ARMED:' $deadline }
            $server.ExpectedKilled = $true
            $server.Process.Refresh()
            if ($server.Process.HasExited) { throw 'Tracked host exited before the runner could exercise real host loss.' }
            Stop-Process -Id $server.Process.Id -Force
            $server.Process.WaitForExit(5000) | Out-Null
            $server.Process.Refresh()
            if (-not $server.Process.HasExited) { throw 'Tracked host is still alive after requested termination.' }
            foreach ($client in $entries | Where-Object Role -eq 'Client') { $client.ActualHostTerminated = $true }
            Write-Host "Task25 actual tracked host termination. PID=$($server.Process.Id)"
            foreach ($client in $entries | Where-Object Role -eq 'Client') { $null = Wait-Task25Marker $client $entries 'STRESS_CLIENT_PASS:' $deadline }
            Assert-Task25Stress $server $entries
            Write-Host 'Task25 Stress PASS: Playing late join, forty real deaths and respawns, actual input cancellation, leave/reconnect and three real host-loss recoveries.'
        } elseif ($scenario -eq 'RoundTrip') {
            $null = Wait-Task25Marker $server $entries 'ROUNDTRIP_PASS:' $deadline
            Assert-Task25RoundTrip $server
            Write-Host 'Task25 RoundTrip PASS: three complete Practice/Arena/Practice cycles, fifteen Worlds and fourteen real teardown observations.'
        } else {
            if ($scenario -in @('RecoveryLogout', 'RecoveryDeath')) {
                $null = Wait-Task25Marker $server $entries 'LATE_LOADED_PASS:' $deadline
                $client = Start-Task25Peer $scenario 'Client' 'Client' $signalDir
                $entries += $client
            }
            $serverMarker = switch ($scenario) {
                'RecoveryAction' { 'RECOVERY_ACTION_PASS:' }
                'RecoveryLogout' { 'RECOVERY_LOGOUT_SERVER_PASS:' }
                'RecoveryTravel' { 'RECOVERY_TRAVEL_PASS:' }
                'RecoveryDeath' { 'RECOVERY_DEATH_PASS:' }
            }
            [string]$serverText = Wait-Task25Marker $server $entries $serverMarker $deadline
            if (-not $serverText.Contains('RECOVERY_PAUSED:') -or -not $serverText.Contains('Pending=1 SameLife=1 Alive=1 MovementNone=1 MovementTick=1')) { throw 'Recovery never exercised the real live paused work.' }
            switch ($scenario) {
                'RecoveryAction' {
                    if (-not $serverText.Contains('RECOVERY_ACTION_REVOKED: ActualStockAction=1') -or -not $serverText.Contains('NoPreCancel=1')) { throw 'Recovery Action did not use the actual stock deactivation entry.' }
                    Assert-Task25Media $server @('RecoveryAction')
                }
                'RecoveryTravel' { Assert-Task25Media $server @('RecoveryFrontEnd') }
                'RecoveryLogout' {
                    $null = Wait-Task25Marker $client $entries 'RECOVERY_LOGOUT_CLIENT_PASS:' $deadline
                    Assert-Task25Media $client @('LogoutFrontEnd')
                }
                'RecoveryDeath' {
                    $null = Wait-Task25Marker $client $entries 'RECOVERY_DEATH_CLIENT_PASS:' $deadline
                    Assert-Task25Count $serverText 'SERVER_CLIENT_ACK: Owner=\d+ Checkpoint=RecoveryDead ' 1 'actual corpse owner ACK'
                    Assert-Task25Count $serverText 'SERVER_CLIENT_ACK: Owner=\d+ Checkpoint=RecoveryRespawned ' 1 'actual recovered owner ACK'
                    Assert-Task25Media $client @('RecoveryRespawned')
                }
            }
            Write-Host "Task25 $scenario PASS: actual lifecycle entry cancels the real paused recovery work."
        }
    } finally {
        foreach ($entry in $entries) {
            $entry.Process.Refresh()
            if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
        }
    }
}
if ($WithMedia) { Write-Host "Actual rendered evidence: $screenshots. Inspect it before reporting visual acceptance." }
