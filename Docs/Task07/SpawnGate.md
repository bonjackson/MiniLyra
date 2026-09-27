# 任务 07：Modular 角色骨架与出生门控

任务 07 把 Experience 的 `Loaded` 状态接到服务端玩家出生。训练 Experience 的 PawnData 指向 `BP_MiniCharacter`；服务器只在 Experience 已完成资产加载、GameFeature 激活和 Action 执行后生成角色。PawnData 在角色延迟生成的 `FinishSpawning` 之前写入，保证 `BeginPlay` 能读到出生配置。这里建立角色与玩家对象的骨架，四阶段初始化协作属于任务 08。

## 对象与数据

| 对象 | 本次职责 |
| --- | --- |
| `AMiniGameMode` | 设置 Mini GameState、PlayerController、PlayerState 和 HUD 类；保持 `DefaultPawnClass=nullptr`，按 Experience 状态决定何时重启玩家，并从 PawnData 选择角色类。 |
| `AMiniPlayerState` | 继承 `AModularPlayerState`；服务器一次性指定并复制 `PawnData`，客户端通过 `OnRep_PawnData` 得到当前值。同一数据的重复指定允许，试图换成另一份数据会拒绝。 |
| `AMiniPlayerController` | 继承 `AModularPlayerController`，成为后续输入与扩展组件的接收者。 |
| `UMiniLocalPlayer` | 继承 `UCommonLocalPlayer`；`Config/DefaultEngine.ini` 将 `LocalPlayerClassName` 指向该类，供后续本地输入与 UI 使用。 |
| `AMiniCharacter` | 继承 `AModularCharacter`；复制 PawnData，只接受服务器在 `BeginPlay` 前的首次设置，记录生成与复制日志。 |
| `AMiniHUD` | 最小 `AHUD`，在组件预初始化时注册 ModularGameplay receiver，在 `BeginPlay` 发送 `GameActorReady`，在 `EndPlay` 移除 receiver；尚无可见战斗 UI。 |

`/Game/Mini/Characters/BP_MiniCharacter` 继承 `AMiniCharacter`，当前使用迁入的 Manny Simple 骨骼网格和静态姿势。`/Game/Mini/System/PawnData/DA_MiniPracticePawnData` 的 `PawnClass` 指向该蓝图；训练 Experience 仍引用这份 PawnData。`Scripts/Task07CreateAssets.ps1` 调用同名 Python 脚本创建／修复这些引用，并保留任务 06 已有的 Experience 和 GameFeature Actions。

`/MiniShooterCore/GameFeatureData` 新增一个 UE 内置 `Add Components` Action：目标是原生 `AMiniCharacter`，注入 `UMiniCharacterFeatureMarkerComponent`。该组件只记录本地添加、移除和同类活动数量，不作为复制的玩法状态。它验证插件能处理 Experience 加载后才出现的蓝图角色；既有 GameState marker 仍用于任务 06 的装配验证。

代码审查又补了一道数据防护：`UMiniPawnData::ValidatePawnData` 除了检查 PawnClass 非空，还要求它继承 `AMiniCharacter`。这样误填普通 `APawn` 的 Experience 会在资产校验时失败，避免进入 `Loaded` 后才发现无法生成 Mini 角色。`Task04CreateAssets.py` 对**新建**的 PawnData 使用原生 `AMiniCharacter` 作占位，已有 PawnData 保留原引用，不会把任务 07 的蓝图类重置；`UMiniGameInstance` 的负例探针加入普通 `APawn` 被拒绝的断言。补丁后的构建与任务 04 探针已通过。

## 出生顺序

1. `MiniGameMode` 选择 Experience，`MiniGameState` 上的 Manager 在服务器及各客户端分别加载。`DefaultPawnClass` 保持空值，避免引擎提前使用默认 Pawn。
2. `HandleStartingNewPlayer` 在服务器 Experience 未 `Loaded` 时直接门控；此时已连接玩家等待。Manager 到达 `Loaded` 后的回调遍历尚无 Pawn 且允许重启的 Controller。晚加入玩家经过同一个已加载判断后走正常 `RestartPlayer`。
3. `RestartPlayer` 从 PlayerState 已有数据或当前 Experience 的 `DefaultPawnData` 取得 PawnData，要求 PawnClass 是 `AMiniCharacter` 的子类，再由服务器设置 PlayerState 的 PawnData。失败时阻止生成并记录原因。
4. `GetDefaultPawnClassForController` 仅在 Loaded 后返回 PawnData 指定的类。`SpawnDefaultPawnAtTransform` 延迟生成该类，先向 Pawn 写入同一份 PawnData，再调用 `FinishSpawning`。Pawn 的 `BeginPlay` 因而能看到配置，随后正常 Possess、复制给客户端。
5. PlayerState 与 Pawn 分别复制 PawnData。GameFeature 的组件请求覆盖已有和后来生成的 Modular 角色，在服务器和客户端 World 各自添加 marker。

这个门控以**服务器 Experience 已加载**为出生条件；客户端本地 Experience 加载、PawnData 复制和后续输入准备仍是各自的生命周期。不能把服务器生成角色解释为客户端已经可以接收玩家输入。Experience 进入 `Failed` 时不会打开出生门控。

## 验证方法与记录

在项目根目录先构建两个 target、更新资产，再运行双端探针：

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\Task07CreateAssets.ps1
.\Scripts\VerifyTask07Assets.ps1
.\Scripts\VerifyTask04.ps1
.\Scripts\VerifyTask07.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -TimeoutSeconds 300 -Port 18781
.\Scripts\VerifyTask06.ps1
```

`VerifyTask07.ps1` 使用独立的未 Cook 编辑器游戏进程：先启动监听服务器与第一个客户端，要求两端都观察到两个有效角色、各自一个本地 Pawn、每个角色一个活动插件 marker；此时再启动第二个客户端，要求三个进程都观察到三个有效角色。脚本还检查服务端每次 `MiniSpawn COMMITTED` 晚于 `ExecutingActions -> Loaded`，并拒绝重复 marker。第二组进程故意选不存在的 Experience，要求两端进入 `Failed`，角色与角色 marker 始终为零。

| 检查 | 状态 | 证据位置 |
| --- | --- | --- |
| Editor／Game target 构建 | 通过 | `Saved/Logs/Build-*-Win64-Development.log` |
| 资产创建／重复执行与新进程复核 | 通过 | `Saved/Logs/Task07-CreateAssets.log`、`Saved/Logs/Task07-VerifyAssets.log` |
| 双玩家、第三人晚加入与复制快照 | 通过 | `Saved/Logs/Task07-Valid-{Server,ClientA,ClientB}.log` |
| 无效 Experience 双端不出生 | 通过 | `Saved/Logs/Task07-InvalidExperience-{Server,Client}.log` |
| PawnClass 子类约束与普通 `APawn` 负例 | 重编译及任务 04 运行／资产验证通过 | `Saved/Logs/Build-*-Win64-Development.log`、`Saved/Logs/Task04-RuntimeVerification.log` |
| 任务 05／06 回归 | 通过：双端正常／失败路径与三轮 World 周期 | `Saved/Logs/Task05-Valid-{Server,Client}.log`、`Saved/Logs/Task06-ThreeWorldCycles.log` |

**最终实测结果（2026-09-28）：** 新增 PawnClass 子类约束后，Editor 与 Game target 再次构建成功；任务 04 运行和资产验证通过，普通 `APawn` 负例被拒绝。`Task07CreateAssets.ps1` 重复运行成功；`VerifyTask07Assets.ps1` 在全新编辑器进程中确认蓝图父类为 `AMiniCharacter`、编译状态为 `BS_UP_TO_DATE`、Manny Simple 网格和 PawnData 蓝图类引用仍在，GameFeatureData 中任务 06 与任务 07 的 AddComponents Action 各一份。日志分别写出 `MINI_TASK07_ASSETS_CREATED` 和 `MINI_TASK07_ASSETS_VERIFIED`。补丁后的 `VerifyTask07.ps1` 再次通过：独立监听服务器与 ClientA 达到两人快照后，才启动晚加入的 ClientB；最终三个进程均观察到 `PlayerStates=3 Characters=3 ValidCharacters=3 LocalPawn=1 CharacterMarkers=3`。服务端只有三次 `MiniSpawn COMMITTED`，每次都晚于 `ExecutingActions -> Loaded`；无效 Experience 双端进入 `Failed`，角色与角色 marker 为零，且没有重复 marker。`VerifyTask06.ps1` 的任务 05 双进程、缺失插件负例和三轮 World 周期回归也全部通过。

## 目前边界

- `BP_MiniCharacter` 只保证可生成和可识别；动画蓝图、第三人称相机、输入、瞄准与移动表现仍按后续任务接入。
- `AMiniPlayerState` 和 `AMiniCharacter` 的 PawnData `OnRep` 当前只记录事件；PawnExtension／Hero 的四阶段条件协作、复制到达顺序与重复初始化防护属于任务 08。
- PlayerState ASC、AbilitySet、生命值和重生尚未实现。PlayerState 当前拒绝替换不同 PawnData，因此运行中切换角色配置需要后续单独设计。
- 本次任务 04／05／06／07 运行与联机探针使用未 Cook 的编辑器游戏进程与 NullRHI；不证明图形呈现或独立打包程序联机。同进程不同 Experience 并存，以及任务 06 记录的插件跨 World 注入边界仍待验证或处理。
