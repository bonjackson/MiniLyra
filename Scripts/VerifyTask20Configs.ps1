[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 120,
    [string]$PackagedExe = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else {
    Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
}
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$prefix = if ($PackagedExe) { 'Task20-Packaged-Config' } else { 'Task20-Config' }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logs, $cache -Force | Out-Null

foreach ($variant in 'RifleOnly', 'Unarmed') {
    $log = Join-Path $logs "$prefix-$variant.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')) }
    $arguments += "/Game/Mini/Maps/L_MiniPractice?Experience=DA_Mini${variant}Experience"
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @(
        "-MiniProbeTask20Config=$variant", '-nullrhi', '-nosound', '-unattended', '-nosplash', '-nop4',
        '-ddc=InstalledNoZenLocalFallback', ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log)
    )
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    try {
        $count = if ($variant -eq 'RifleOnly') { 1 } else { 0 }
        $pass = "MiniTask20Config PASS: Variant=$variant Experience=DA_Mini${variant}Experience Items=$count Fire=$count Reload=$count EquipmentSource=1 ProbeAbilities=0 Targets=0 Supply=0 OwnerHUD=1 Input=1 RuntimeLoadoutMutation=0"
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
            $process.Refresh()
            if ($process.HasExited) { throw "Config $variant exited early: $log" }
            $text = if (Test-Path -LiteralPath $log) { [string](Get-Content -LiteralPath $log -Raw) } else { '' }
            if ($text -match 'MiniTask20Config FAIL:|Experience state .* -> Failed|Fatal error:|Assertion failed:') {
                throw "Config $variant failed: $log"
            }
            if ($text.Contains($pass)) { break }
            Start-Sleep -Milliseconds 500
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not $text.Contains($pass)) { throw "Config $variant did not PASS: $log" }
        if (-not $text.Contains("MiniGameMode selected travel Experience MiniExperienceDefinition:DA_Mini${variant}Experience")) {
            throw "Config $variant did not exercise normal travel selection: $log"
        }
        Write-Host "Task 20 config $variant PASS: URL-selected Experience, items=$count, equipment grants=$count, owner HUD/input, training actors=0. Log: $log"
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
