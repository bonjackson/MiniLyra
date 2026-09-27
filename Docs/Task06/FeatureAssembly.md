# 任务 06：GameFeature 装配与可撤销 Actions

任务 05 只完成了 Experience 的资产加载和状态门控。本任务让训练 Experience 真正激活 `MiniShooterCore`，然后执行 Experience 自身的 Action；退出或失败时回收该 World 持有的 Action 和插件使用权。服务器仍只复制 Experience ID，监听服务器和客户端分别在本地完成插件激活与组件注入，全部完成后才进入 `Loaded`。

## 内容与装配顺序

| 对象 | 本次配置与作用 |
| --- | --- |
| `Plugins/GameFeatures/MiniShooterCore` | 内容型 GameFeature 插件；`ExplicitlyLoaded=true`、`EnabledByDefault=false`、初始状态为 `Registered`。工程在 `FPS.uproject` 中启用它，以便 UE 发现插件并挂载 `/MiniShooterCore` 内容根；Experience 再按需激活。 |
| `/MiniShooterCore/GameFeatureData` | 插件内的 `UGameFeatureData` 实例。使用 UE 内置 `UGameFeatureAction_AddComponents`，向本地 `AMiniGameState` 添加 `UMiniFeatureMarkerComponent`。 |
| `/Game/Mini/System/Experiences/DA_MiniPracticeExperience` | 与其 ActionSet 都声明 `MiniShooterCore`，借此验证按插件 URL 去重；Experience 自身另有一个内置 AddComponents Action，为 GameState 添加 `UMiniExperienceActionMarkerComponent`。 |
| `/Game/Mini/System/Experiences/DA_MiniMissingFeatureExperience` | 保留合法 PawnData，却要求不存在的 `MiniDefinitelyMissing`，供双端必需插件失败探针使用。 |

`UMiniExperienceManagerComponent` 在资产校验后进入 `LoadingFeatures`，汇总 Experience 和 ActionSet 的插件名，借 `UGameFeaturesSubsystem::GetPluginURLByName` 解析并按 URL 去重，逐个异步调用 `LoadAndActivateGameFeaturePlugin`。所有插件成功后，进入 `ExecutingActions`，按 Experience、ActionSet 的顺序激活 Action，最后才广播 `Loaded`。名称解析或激活失败进入 `Failed`，不会让角色开始游戏。当前 ActionSet 仅声明同一插件，没有自己的 Action。

没有为示例组件另造一种注入机制：插件内和 Experience 内都复用 UE 的 `UGameFeatureAction_AddComponents`。它通过 ModularGameplay 的组件请求处理已有和后来出现的 GameState，并由 Action 的停用路径撤回。两个 marker 只记录 `BeginPlay`/`EndPlay`、World、网络角色及同类活动组件数；其 `IsFeatureActive()` 便于运行时观察，不承载复制的玩法状态。`FGameFeatureComponentEntry` 在当前 UE Python 接口中无法直接编写，`UMiniTask06AssetSetupLibrary` 因而只在编辑器构建中提供一个窄 C++ 桥接，设置目标 Actor、组件类以及客户端／服务器两端启用标志。资产脚本可重复运行，并检查已有资产类型、Action 唯一性及引用。

## 生命周期与回滚

每个 Experience Manager 保存本 World 的插件激活 lease、已激活的 **Experience 自有 Action** 和 WorldContextHandle。Experience 自有 Action 使用带该 WorldContextHandle 的激活／停用上下文；停用时按逆序撤销，等待其异步 pauser 完成后再释放插件 lease。`EndPlay` 先废弃加载代次并取消未完成的资产加载，再回收 Action 与插件使用权；失败路径同样回滚已经取得的贡献。回调使用弱对象引用和代次检查，已结束的 World 不再推进 Experience 状态。

GameFeatures 子系统的插件状态是进程级的，不能把一个 World 退出当作所有 World 都不再使用插件。Manager 的进程级 URL 使用者表从发起激活请求起计数，**包括仍在途的请求**；最后一个 lease 释放时才调用 `DeactivateGameFeaturePlugin`。如果 World 先结束、激活回调后到，lease 会在回调后释放，避免把另一个 World 正在使用的插件提前停掉。Experience 自有 Action 实例也有进程级使用计数；只在最后一个 World 释放它时调用 `OnGameFeatureUnloading` 和 `OnGameFeatureUnregistering`。这为后续 Experience 自有 AddAbilities、Input、Widget Action 建立了 World 作用域和回收规则。

本任务不支持运行中热切换整个 Experience。缺失插件的负例覆盖**名称无法发现**，尚未用一个可发现但激活过程报错的插件，或受控的“激活回调与 EndPlay 交错”测试这些更窄的竞态。双进程服务器／客户端有各自的进程级插件表；连续三次 PIE 依次启动、停止，尚未覆盖同一个编辑器进程中多个 PIE World **同时存活**时的引用计数行为。尤其要注意：UE 内置 GameFeatureData 中的 AddComponents 随插件在**进程级**激活，不受 Experience 自有 Action 的 WorldContextHandle 约束；同一编辑器进程若同时运行不同 Experience 的多个 PIE World，插件组件可能注入到没有请求该插件的 World。当前 lease 只防止插件过早卸载，不能消除这种跨 World 注入。后续有不同 Experience 并存需求时须限制该插件 Action 的 World 适用范围或改为 Experience 自有、带 WorldContextHandle 的 Action，并做并存验收。

## 复现与证据

从项目根目录执行：

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\Task06CreateAssets.ps1
.\Scripts\VerifyTask06.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -TimeoutSeconds 180 -Port 18779
.\Scripts\VerifyTask06PIE.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -TimeoutSeconds 240
```

资产脚本通过 `UnrealEditor-Cmd.exe -run=pythonscript` 创建或更新 GameFeatureData、训练 Experience、ActionSet 及负例 Experience。重复运行仍只有一个任务 06 Action 和一份 `MiniShooterCore` 声明。日志位于 `Saved/Logs/Task06-CreateAssets.log`。

`VerifyTask06.ps1` 先重用任务 05 的真实监听服务器／客户端探针，要求两端分别达到 `Loaded`、各注入两个不同类型的 marker 且每种活动数量为 1，并确认 Experience 与 ActionSet 中重复声明的插件只触发一次解析和激活请求。然后以同样的双进程形式选择缺失插件 Experience，要求两端在 `LoadingFeatures` 转为 `Failed`、报告插件名，且没有 marker 或 `Loaded`。最后一个独立的未 Cook 编辑器游戏进程使用 `-MiniProbeFeatureCycles=3`，在同一进程中对训练图执行三轮 World travel：每轮加载、两类 marker 各添加一次、切图时各撤销一次、释放插件 lease。这是额外的 World 重建检查；相关日志为 `Saved/Logs/Task05-Valid-{Server,Client}.log`、`Task06-MissingFeature-{Server,Client}.log`、`Task06-ThreeWorldCycles.log`。

`VerifyTask06PIE.ps1` 启动真实 `UnrealEditor.exe`，`Task06PIE.py` 在同一个编辑器进程内依次请求三次 Play In Editor 并逐次结束。脚本核对每轮 `UEDPIE_0_L_MiniPractice` World、Experience `Loaded`、两种 marker 各添加一次（`Count=1`）且退出时各撤销一次（`Count=0`）、Action 停用与插件 lease 释放；任一重复注入或失败日志都会令验证失败。编辑器正常退出，`Saved/Logs/Task06-PIE.log` 末尾有 `MINI_TASK06_PIE_SCRIPT_DONE Cycles=3`。

本机验证（2026-09-27）：Editor 和 Game target 构建通过；资产创建脚本重复运行未再出现 GameFeatureData 加载错误；上述双端有效／缺失插件场景、三轮 World travel 及**真实连续三次 PIE** 均通过。有效场景两端分别有 `MiniFeature ActivationSucceeded`、两种 marker 的 `Count=1` 和 `MiniFlowProbe PASS`；PIE 三轮均有 `UEDPIE` World、marker `Count=1 → 0`、`MiniAction Deactivated` 和 `MiniFeature Release`，无 `MiniFeatureMarker DUPLICATE`。这些探针运行在编辑器或未 Cook 编辑器游戏中，尚未验证打包程序联机。

最小 Windows 地图 Cook 已以 `/Game/Mini/Maps/L_MiniPractice` 为入口运行，进程退出码为 0；`Saved/Logs/Task06-Cook.log` 报告 513 个已 Cook 包、0 error、0 warning，插件在启动日志中注册为 `Registered`。Cook 输出位于 `C:\Users\sq100\AppData\Local\Temp\MiniLyraTask06Cook-20260927`。随后用 UE 的 `DumpAssetRegistry` 读取该输出的 `FPS/AssetRegistry.bin`；`Saved/Reports/Task06CookRegistry/Page_00001.txt` 明确列出 `/MiniShooterCore/GameFeatureData.GameFeatureData`、训练 Experience 和地图，命令日志见 `Saved/Logs/Task06-CookRegistry.log`。这证明这三项进入已 Cook 的 AssetRegistry；打包程序的运行证据见下文。

完整打包烟雾检查前两次未产出可运行包：首次复用自定义 Cook 输出时，staging 期待的 `<Cook 输出>/Windows` 目录结构不符（`Saved/Logs/Task06-PackageConsole.log`）；第二次重新 Cook 成功，但 IoStore staging 三次连接 Zen 服务 `[::1]:8558` 均未就绪（`Saved/Logs/Task06-PackageFreshConsole.log`）。第三次改用 `RunUAT BuildCookRun -pak -skipiostore -AdditionalCookerOptions=-SkipZenStore`，从新目录 `C:\Users\sq100\AppData\Local\Temp\MiniLyraTask06CookLoose-20260927\Windows` Cook，并 stage 到 `C:\Users\sq100\AppData\Local\Temp\MiniLyraTask06StageLoose-20260927`。`Saved/Logs/Task06-PackageLooseConsole.log` 记录 513 个已 Cook 包、0 error／0 warning、Cook 与 Stage 均完成、`BUILD SUCCESSFUL` 和 AutomationTool 退出码 0。

随后直接启动该 stage 下的 `Windows\FPS\Binaries\Win64\FPS.exe`，加载训练图并带 `-MiniProbeExperienceFlow`；`Saved/Logs/Task06-PackagedSmoke.log` 显示 `MiniShooterCore` 解析、两种 marker 均 `ADDED Count=1`、Experience 进入 `Loaded`、`MiniFlowProbe PASS`。因此独立打包程序的**单机启动与功能装配烟测通过**。本次使用 Pak 且跳过 IoStore／Zen Store；前述 IoStore staging 的 Zen 连接问题仍在。尚未在打包程序中验证监听服务器／客户端联机，也未验证同进程不同 Experience World 并存。

复现这条成功的最小打包路径，先构建 `FPS` Game target，再从项目根目录运行；Cook 与 Stage 使用 C 盘临时目录：

```powershell
$uat = 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat'
$cookWindows = Join-Path $env:TEMP 'MiniLyraTask06CookLoose-Recheck\Windows'
$stageDir = Join-Path $env:TEMP 'MiniLyraTask06StageLoose-Recheck'
& $uat BuildCookRun '-project=F:\FPS\FPS\FPS.uproject' -noP4 -platform=Win64 `
    -clientconfig=Development -skipbuild -cook -stage -package -pak -skipiostore `
    '-AdditionalCookerOptions=-SkipZenStore' "-cookoutputdir=$cookWindows" `
    "-stagingdirectory=$stageDir" '-map=/Game/Mini/Maps/L_MiniPractice' -unattended
```

单独复现 Cook 与注册表检查可使用以下命令；Cook 输出位于临时目录：

```powershell
$editorCmd = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$cookOutput = Join-Path $env:TEMP 'MiniLyraTask06Cook-Recheck'
& $editorCmd 'F:\FPS\FPS\FPS.uproject' '-run=Cook' '-TargetPlatform=Windows' '-Map=/Game/Mini/Maps/L_MiniPractice' "-OutputDir=$cookOutput" '-unattended' '-nop4' '-nullrhi' '-nosound' '-ddc=InstalledNoZenLocalFallback' '-LocalDataCachePath=F:\FPS\FPS\DerivedDataCache' '-abslog=F:\FPS\FPS\Saved\Logs\Task06-Cook-Recheck.log'
& $editorCmd 'F:\FPS\FPS\FPS.uproject' '-run=DumpAssetRegistry' "-Path=$cookOutput\FPS\AssetRegistry.bin" '-OutDir=F:\FPS\FPS\Saved\Reports\Task06CookRegistry-Recheck' '-ObjectPath' '-Class' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' '-ddc=InstalledNoZenLocalFallback' '-LocalDataCachePath=F:\FPS\FPS\DerivedDataCache' '-abslog=F:\FPS\FPS\Saved\Logs\Task06-CookRegistry-Recheck.log'
```
