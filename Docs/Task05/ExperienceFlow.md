# 任务 05：Experience 异步加载与失败状态

本任务把任务 04 的 Experience 数据入口接入实际开局流程。服务器选择一个 Primary Asset ID，`MiniGameState` 上的 `MiniExperienceManagerComponent` 将 ID 复制给客户端；每个进程独立异步加载并验证该资产。**收到 ID 不等于加载完成。** 当前训练 Experience 没有 GameFeature 或 Action，因此可以走完空的装配阶段；真正的功能激活属于任务 06。

## 入口与职责

| 类 | 本任务职责 |
| --- | --- |
| `AMiniGameMode` | 服务端在 `InitGame` 后下一帧选择 Experience。地图 `AMiniWorldSettings` 的覆盖项非空时优先使用；只有覆盖项为空才读取 `UMiniAssetManager.DefaultExperienceId`。非空但无效的地图覆盖项会失败，不会静默回退。暂将玩家设为 spectator 且 `DefaultPawnClass=nullptr`，避免在任务 07 的正式出生逻辑前生成引擎默认 Pawn。 |
| `AMiniGameState` | 作为复制宿主，构造默认子对象 `UMiniExperienceManagerComponent`。带 `-MiniProbeExperienceFlow` 时注册成功／失败探针，并验证加载完成后的迟订阅立即回调。 |
| `UMiniExperienceManagerComponent` | 只允许服务器选择一次 ID；复制 `CurrentExperienceId` 和选择失败原因。服务器设置 ID 后立即开始本地加载；客户端在 `OnRep_CurrentExperienceId` 后自行加载。提供状态、失败原因、调试文本和成功／失败订阅接口。 |

项目默认 GameMode 已在 `Config/DefaultEngine.ini` 指向 `/Script/FPS.MiniGameMode`。训练图仍引用任务 04 的 `DA_MiniPracticeExperience`；它是原生 `UPrimaryDataAsset` **实例**，不是 Lyra 原版的 Experience 蓝图类／CDO。

## 状态与数据约束

正常流程为 `Unloaded → LoadingAssets → LoadingFeatures → ExecutingActions → Loaded`。`LoadingAssets` 使用 `UAssetManager::LoadPrimaryAsset`；完成回调再次检查实际加载对象类型、Primary Asset ID、`DefaultPawnData` 与 PawnClass 等必填数据。句柄为空或已完成时也会检查对象，避免已缓存资产导致流程悬空。

任务 05 暂不执行 `GameFeaturesToEnable` 或 `Actions`。Experience 或任一 ActionSet 只要包含这些条目，就从对应阶段进入 `Failed`，附带“任务 06 尚未实现”的原因；不会虚报 `Loaded`。当前训练 ActionSet 为空，可安全经过这两个阶段。`GetLoadingDebugString()` 返回当前状态或失败原因，`LogMiniExperience` 记录每次状态迁移；尚无屏幕上的加载 UI。

无效 ID、资产未扫描、加载结果类型／ID 不匹配、数据缺项及加载取消都会进入 `Failed`。如果地图覆盖项或项目默认配置在选 ID 前就失败，服务器复制 `SelectionFailureReason`，客户端也可转入 `Failed`。`CallOrRegister_OnExperienceLoaded` 和 `CallOrRegister_OnExperienceFailed` 在终态已到达时立即调用一次，否则等待本地终态；成功与失败互斥。

`EndPlay` 先标记结束并递增加载代次，再取消在途句柄、清除本地 Experience 指针和委托。异步回调同时使用弱对象引用、代次和状态检查，旧世界结束后不再推进状态。单个世界不会调用进程全局的 `UnloadPrimaryAsset`，避免影响同进程其他 PIE 世界；任务 06 需为实际激活的插件与 Action 建立各自的撤销／所有权记录。运行中热切换 Experience 不在首版范围内。

## 可重复验收

先编译 Editor 和 Game target，再运行双进程探针：

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\VerifyTask05.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -TimeoutSeconds 180 -Port 18777
```

`VerifyTask05.ps1` 用隐藏的 `UnrealEditor-Cmd.exe -game` 进程启动 `/Game/Mini/Maps/L_MiniPractice?listen`，监听就绪后启动连接 `127.0.0.1` 的客户端。正常场景要求**两端各自**写出 `MiniFlowProbe PASS`、相同的 `MiniExperienceDefinition:DA_MiniPracticeExperience` ID 和 `LateSubscriber=1`。随后脚本在两端加 `-MiniProbeInvalidExperience` 重跑；服务器故意选择未扫描的 `DA_MiniDefinitelyMissing`，要求两端写出 `MiniFlowProbe FAIL_EXPECTED` 且原因包含 `Unknown Experience ID`。脚本对启动、退出和等待设超时，并只清理自己启动的两个进程。四份本地日志分别是 `Saved/Logs/Task05-Valid-Server.log`、`Task05-Valid-Client.log`、`Task05-Invalid-Server.log`、`Task05-Invalid-Client.log`。

**验证结果（2026-09-27）：**`FPSEditor Win64 Development` 与 `FPS Win64 Development` 均构建成功。双进程探针先后通过正常和无效 ID 场景：正常场景的服务端日志确认从训练图覆盖项选择 ID、进入 `Loaded`；客户端日志确认收到同一 ID 后本地进入 `Loaded`；两端均有 `LateSubscriber=1`。无效场景两端均从 `LoadingAssets` 进入 `Failed`，原因包含 `Unknown Experience ID`。任务 03、04 的回归脚本也再次通过。日志见上述四个路径及 `Saved/Logs/Build-*-Win64-Development.log`。

结束世界时的在途回调保护已经按弱引用、加载代次、状态与取消顺序审查；由于当前资产较小且会被缓存，这次没有稳定制造“回调与 EndPlay 交错”的运行场景，**不把普通进程退出当作该竞态的动态验证**。本探针使用未 Cook 的编辑器游戏进程，不证明独立 `FPS.exe`、Cook／打包、晚加入玩家、GameFeature 装配、角色出生或 UI。任务 06 接功能插件与 Action，任务 07 接正式角色和出生门控。
