# 任务 04：Primary Asset 与 Experience 数据入口

更新：2026-09-27（Asia/Shanghai）

任务 04 把训练图所选 Experience 从规划中的字符串变成可扫描、可加载的数据资产。当前只有数据入口和同步验收探针；进入地图不会因此自动装配玩法或生成 Mini 角色。

## 类型与资产

| PrimaryAssetType | 原生类 | 扫描目录 | 当前实例 ID |
| --- | --- | --- | --- |
| `MiniExperienceDefinition` | `UMiniExperienceDefinition` | `/Game/Mini/System/Experiences` | `MiniExperienceDefinition:DA_MiniPracticeExperience` |
| `MiniExperienceActionSet` | `UMiniExperienceActionSet` | `/Game/Mini/System/ActionSets` | `MiniExperienceActionSet:DA_MiniPracticeActionSet` |
| `MiniPawnData` | `UMiniPawnData` | `/Game/Mini/System/PawnData` | `MiniPawnData:DA_MiniPracticePawnData` |

三者都是原生 `UPrimaryDataAsset` 的**实例资产**，不是 Experience 蓝图类或类默认对象。`Config/DefaultGame.ini` 对各类型使用对应 `/Script/FPS.*` 原生基类、`bHasBlueprintClasses=False`、`bIsEditorOnly=False` 和 `CookRule=AlwaysCook`。`UMiniAssetManager` 定义固定类型名，`Config/DefaultEngine.ini` 将 `AssetManagerClassName` 设为 `/Script/FPS.MiniAssetManager`；项目默认值是 `DefaultExperienceId=MiniExperienceDefinition:DA_MiniPracticeExperience`。此类型和名称也是资产 ID 的一部分，改名需要同步修改配置与地图引用。

`DA_MiniPracticeExperience` 保存 `DefaultPawnData`、`GameFeaturesToEnable`、`Actions`、`ActionSets`。它现在硬引用 `DA_MiniPracticePawnData` 和一个空的 `DA_MiniPracticeActionSet`；两个 Action／GameFeature 列表均为空，因此尚不激活插件或执行 Action。ActionSet 为空是有效的引导状态。PawnData 目前只定义 `PawnClass`，暂设 `/Script/Engine.Pawn` 以完成数据必填项验证；任务 07 会换成正式 Mini 角色，并逐步加入能力、输入和相机数据。临时 Pawn 不表示已有可操作的第三人称角色。

`AMiniWorldSettings` 以 `TSoftObjectPtr<UMiniExperienceDefinition>` 保存地图覆盖项。训练图 `L_MiniPractice.umap` 已保存该类和指向 `DA_MiniPracticeExperience` 的覆盖项；在新编辑器进程重开地图后仍能读取。非空覆盖项必须解析为已扫描、类型正确的 ID，错误路径会报告失败；空覆盖项允许任务 05 按项目默认值回退。`GetDefaultGameplayExperience` 只负责解析地图数据，不负责加载、复制或启动对局。

## 数据检查与本次验收

`UMiniAssetManager` 可校验项目默认 ID、按软对象路径解析 ID，并提供任务 04 使用的同步加载辅助方法。加载后检查资产实际类、返回的 ID 与必填数据。Experience 缺少 `DefaultPawnData`、PawnData 缺少 `PawnClass`、Action／ActionSet 空条目，以及空的 GameFeature 名称均有明确错误；编辑器数据验证也使用这些规则。未知 ID 不会被当作一个有效 Experience。同步辅助方法用于数据制作和探针，任务 05 会建立异步加载链。

从工程根目录可重复执行：

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\Task04CreateAssets.ps1
.\Scripts\Task04VerifyAssets.ps1
.\Scripts\VerifyTask04.ps1
```

资产制作脚本只创建／更新上述三个 Mini 数据资产及训练图配置；已存在同名但类型不符的资产会报错，不会覆盖。`Task04VerifyAssets.ps1` 在**新编辑器进程**检查保存后的原生实例类、引用关系、空 ActionSet、地图 WorldSettings 和四个出生点、六处掩体，并确认扫描范围内训练 Experience 名称只出现一次。`VerifyTask04.ps1` 以未 Cook 的 `UnrealEditor-Cmd.exe -game -MiniProbeExperience` 加载默认图，检查配置 ID、扫描后的对象路径、同步加载和地图覆盖项一致；还分别检验未知 ID、缺少 DefaultPawnData 与缺少 PawnClass 的失败说明。

本机 `FPSEditor Win64 Development`、`FPS Win64 Development` 均构建成功。资产新进程复核写入 `MINI_TASK04_ASSETS_VERIFIED`；运行探针写入 `MiniExperienceProbe PASS` 与 `MiniExperienceProbe negative cases PASS`，退出码为 0。任务 01–03 的验证脚本在本次变更后也通过。完整本地日志分别在 `Saved/Logs/Build-FPSEditor-Win64-Development.log`、`Build-FPS-Win64-Development.log`、`Task04-CreateAssets.log`、`Task04-VerifyAssets.log` 和 `Task04-RuntimeVerification.log`。这些生成日志不入库。

## 边界与后续

三类资产的 `AlwaysCook` 是**烹饪规则配置**，本次尚未执行 Cook 或打包，也未证明独立 Game 程序按 ID 取到资产。地图本身的 Cook 清单及打包后引用链仍需后续验证。当前默认 GameMode 仍为引擎 `GameModeBase`；地图配置和项目默认 ID 仅被探针读取，未接入开局流程。

任务 05 创建 GameMode、GameState 与 ExperienceManager，按“地图覆盖 → 项目默认”选择 ID，让服务器和客户端分别异步加载，并区分 Loading、Loaded、Failed 和退出清理。任务 06 再接 GameFeature 激活、Actions 执行与回收；不能因为任务 04 已能同步加载数据就宣布 Experience 已可玩。
