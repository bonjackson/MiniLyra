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
$verificationLog = Join-Path $logDirectory 'Task01-BaselineMap.log'

# PythonScriptPlugin ships with the engine; enable it only for this verification.
& $editor (Join-Path $projectRoot 'FPS.uproject') `
    '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot 'VerifyBaseline.py')" `
    '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' `
    '-stdout' '-FullStdOutLogOutput' "-abslog=$verificationLog"
$editorExitCode = $LASTEXITCODE
if ($editorExitCode -ne 0) {
    throw "Editor verification failed with exit code $editorExitCode. See $verificationLog"
}
if (-not (Select-String -LiteralPath $verificationLog -SimpleMatch 'MINI_BASELINE_VERIFICATION_PASSED' -Quiet)) {
    throw "The editor exited without the baseline success marker. See $verificationLog"
}
Write-Host "Baseline map verification succeeded. Log: $verificationLog"
