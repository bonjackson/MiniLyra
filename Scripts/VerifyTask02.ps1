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
$verificationLog = Join-Path $logDirectory 'Task02-Verification.log'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null

& $editor (Join-Path $projectRoot 'FPS.uproject') `
    '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot 'VerifyTask02.py')" `
    '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
    "-abslog=$verificationLog" | Out-Null
$editorExitCode = $LASTEXITCODE
if ($editorExitCode -ne 0) {
    throw "Task 02 verification failed with exit code $editorExitCode. See $verificationLog"
}
if (-not (Select-String -LiteralPath $verificationLog -SimpleMatch 'MINI_TASK02_VERIFICATION_PASSED' -Quiet)) {
    throw "The editor exited without the Task 02 success marker. See $verificationLog"
}
Write-Host "Task 02 Unreal verification passed. Log: $verificationLog"
