[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateSet('All', 'Four', 'Combat', 'RestartTrend', 'Practice')][string]$Mode = 'All',
    [ValidateSet('Current', 'Performance')][string]$Profile = 'Current',
    [ValidateRange(0, 240)][double]$MaxFPS = 60,
    [ValidateRange(2, 120)][double]$WarmupSeconds = 10,
    [ValidateRange(5, 240)][double]$SampleSeconds = 45,
    [ValidateRange(5, 60)][double]$RestartSampleSeconds = 10,
    [ValidateRange(1, 10)][int]$Restarts = 5,
    [ValidateRange(120, 3600)][int]$TimeoutSeconds = 900,
    [ValidateRange(1024, 65535)][int]$Port = 18927,
    [switch]$WithMedia
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'FPS.uproject') -PathType Leaf)) {
    throw 'Integrate this candidate into the project Scripts directory before running it.'
}
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else { Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$runId = [guid]::NewGuid().ToString('N')
$outputRoot = Join-Path $projectRoot "Saved\Task27Results\$runId"
$cache = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $outputRoot, $cache -Force | Out-Null
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Convert-Task27Number($Value) { return [double]::Parse([string]$Value, $invariant) }
function Quote-Task27Argument([string]$Value) {
    if ($Value.Contains('"') -or $Value.Contains("`n") -or $Value.Contains("`r")) { throw 'Command argument contains an unsupported quote or newline.' }
    return '"' + $Value + '"'
}
function Get-Task27Percentile([double[]]$Values, [double]$Fraction) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Max(0, [Math]::Min($sorted.Count - 1, [Math]::Ceiling($Fraction * $sorted.Count) - 1))
    return [double]$sorted[$index]
}
function Get-Task27Hardware {
    $cpu = Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors
    $os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, TotalVisibleMemorySize
    $ram = Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory
    $gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, AdapterRAM)
    $battery = @(Get-CimInstance Win32_Battery | Select-Object BatteryStatus, EstimatedChargeRemaining)
    $power = try { @(Get-CimInstance -Namespace root/cimv2/power -ClassName Win32_PowerPlan | Where-Object IsActive | Select-Object ElementName, IsActive) } catch { @() }
    $revision = try { [string](& git -C $projectRoot rev-parse HEAD 2>$null) } catch { 'Unavailable' }
    return [ordered]@{
        UTC = [DateTime]::UtcNow.ToString('o'); CPU = @($cpu); OS = $os; RAM = $ram; EnumeratedGPUs = $gpu
        Battery = $battery; ActivePowerPlan = $power; Revision = $revision.Trim()
        ActualGPU = 'Read per-peer MiniTask27Probe RENDER markers; WMI enumeration does not select the UE adapter.'
        Execution = $(if ($PackagedExe) { 'Packaged Development; verify binary configuration in log.' } else { 'UnrealEditor Development -game' })
        Conditions = 'Four local rendered 1920x1080 processes share CPU/GPU/RAM. Windows may overlap; do not minimize them.'
    }
}
Get-Task27Hardware | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'Hardware.json') -Encoding UTF8
[ordered]@{ RunId=$runId; Mode=$Mode; Profile=$Profile; MaxFPS=$MaxFPS; WarmupSeconds=$WarmupSeconds; SampleSeconds=$SampleSeconds
    RestartSampleSeconds=$RestartSampleSeconds; Restarts=$Restarts; Port=$Port; Media=[bool]$WithMedia; UTC=[DateTime]::UtcNow.ToString('o')
    Near60Definition='Mean FPS >= 59 and wall-frame P95 <= 18ms; tolerance is separate from strict 60FPS/16.667ms.'
    Target60Definition='Mean FPS >= 60 and wall-frame P95 <= 16.666667ms; Over16_67Percent remains visible.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'Run.json') -Encoding UTF8

function Start-Task27Peer([string]$Scenario, [string]$Role, [string]$Name, [string]$Directory) {
    $probeMode = if ($Scenario -eq 'Combat') { 'ObserveArena' } else { $Scenario }
    $duration = if ($Scenario -eq 'RestartTrend') { $RestartSampleSeconds } else { $SampleSeconds }
    $log = Join-Path $Directory "$Name.log"
    $arguments = @()
    if (-not $PackagedExe) { $arguments += Quote-Task27Argument (Join-Path $projectRoot 'FPS.uproject') }
    if ($Scenario -eq 'Practice') { $arguments += '/Game/Mini/Maps/L_MiniPractice' }
    elseif ($Role -eq 'Server') { $arguments += '/Game/Mini/Maps/L_MiniArena?listen' }
    else { $arguments += "127.0.0.1:$Port" }
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @("-MiniProbeTask27=$probeMode", "-MiniTask27Role=$Role", "-MiniTask27Peer=$Name", "-MiniTask27Profile=$Profile",
        "-MiniTask27RunId=$runId", "-MiniTask27Restarts=$Restarts", "-MiniTask27TimeoutSeconds=$TimeoutSeconds",
        ('-MiniTask27WarmupSeconds=' + $WarmupSeconds.ToString($invariant)), ('-MiniTask27SampleSeconds=' + $duration.ToString($invariant)),
        (Quote-Task27Argument "-MiniTask27Output=$Directory"), (Quote-Task27Argument "-MiniTask27Signals=$(Join-Path $Directory 'Signals')"),
        '-MiniTask27IsolateInput', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080', '-unattended', '-nosplash', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        (Quote-Task27Argument "-LocalDataCachePath=$cache"), (Quote-Task27Argument "-abslog=$log"))
    if ($Role -eq 'Server' -and $Scenario -ne 'Practice') { $arguments += "-port=$Port" }
    if ($Scenario -eq 'Combat') { $arguments += @('-MiniProbeTask23=Arena', "-MiniTask23TimeoutSeconds=$TimeoutSeconds") }
    if ($WithMedia) { $arguments += '-MiniTask27Media' }
    $settings = @('r.ScreenPercentage 100', 'r.VSync 0', 'r.DynamicRes.OperationMode 0', 't.IdleWhenNotForeground 0', ('t.MaxFPS ' + $MaxFPS.ToString($invariant)))
    if ($Profile -eq 'Performance') { $settings += @('sg.GlobalIlluminationQuality 0', 'sg.ReflectionQuality 0', 'sg.ShadowQuality 1', 'sg.AntiAliasingQuality 2') }
    $arguments += Quote-Task27Argument ('-ExecCmds=' + ($settings -join ','))
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task27 $Scenario $Name started: PID=$($process.Id), actual rendered 1920x1080, profile=$Profile."
    return @{ Process=$process; Log=$log; Role=$Role; Name=$Name; Scenario=$Scenario; Directory=$Directory }
}
function Read-Task27Healthy($Entry) {
    $Entry.Process.Refresh()
    $logText = if (Test-Path -LiteralPath $Entry.Log) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($logText -match 'MiniTask27Probe FAIL:|MiniTask23Probe FAIL:|Experience state .* -> Failed|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|BroadcastNetworkFailure:|MiniAddWidgets (LOAD_FAILED|INVALID_CLASS_OR_TAG|NO_LAYER|NO_EXTENSION_SUBSYSTEM):') {
        throw "Task27 $($Entry.Scenario) $($Entry.Name) failed; actual evidence retained at $($Entry.Log)"
    }
    if ($Entry.Process.HasExited) { throw "Task27 $($Entry.Name) exited before runner cleanup: $($Entry.Log)" }
    return $logText
}
function Wait-Task27Marker($Entry, $Entries, [string]$Marker, [DateTime]$Deadline) {
    do {
        foreach ($peer in $Entries) { $null = Read-Task27Healthy $peer }
        [string]$logText = Read-Task27Healthy $Entry
        if ($logText.Contains($Marker)) { return $logText }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Task27 missing '$Marker': $($Entry.Log)"
}
function Assert-Task27Combat($Server, $Entries) {
    [string]$serverText = Read-Task27Healthy $Server
    if (-not $serverText.Contains('MiniTask23Probe ARENA_PASS:') -or
        [regex]::Matches($serverText, 'MiniTask23Probe SERVER_COMBAT_GE:').Count -ne 20 -or
        [regex]::Matches($serverText, 'MiniTask23Probe SERVER_ENVIRONMENT_DEATH:').Count -ne 4) {
        throw 'Combat measurement did not contain the real Task23 two rounds, 20 combat GE deaths and four environment GE deaths.'
    }
    foreach ($entry in $Entries | Where-Object Role -eq 'Client') {
        [string]$clientText = Read-Task27Healthy $entry
        foreach ($checkpoint in 'ArenaReady', 'EnvironmentDeaths', 'Result1', 'Round2', 'Result2', 'Round3') {
            if (-not $clientText.Contains("Checkpoint=$checkpoint ")) { throw "Combat $($entry.Name) missed actual owner acknowledgement '$checkpoint'." }
        }
    }
}
function Assert-Task27Media($Entry) {
    if (-not $WithMedia) { return }
    [string]$logText = Read-Task27Healthy $Entry
    $path = Join-Path $Entry.Directory "$($Entry.Name)-1080p.png"
    if (-not $logText.Contains("SCREENSHOT_SAVED: Peer=$($Entry.Name) Size=1920x1080 ") -or
        -not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 1024) { throw "Missing current-run rendered screenshot: $path" }
    Add-Type -AssemblyName System.Drawing
    $bitmap = [Drawing.Bitmap]::FromFile($path)
    try { if ($bitmap.Width -ne 1920 -or $bitmap.Height -ne 1080) { throw "Wrong actual screenshot dimensions: $path" } }
    finally { $bitmap.Dispose() }
}
function Read-Task27Measurements($Entry, [int]$CycleCount) {
    [string]$logText = Read-Task27Healthy $Entry
    foreach ($kind in 'frames', 'gpu', 'snapshots', 'network') {
        $path = Join-Path $Entry.Directory "$($Entry.Name)-$kind.csv"
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -lt 30) { throw "Missing actual measurement CSV: $path" }
    }
    $frames = @(Import-Csv -LiteralPath (Join-Path $Entry.Directory "$($Entry.Name)-frames.csv"))
    $gpu = @(Import-Csv -LiteralPath (Join-Path $Entry.Directory "$($Entry.Name)-gpu.csv"))
    $snapshots = @(Import-Csv -LiteralPath (Join-Path $Entry.Directory "$($Entry.Name)-snapshots.csv"))
    $network = @(Import-Csv -LiteralPath (Join-Path $Entry.Directory "$($Entry.Name)-network.csv"))
    if ([regex]::Matches($logText, 'MiniTask27Probe SAMPLE_END:').Count -ne $CycleCount -or
        [regex]::Matches($logText, 'MiniTask27Probe WORLD:').Count -ne $CycleCount) { throw "Wrong completed cycle evidence: $($Entry.Log)" }
    if ($Entry.Scenario -eq 'Combat') {
        foreach ($round in 1, 2) {
            if (@($frames | Where-Object { [int]$_.Round -eq $round }).Count -eq 0 -or @($gpu | Where-Object { [int]$_.Round -eq $round }).Count -eq 0) { throw "Combat $($Entry.Name) did not measure real Playing round $round." }
        }
    }
    $summary = @()
    foreach ($cycle in 1..$CycleCount) {
        $frameRows = @($frames | Where-Object { [int]$_.Cycle -eq $cycle })
        $gpuRows = @($gpu | Where-Object { [int]$_.Cycle -eq $cycle })
        $minimumFrames = if ($Entry.Scenario -eq 'RestartTrend') { 20 } else { 60 }
        $minimumGPU = if ($Entry.Scenario -eq 'RestartTrend') { 10 } else { 30 }
        if ($frameRows.Count -lt $minimumFrames -or $gpuRows.Count -lt $minimumGPU) { throw "Insufficient actual CPU/GPU samples for $($Entry.Name), cycle $cycle." }
        [double[]]$wall = @($frameRows | ForEach-Object { Convert-Task27Number $_.FrameMS })
        [double[]]$game = @($frameRows | ForEach-Object { Convert-Task27Number $_.GameBusyMS })
        [double[]]$render = @($frameRows | ForEach-Object { Convert-Task27Number $_.RenderBusyMS })
        [double[]]$rhi = @($frameRows | ForEach-Object { Convert-Task27Number $_.RHIBusyMS })
        [double[]]$gpuMS = @($gpuRows | ForEach-Object { Convert-Task27Number $_.UniqueGPUFrameMS })
        foreach ($value in @($wall) + @($gpuMS)) {
            if ($value -le 0 -or [double]::IsNaN($value) -or [double]::IsInfinity($value)) { throw 'Frame/GPU samples must be actual positive finite times.' }
        }
        $sumMS = ($wall | Measure-Object -Sum).Sum
        $fps = 1000.0 * $wall.Count / $sumMS
        $p95 = Get-Task27Percentile $wall 0.95
        $disjoint = @($gpuRows | Where-Object { [int]$_.Disjoint -ne 0 }).Count
        if ($disjoint -ne 0) { throw 'A measured GPU history gap omitted an unknown number of frames; the incomplete capture cannot substantiate GPU distribution.' }
        $netRows = @($network | Where-Object { [int]$_.Cycle -eq $cycle })
        if ($Entry.Scenario -ne 'Practice' -and $netRows.Count -eq 0) { throw 'Multiplayer measurement did not observe a real NetDriver.' }
        $inBytes = if ($netRows.Count) { ($netRows | ForEach-Object { [uint64]$_.InBytes } | Measure-Object -Sum).Sum } else { 0 }
        $outBytes = if ($netRows.Count) { ($netRows | ForEach-Object { [uint64]$_.OutBytes } | Measure-Object -Sum).Sum } else { 0 }
        $networkSeconds = if ($netRows.Count) { ($netRows | ForEach-Object { Convert-Task27Number $_.IntervalSeconds } | Measure-Object -Sum).Sum } else { 0 }
        $snapshot = @($snapshots | Where-Object { [int]$_.Cycle -eq $cycle -and $_.Stage -eq 'SettledEnd' })
        if ($snapshot.Count -ne 1) { throw "Missing equivalent settled snapshot for $($Entry.Name), cycle $cycle." }
        $renderLine = [regex]::Match($logText, "(?m)^.*MiniTask27Probe RENDER: Peer=$($Entry.Name) Cycle=$cycle .*$").Value.TrimEnd()
        if (-not $renderLine -or $renderLine -notmatch 'Viewport=1920x1080 ' -or $renderLine -match 'RHI=Null' -or
            $renderLine -notmatch 'ScreenPercentage=100 VSync=0 DynamicRes=0 BackgroundIdle=0 ') { throw "Actual rendering readback missing for $($Entry.Name), cycle $cycle." }
        $actualCap = [regex]::Match($renderLine, 'MaxFPS=([0-9.]+) ').Groups[1].Value
        if (-not $actualCap -or [Math]::Abs((Convert-Task27Number $actualCap) - $MaxFPS) -gt 0.11) { throw 'Actual frame cap differs from the requested run.' }
        $settings = @([regex]::Matches($logText, "(?m)^.*MiniTask27Probe SETTING: Peer=$($Entry.Name) Cycle=$cycle .*$") | ForEach-Object { $_.Value.TrimEnd() })
        if ($settings.Count -lt 10) { throw 'Current quality readback was not recorded.' }
        if ($Profile -eq 'Performance') {
            foreach ($expected in @('sg.GlobalIlluminationQuality Value=0.000', 'sg.ReflectionQuality Value=0.000', 'sg.ShadowQuality Value=1.000', 'sg.AntiAliasingQuality Value=2.000')) {
                if (-not ($settings -join "`n").Contains("Name=$expected")) { throw "Performance profile actual readback differs: $expected" }
            }
        }
        $summary += [pscustomobject][ordered]@{
            Scenario=$Entry.Scenario; Profile=$Profile; Peer=$Entry.Name; Cycle=$cycle; Frames=$wall.Count; UniqueGPUFrames=$gpuMS.Count; GPUDisjoint=$disjoint
            MeasuredSeconds=$sumMS / 1000.0; MeanFPS=$fps; FrameMedianMS=(Get-Task27Percentile $wall 0.5); FrameP95MS=$p95; FrameP99MS=(Get-Task27Percentile $wall 0.99)
            Over16_67Percent=100.0 * @($wall | Where-Object { $_ -gt 16.666667 }).Count / $wall.Count
            Over33_33Percent=100.0 * @($wall | Where-Object { $_ -gt 33.333333 }).Count / $wall.Count
            GameMedianMS=(Get-Task27Percentile $game 0.5); GameP95MS=(Get-Task27Percentile $game 0.95)
            RenderMedianMS=(Get-Task27Percentile $render 0.5); RenderP95MS=(Get-Task27Percentile $render 0.95)
            RHIMedianMS=(Get-Task27Percentile $rhi 0.5); RHIP95MS=(Get-Task27Percentile $rhi 0.95)
            GPUMedianMS=(Get-Task27Percentile $gpuMS 0.5); GPUP95MS=(Get-Task27Percentile $gpuMS 0.95); GPUP99MS=(Get-Task27Percentile $gpuMS 0.99)
            Target60Met=($fps -ge 60 -and $p95 -le 16.666667); Near60TargetMet=($fps -ge 59 -and $p95 -le 18)
            NetworkSeconds=$networkSeconds; InBytes=$inBytes; OutBytes=$outBytes
            InBytesPerSecond=$(if ($networkSeconds -gt 0) { $inBytes / $networkSeconds } else { $null })
            OutBytesPerSecond=$(if ($networkSeconds -gt 0) { $outBytes / $networkSeconds } else { $null })
            WorkingSetBytes=[uint64]$snapshot[0].WorkingSetBytes; CommittedBytes=[uint64]$snapshot[0].CommittedBytes; GlobalObjects=[int]$snapshot[0].GlobalObjects
            DiagnosticArrayStringBytes=[uint64]$snapshot[0].DiagnosticArrayStringBytes
            RenderReadback=$renderLine; SettingsReadback=($settings -join "`n")
        }
    }
    Assert-Task27Media $Entry
    return $summary
}
function Export-Task27Trend($Entry, [int]$CycleCount) {
    $rows = @(Import-Csv -LiteralPath (Join-Path $Entry.Directory "$($Entry.Name)-snapshots.csv") | Where-Object Stage -eq 'SettledEnd' | Sort-Object { [int]$_.Cycle })
    if ($rows.Count -ne $CycleCount) { throw 'Restart memory trend does not contain every actual new World.' }
    $previous = $null
    $trend = foreach ($row in $rows) {
        if ([int]$row.OldWorldWeakAlive -ne 0 -or [int]$row.HeroBindings -ne 16 -or [int]$row.HUDLayouts -ne 1 -or
            [int]$row.HUDWidgets -ne 4 -or [int]$row.HUDExtensionPoints -ne 4 -or [int]$row.RootInputGates -ne 0) { throw 'Restart snapshot accumulated gameplay objects or input gates.' }
        $item = [pscustomobject][ordered]@{
            Peer=$Entry.Name; Cycle=[int]$row.Cycle; GCPolicy='Natural; no forced GC'; Actors=[int]$row.Actors; Components=[int]$row.Components
            PlayerStates=[int]$row.PlayerStates; Pawns=[int]$row.Pawns; Controllers=[int]$row.Controllers; OwnedInventoryEntries=[int]$row.OwnedInventoryEntries
            GlobalObjects=[int]$row.GlobalObjects; ClaimedObjectSlots=[int]$row.ClaimedObjectSlots; OldWorldWeakAlive=[int]$row.OldWorldWeakAlive
            WorkingSetBytes=[uint64]$row.WorkingSetBytes; CommittedBytes=[uint64]$row.CommittedBytes; VMBindings=[int]$row.VMBindings
            HUDMessageListeners=[int]$row.HUDMessageListeners; HeroBindings=[int]$row.HeroBindings; AbilitySpecs=[int]$row.AbilitySpecs
            DiagnosticArrayStringBytes=[uint64]$row.DiagnosticArrayStringBytes
            WorkingSetDelta=$(if ($previous) { [long]$row.WorkingSetBytes - [long]$previous.WorkingSetBytes } else { $null })
            CommittedDelta=$(if ($previous) { [long]$row.CommittedBytes - [long]$previous.CommittedBytes } else { $null })
            ObjectDelta=$(if ($previous) { [int]$row.GlobalObjects - [int]$previous.GlobalObjects } else { $null })
        }
        $previous = $row
        $item
    }
    $trend | Export-Csv -LiteralPath (Join-Path $Entry.Directory "$($Entry.Name)-trend.csv") -NoTypeInformation -Encoding UTF8
    # Numeric memory growth is evidence to investigate, not an automatic leak verdict.
    return @($trend)
}

[string[]]$scenarios = if ($Mode -eq 'All') { @('Four', 'Combat', 'RestartTrend', 'Practice') } else { @($Mode) }
$allSummary = @(); $allTrends = @()
foreach ($scenario in $scenarios) {
    $entries = @()
    $directory = Join-Path $outputRoot $scenario
    New-Item -ItemType Directory -Path $directory, (Join-Path $directory 'Signals') -Force | Out-Null
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $server = Start-Task27Peer $scenario 'Server' 'Server' $directory
        $entries += $server
        if ($scenario -ne 'Practice') {
            $joinMarker = if ($scenario -eq 'Combat') { 'MiniTask23Probe SERVER_JOIN_READY:' } else { 'MiniTask27Probe SERVER_JOIN_READY:' }
            $null = Wait-Task27Marker $server $entries $joinMarker $deadline
            foreach ($name in 'ClientA', 'ClientB', 'ClientC') { $entries += Start-Task27Peer $scenario 'Client' $name $directory }
        }
        foreach ($entry in $entries) { $null = Wait-Task27Marker $entry $entries 'MiniTask27Probe CAPTURE_PASS:' $deadline }
        if ($scenario -eq 'Combat') {
            $null = Wait-Task27Marker $server $entries 'MiniTask23Probe ARENA_PASS:' $deadline
            Assert-Task27Combat $server $entries
        }
        $cycles = if ($scenario -eq 'RestartTrend') { $Restarts + 1 } else { 1 }
        foreach ($entry in $entries) {
            $allSummary += @(Read-Task27Measurements $entry $cycles)
            if ($scenario -eq 'RestartTrend') { $allTrends += @(Export-Task27Trend $entry $cycles) }
        }
        if ($scenario -eq 'RestartTrend') {
            [string]$serverText = Read-Task27Healthy $server
            if ([regex]::Matches($serverText, 'MiniTask27Probe REAL_RESTART:').Count -ne $Restarts) { throw 'RestartTrend did not use the required real production RestartArena requests.' }
        }
        $allSummary | Export-Csv -LiteralPath (Join-Path $outputRoot 'Summary.csv') -NoTypeInformation -Encoding UTF8
        $allSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'Summary.json') -Encoding UTF8
        if ($allTrends.Count) { $allTrends | Export-Csv -LiteralPath (Join-Path $outputRoot 'RestartTrend.csv') -NoTypeInformation -Encoding UTF8 }
        Write-Host "Task27 $scenario CAPTURE PASS. Actual FPS and strict/near-60 assessments are retained in Summary.csv."
    } finally {
        foreach ($entry in $entries) {
            $entry.Process.Refresh()
            if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
        }
    }
}
Write-Host "Task27 measured evidence: $outputRoot"
if ($WithMedia) { Write-Host 'Current-run PNG dimensions were checked. Inspect the actual images before claiming visual acceptance.' }
