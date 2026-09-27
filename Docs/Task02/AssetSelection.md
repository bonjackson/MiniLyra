# 任务 02：素材取舍与灰盒训练图

更新：2026-09-27（Asia/Shanghai）

## 来源与审计方法

活动工程从任务 01 的空 C++ 基线继续。旧工程内容保留在本机 `Backups/Task01_PreReset_20260927`，不属于当前活动 `Content`。本次在独立的 `Saved/Task02AssetAudit` 审计工程中检查资源：角色来自 UE 5.8 的 Manny／Quinn 模板素材，武器来自本机 `F:\LyraStarterGame`。原 Lyra 工程没有因本次迁移而修改。

`Scripts/AuditLyraAssets.py` 使用 Unreal AssetRegistry 读取包的硬引用与软引用，并递归展开 `/Game` 依赖；这与 Reference Viewer 查看的是同类包依赖。原始节点、类型和引用边保存在 [SourceAssetAudit.json](SourceAssetAudit.json)。报告共有 51 个节点、10 个审计种子；其中 8 个种子及其硬／软依赖闭包被选入工程，合计 49 个 `.uasset`，均放在 `/Game/Mini` 下。引擎包与模块引用有记录，但没有递归审计引擎和插件内部依赖。

## 迁入的八个种子资源

| 类别 | 种子资源（当前路径均以 `/Game/Mini` 开头） | 来源与用途 |
| --- | --- | --- |
| 角色网格 | `Characters/Mannequins/Meshes/SKM_Manny_Simple` | UE 5.8 模板；后续第三人称角色外观 |
| 角色待机 | `Characters/Mannequins/Anims/Unarmed/MM_Idle` | UE 5.8 模板；基础待机 |
| 角色行走 | `Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd` | UE 5.8 模板；基础移动 |
| 角色慢跑 | `Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd` | UE 5.8 模板；基础移动 |
| 步枪网格 | `Weapons/Rifle/Mesh/SK_Rifle` | Lyra 素材；后续装备表现 |
| 步枪开火动画 | `Weapons/Rifle/Animations/Weap_Rifle_Fire` | Lyra 素材；后续开火表现 |
| 手枪网格 | `Weapons/Pistol/Mesh/SK_Pistol` | Lyra 素材；后续装备表现 |
| 手枪开火动画 | `Weapons/Pistol/Animations/Weap_Pistol_Fire` | Lyra 素材；后续开火表现 |

49 包包括上述种子及其骨架、物理资产、材质、贴图和软引用资产。例如 Manny 骨架的软引用带入 Quinn 简化网格及相关材质，Manny 网格的软引用带入 `CR_Mannequin_Body`。迁移后使用 Unreal 资产重命名将路径统一为 `/Game/Mini/Characters/...` 与 `/Game/Mini/Weapons/...`，避免依赖旧 `/Game/Characters` 和 `/Game/Weapons` 路径。

报告中另外审计了 `Weapons/Rifle/Mesh/SM_Rifle` 与 `Weapons/Pistol/Mesh/SM_Pistol`。本期已有对应骨骼网格供后续装备使用，静态展示网格没有进入 8 个迁移种子或 49 包闭包；以后需要展示物或拾取模型时可重新评估。

## 原版玩法资产的取舍

审计报告中的以下示例不是可直接迁入的基础素材。它们含原版玩法蓝图、`LyraGame` 类或其他插件依赖，本次不移入活动工程：

| 原版示例 | 本项目的后续实现 |
| --- | --- |
| `Characters/Heroes/B_Hero_Default` | 任务 07–11 建立 Mini Pawn、初始化组件、输入与相机 |
| `System/Experiences/B_LyraDefaultExperience` | 任务 04–06 建立 Mini Experience 数据及加载装配 |
| `Characters/Heroes/SimplePawnData/SimplePawnData` | 任务 07–08 建立 Mini PawnData 与角色初始化 |
| `Weapons/GA_Weapon_Fire` | 任务 16 以 Mini 装备和能力链实现开火 |
| `Weapons/Pistol/GA_Weapon_Reload_Pistol` | 任务 17 实现装填能力与手枪配置 |

这些是待重建的功能入口，不表示任务 02 已具备角色控制、GAS、射击或联机玩法。

## `/Game/Mini` 与默认地图

`/Game/Mini/Maps/L_MiniPractice` 是约 4000 × 3000 cm 的灰盒训练图：一块地面、四周边墙、6 处掩体、4 个 `PlayerStart`、3 组静态可见训练靶及一盏方向光。训练靶目前只是几何体，不处理命中或伤害。`Config/DefaultEngine.ini` 的 `EditorStartupMap` 和 `GameDefaultMap` 均指向该地图；地图使用 `/Script/Engine.WorldSettings`，Experience 与专用 WorldSettings 留待任务 04。

地图和资源共 50 个 `/Game/Mini` 包：49 个 `.uasset` 加 1 个 `.umap`。灰盒图提供后续功能的稳定加载入口，不代表可玩的训练模式已经完成。

## 验证范围

执行 `Scripts/VerifyTask02.ps1` 后，UE 5.8 编辑器 commandlet 正常退出，并在日志中写入 `MINI_TASK02_VERIFICATION_PASSED`。脚本逐个加载 49 个资源包，重新打开地图，检查 4 个出生点、6 个掩体、3 个靶牌、地面尺度、引擎 `WorldSettings`，并检查这些包没有 `/Script/LyraGame` 或旧 `/Game` 路径的硬／软包依赖。结构化结果见 [Verification.json](Verification.json)，本机日志位于 `Saved/Logs/Task02-Verification.log`。

这是命令行编辑器加载与包引用验证；图形界面、PIE、独立程序、打包和联机尚未执行。任务 02 不需要额外下载安装插件。PythonScriptPlugin 仅在命令行验证中临时启用；ControlRig 属于引擎提供的功能，后续如实际使用其动画图，再检查相应项目配置。
