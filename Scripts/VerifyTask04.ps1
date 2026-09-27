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
$verificationLog = Join-Path $logDirectory 'Task04-RuntimeVerification.log'
if (Test-Path -LiteralPath $verificationLog) {
    Remove-Item -LiteralPath $verificationLog
}
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null

& $editor (Join-Path $projectRoot 'FPS.uproject') `
    '-game' '-MiniProbeExperience' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
    "-abslog=$verificationLog" | Out-Null
$editorExitCode = $LASTEXITCODE
if ($editorExitCode -ne 0) {
    throw "Task 04 runtime verification exited with code $editorExitCode. See $verificationLog"
}
if (-not (Test-Path -LiteralPath $verificationLog)) {
    throw "Task 04 runtime verification did not write its log: $verificationLog"
}

$logText = Get-Content -LiteralPath $verificationLog -Raw
if ($logText -match '(?:LogPluginManager|LogModuleManager|LogGameFeatures|LogMiniExperience): Error:') {
    throw "Task 04 runtime verification found an asset or framework error. See $verificationLog"
}
$requiredMarkers = @(
    'MiniExperienceProbe PASS: ID=MiniExperienceDefinition:DA_MiniPracticeExperience',
    'MiniExperienceProbe negative cases PASS:',
    'Unknown Experience ID',
    'has no DefaultPawnData',
    'has no PawnClass',
    'must derive from MiniCharacter'
)
foreach ($marker in $requiredMarkers) {
    if (-not $logText.Contains($marker)) {
        throw "Task 04 runtime verification log lacks '$marker'. See $verificationLog"
    }
}

Write-Host "Task 04 runtime verification passed. Log: $verificationLog"
