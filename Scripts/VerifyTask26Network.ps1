[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [string]$ProjectRoot = '',
    [ValidateSet('All', 'Baseline', 'Weak')][string]$Profile = 'All',
    [ValidateRange(180, 900)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65530)][int]$Port = 19026
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$projectPath = [IO.Path]::GetFullPath($ProjectRoot)
$projectFile = Join-Path $projectPath 'FPS.uproject'
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else { Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing Development runner: $runner" }
if (-not $PackagedExe -and -not (Test-Path -LiteralPath $projectFile -PathType Leaf)) { throw "Missing project: $projectFile" }
$logs = Join-Path $projectPath 'Saved\Logs'
$cache = Join-Path $projectPath 'DerivedDataCache'
New-Item -ItemType Directory -Path $logs, $cache -Force | Out-Null

function Assert-Records([string]$Text, [string]$Pattern, [int]$Count, [string]$Label) {
    $actual = [regex]::Matches($Text, $Pattern).Count
    if ($actual -ne $Count) { throw "$Label expected $Count records, got $actual." }
}
function Read-NetworkPeer($Entry) {
    $Entry.Process.Refresh()
    $text = if (Test-Path -LiteralPath $Entry.Log -PathType Leaf) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask26Net FAIL:|: Error:|Experience state .* -> Failed|Fatal error:|Assertion failed:|Ensure condition failed:|CreateSavedMove: Hit limit|BroadcastNetworkFailure:|BroadcastTravelFailure:') {
        throw "Unexpected weak-network runtime failure: $($Entry.Log)"
    }
    if ($Entry.Process.HasExited -and $Entry.Process.ExitCode -ne 0) { throw "Peer exited with $($Entry.Process.ExitCode): $($Entry.Log)" }
    return $text
}
function Start-NetworkPeer([string]$Role, [string]$Url, [string]$Name, [int]$RunPort, [int]$Lag, [int]$Loss) {
    $log = Join-Path $logs "Task26-Network-$Name-$Role.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f $projectFile) }
    $arguments += $Url
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @('-MiniProbeTask26Network', "-MiniTask26ExpectedLag=$Lag", "-MiniTask26ExpectedLoss=$Loss",
        "-MiniTask26NetworkTimeoutSeconds=$TimeoutSeconds", "-PktLag=$Lag", "-PktLoss=$Loss",
        '-PktLossMinSize=0', '-PktLossMaxSize=0', '-PktLagVariance=0', '-PktLagMin=0', '-PktLagMax=0',
        '-PktOrder=0', '-PktDup=0', '-PktJitter=0', '-PktIncomingLagMin=0', '-PktIncomingLagMax=0',
        '-PktIncomingLoss=0', '-PktBufferBloatInMS=0', '-PktIncomingBufferBloatInMS=0',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log), '"-ExecCmds=t.MaxFPS 30"')
    if ($Role -eq 'Server') { $arguments += "-port=$RunPort" }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectPath -WindowStyle Hidden -PassThru
    Write-Host "Task26 $Name $Role started. PID=$($process.Id) Log=$log"
    return @{ Process=$process; Log=$log; Role=$Role }
}
function Wait-NetworkMarker($Entry, [string]$Marker, [DateTime]$Deadline) {
    do {
        [string]$text = Read-NetworkPeer $Entry
        if ($text -match $Marker) { return $text }
        if ($Entry.Process.HasExited) { throw "Peer exited before $Marker : $($Entry.Log)" }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Finite wait expired for $Marker : $($Entry.Log)"
}

$profiles = if ($Profile -eq 'All') { @('Baseline', 'Weak') } else { @($Profile) }
$runIndex = 0
foreach ($name in $profiles) {
    $lag = if ($name -eq 'Weak') { 50 } else { 0 }
    $loss = if ($name -eq 'Weak') { 1 } else { 0 }
    $runPort = $Port + $runIndex
    ++$runIndex
    $entries = @()
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 45)
        $server = Start-NetworkPeer 'Server' '/Game/Mini/Maps/L_MiniArena?listen' $name $runPort $lag $loss
        $entries += $server
        $null = Wait-NetworkMarker $server 'MiniSpawn COMMITTED:' $deadline
        foreach ($role in 'ClientA', 'ClientB') { $entries += Start-NetworkPeer $role "127.0.0.1:$runPort" $name $runPort $lag $loss }
        $null = Wait-NetworkMarker $server 'MiniTask26Net LATE_JOIN_READY: Playing=1' $deadline
        # Third remote really connects after production Playing, never a synthetic OnRep.
        $entries += Start-NetworkPeer 'ClientC' "127.0.0.1:$runPort" $name $runPort $lag $loss
        do {
            $texts = @($entries | ForEach-Object { Read-NetworkPeer $_ })
            $exited = @($entries | Where-Object { $_.Process.HasExited }).Count
            if ($exited -eq 4) { break }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($exited -ne 4) { throw "Four peers did not terminate normally by finite deadline: $name" }
        [string]$serverText = $texts[0]
        Assert-Records $serverText 'MiniTask26Net SERVER_PASS: Players=4 OwnerSuites=3' 1 'four-peer final acceptance'
        Assert-Records $serverText 'MiniTask26Net FOUR_PEERS_READY: Players=4 RealConnections=3 RTT20Each=1 LateJoin=1' 1 'real late-joining fourth player'
        Assert-Records $serverText 'MiniTask26Net SERVER_JOIN:' 3 'remote owning connections'
        Assert-Records $serverText 'MiniTask26Net OWNER_SUITE_PASS:' 3 'real client weapon suites'
        Assert-Records $serverText 'MiniTask26Net RPC_VERIFIED:' 31 'real server observed weapon validations'
        Assert-Records $serverText 'MiniTask26Net REAL_DAMAGE_GE:' 19 'actual authority damage effects'
        Assert-Records $serverText 'MiniTask26Net RELOAD_VERIFIED:.*RealEnhancedInput=1 ReloadTagObserved=1' 3 'real reload and tag lifecycle'
        Assert-Records $serverText 'MiniTask26Net SWITCH_VERIFIED:.*RealEnhancedInput=1 OldSourceRevoked=1' 3 'real switch and old ability cleanup'
        Assert-Records $serverText 'MiniTask26Net OLD_EQUIPMENT_REJECTED:.*RealOwnerRPC=1 NewItemSequence=0 Unconsumed=1' 3 'actual obsolete equipment RPC'
        Assert-Records $serverText 'MiniTask26Net REAL_GE_SCORE: Owner=3 Kills=1 Deaths=1 DuplicateRejected=1 OldRoundRejected=1' 1 'GE death and once-only score'
        Assert-Records $serverText 'MiniTask26Net RESPAWN_VERIFIED:.*OldLifeNoticeRejected=1 Slots=4' 1 'production respawn and old-life rejection'
        Assert-Records $serverText 'MiniTask26Net SCORE_REPLICATED_ACK:' 3 'replicated score and HUD state'
        Assert-Records $serverText 'MiniTask26Net RTT_ACK:' 3 'three remote latency sample sets'
        if ($serverText -notmatch "MiniTask26Net DRIVER_READBACK: Stage=FourPeersReady .*Lag=$lag Loss=$loss Connections=3 OtherFieldsZero=1 Restored=0" -or
            $serverText -notmatch 'MiniTask26Net DRIVER_READBACK: Stage=ServerFinal .*Lag=0 Loss=0 Connections=3 OtherFieldsZero=1 Restored=1') { throw 'Actual Driver + connection profile evidence missing.' }
        for ($index = 1; $index -lt 4; ++$index) {
            [string]$clientText = $texts[$index]
            Assert-Records $clientText 'MiniTask26Net RTT_SAMPLE:' 20 'same-client-clock echo samples'
            Assert-Records $clientText 'MiniTask26Net RTT_SUMMARY:.*Samples=20 .*ActualMeasured=1' 1 'measured RTT statistics'
            Assert-Records $clientText 'MiniTask26Net CLIENT_PASS:' 1 'client completion and zero profile recovery'
            Assert-Records $clientText 'MiniTask26Net CLIENT_SEND:.*ProductionServerFire=1' $(if ($index -eq 3) { 11 } else { 10 }) 'client sends to existing production RPC'
            Assert-Records $clientText 'MiniTask26Net CLIENT_INPUT:.*RealEnhancedInput=1' 3 'mouse/reload/switch actual input'
            if ($clientText -notmatch "MiniTask26Net DRIVER_READBACK: Stage=OwningClient .*Lag=$lag Loss=$loss Connections=1 OtherFieldsZero=1 Restored=0" -or
                $clientText -notmatch 'MiniTask26Net DRIVER_READBACK: Stage=ClientFinal .*Lag=0 Loss=0 Connections=1 OtherFieldsZero=1 Restored=1') { throw "Client $index profile readback missing." }
        }
        Write-Host "Task26 $name PASS: four real peers; actual $lag ms/$loss percent settings; 60 RTT samples; three real owning-client RPC suites; 19 real GEs; once-only score; normal respawn; full profile restored; all Exit0."
    } finally {
        foreach ($entry in $entries) {
            $entry.Process.Refresh()
            if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force; $null = $entry.Process.WaitForExit(10000) }
        }
    }
}
Write-Host 'Task26 normal and weak-network acceptance complete. Actual latency/loss statistics are in per-peer logs; hitscan still uses current server state without rewind.'
