[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateSet('Training', 'Assembly', 'Loadout', 'Config', 'Cook', 'Map')]
    [string[]]$Groups = @('Training', 'Assembly', 'Loadout', 'Config', 'Cook', 'Map')
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Editor unavailable: $editor" }
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null

# Dependencies matter: the target BP exists before AddActors is saved;
# Diagnostics PawnData exists before its loadout is linked; the map is last.
$assetGroups = @(
    @{Name='Training'; Create='Task20TrainingAssets.py'; Verify='Task20TrainingVerify.py'; Created='MINI_TASK20_TRAINING_ASSETS_CREATED'; Verified='MINI_TASK20_TRAINING_ASSETS_VERIFIED'},
    @{Name='Assembly'; Create='Task20AssemblyCreateAssets.py'; Verify='Task20AssemblyVerifyAssets.py'; Created='MINI_TASK20_ASSEMBLY_CREATED'; Verified='MINI_TASK20_ASSEMBLY_VERIFIED'},
    @{Name='Loadout'; Create='Task20LoadoutAssets.py'; Verify='Task20LoadoutVerify.py'; Created='MINI_TASK20_LOADOUT_ASSETS_CREATED'; Verified='MINI_TASK20_LOADOUT_ASSETS_VERIFIED'},
    @{Name='Config'; Create='Task20ConfigVariants.py'; Verify='Task20ConfigVerify.py'; Created='MINI_TASK20_CONFIG_VARIANTS_CREATED'; Verified='MINI_TASK20_CONFIG_VARIANTS_VERIFIED'},
    @{Name='Cook'; Create='Task20CookAssets.py'; Verify='Task20CookVerify.py'; Created='MINI_TASK20_COOK_ASSETS_CREATED'; Verified='MINI_TASK20_COOK_ASSETS_VERIFIED'},
    @{Name='Map'; Create='Task20PracticeMap.py'; Verify='Task20PracticeMapVerify.py'; Created='MINI_TASK20_PRACTICE_MAP_CREATED'; Verified='MINI_TASK20_PRACTICE_MAP_VERIFIED'}
)
foreach ($group in $assetGroups) {
    if ($group.Name -notin $Groups) { continue }
    foreach ($stage in 'Create', 'CreateAgain', 'Verify') {
        $scriptName = if ($stage -eq 'Verify') { $group.Verify } else { $group.Create }
        $marker = if ($stage -eq 'Verify') { $group.Verified } else { $group.Created }
        $logPath = Join-Path $logDirectory "Task20-$($group.Name)-$stage.log"
        if (Test-Path -LiteralPath $logPath -PathType Leaf) { Remove-Item -LiteralPath $logPath }
        & $editor (Join-Path $projectRoot 'FPS.uproject') '-run=pythonscript' `
            "-script=$(Join-Path $PSScriptRoot $scriptName)" '-EnablePlugins=PythonScriptPlugin' `
            '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' '-ddc=InstalledNoZenLocalFallback' `
            "-LocalDataCachePath=$cacheDirectory" "-abslog=$logPath" | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $logPath) -or
            -not (Select-String -LiteralPath $logPath -SimpleMatch $marker -Quiet)) {
            throw "Task 20 $($group.Name) $stage failed. See $logPath"
        }
        Write-Host "Task 20 $($group.Name) $stage passed. Log: $logPath"
    }
}
