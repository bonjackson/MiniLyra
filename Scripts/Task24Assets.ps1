[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateSet('All', 'Create', 'CreateAgain', 'Verify')][string]$Stage = 'All'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logs, $cache -Force | Out-Null
$protectedPaths = @('Content\Mini\Maps\L_MiniPractice.umap', 'Content\Mini\Maps\L_MiniArena.umap',
    'Content\Mini\System\Experiences\DA_MiniPracticeExperience.uasset',
    'Content\Mini\System\Experiences\DA_MiniArenaExperience.uasset',
    'Content\Mini\System\ActionSets\DA_MiniPracticeActionSet.uasset',
    'Content\Mini\System\ActionSets\DA_MiniCombatActionSet.uasset',
    'Content\Mini\System\ActionSets\DA_MiniArenaActionSet.uasset',
    'Content\Mini\System\PawnData\DA_MiniPracticePawnData.uasset')
$hashes = @{}
foreach ($path in $protectedPaths) { $hashes[$path] = (Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash }
foreach ($entry in @(
    @{Name='Create'; Script='Task24CreateAssets.py'; Marker='MINI_TASK24_ASSETS_CREATED'},
    @{Name='CreateAgain'; Script='Task24CreateAssets.py'; Marker='MINI_TASK24_ASSETS_CREATED'},
    @{Name='Verify'; Script='Task24VerifyAssets.py'; Marker='MINI_TASK24_ASSETS_VERIFIED'}
)) {
    if ($Stage -ne 'All' -and $Stage -ne $entry.Name) { continue }
    $log = Join-Path $logs "Task24-Assets-$($entry.Name).log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    & $editor (Join-Path $projectRoot 'FPS.uproject') '-run=pythonscript' `
        "-script=$(Join-Path $PSScriptRoot $entry.Script)" '-EnablePlugins=PythonScriptPlugin' `
        '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' '-ddc=InstalledNoZenLocalFallback' `
        "-LocalDataCachePath=$cache" "-abslog=$log" | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $log) -or
        -not (Select-String -LiteralPath $log -SimpleMatch $entry.Marker -Quiet)) {
        throw "Task24 assets $($entry.Name) failed: $log"
    }
    foreach ($path in $protectedPaths) {
        if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -ne $hashes[$path]) {
            throw "Protected gameplay asset changed: $path"
        }
    }
    Write-Host "Task24 assets $($entry.Name) PASS. Gameplay assets unchanged. Log: $log"
}
