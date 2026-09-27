[CmdletBinding()]
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor)) {
    throw "UnrealEditor-Cmd.exe was not found under $EngineRoot"
}
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$verificationLog = Join-Path $logDirectory 'Task03-Verification.log'
if (Test-Path -LiteralPath $verificationLog) {
    Remove-Item -LiteralPath $verificationLog
}
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null

& $editor (Join-Path $projectRoot 'FPS.uproject') `
    '-game' '-MiniProbeReceivers' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
    "-abslog=$verificationLog" | Out-Null
$editorExitCode = $LASTEXITCODE
if ($editorExitCode -ne 0) {
    throw "Task 03 verification process exited with code $editorExitCode. See $verificationLog"
}
if (-not (Test-Path -LiteralPath $verificationLog)) {
    throw "Task 03 verification did not write its log: $verificationLog"
}

$logText = Get-Content -LiteralPath $verificationLog -Raw
if ($logText -match '(?:LogPluginManager|LogModuleManager|LogGameFeatures|LogCommonGame|LogUIActionRouter): Error:') {
    throw "Task 03 verification found a framework load error. See $verificationLog"
}
$requiredMarkers = @(
    'Registered InitState.Spawned -> DataAvailable -> DataInitialized -> GameplayReady',
    'MiniTagProbe PASS: all four InitState tags are queryable',
    'MiniReceiverProbe PASS: ReceiverAdded=1 GameActorReady=1 ReceiverRemoved=1'
)
foreach ($marker in $requiredMarkers) {
    if (-not $logText.Contains($marker)) {
        throw "Task 03 verification log lacks '$marker'. See $verificationLog"
    }
}

$projectPlugins = @(
    'ModularGameplayActors', 'GameplayMessageRouter', 'CommonGame', 'CommonUser', 'UIExtension'
)
foreach ($plugin in $projectPlugins) {
    if (-not $logText.Contains("Mounting Project plugin $plugin")) {
        throw "Task 03 verification log lacks project plugin '$plugin'. See $verificationLog"
    }
}

Write-Host "Task 03 runtime verification passed. Log: $verificationLog"
