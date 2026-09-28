# 任务 08：PawnExtension／Hero 初始化协作

任务 08 为 `AMiniCharacter` 加入两个原生默认子组件：`UMiniPawnExtensionComponent` 协调 PawnData 与其他角色功能，`UMiniHeroComponent` 等待玩家依赖并为后续输入、相机初始化保留入口。两者实现 `IGameFrameworkInitStateInterface`，使用任务 03 已注册的 `Spawned → DataAvailable → DataInitialized → GameplayReady` 状态链。当前实际完成到 `DataInitialized`；ASC 和本地输入尚未接通，因此不会宣称角色已 `GameplayReady`。

## 状态条件

| 转换 | PawnExtension | Hero |
| --- | --- | --- |
| 未注册 → `Spawned` | 所属 Actor 是 `AMiniCharacter`；在 `OnRegister` 注册 Feature，`BeginPlay` 宣告已生成。 | 同样要求 `AMiniCharacter`，以独立的 `Hero` Feature 注册。 |
| `Spawned` → `DataAvailable` | Pawn 与 PlayerState 都已收到同一份有效 PawnData；Authority 和 AutonomousProxy 还要有 Controller，且 Controller 的 PlayerState 必须与 Pawn 的相同。 | 等待同一组 PawnData／PlayerState 条件；Authority 和 AutonomousProxy 还要有与 Pawn 的 PlayerState 配对的 Controller。 |
| `DataAvailable` → `DataInitialized` | Hero 已到 `DataAvailable`，且角色上其他已注册 Feature 也到 `DataAvailable`。 | PawnExtension 已到 `DataInitialized`。 |
| `DataInitialized` → `GameplayReady` | 暂不开放；任务 09 补齐 PlayerState ASC 与 Avatar 绑定。 | 暂不开放；任务 10 补齐本地输入绑定，并与后续相机工作衔接。 |

模拟代理在 `DataAvailable` 门槛不等待 Controller，也不检查本地 `LocalPlayer` 或 `InputComponent`。当前 Hero 只是依赖协作骨架，还没有真正绑定输入、设置相机模式，不能把 `DataInitialized` 理解为可操作角色。PawnData 仍由 `AMiniCharacter` 和 `AMiniPlayerState` 各自复制；PawnExtension 读取并协调它们，不在本任务重复建立第三份复制属性。两个组件各要求同一角色上只有一份实例，且不启用 Tick。

## 事件与生命周期

服务器继续在 `FinishSpawning` 前给 Pawn 指定 PawnData。Pawn 的首次指定及客户端 `OnRep_PawnData`、PlayerState 的首次指定及 `OnRep_PawnData` 都重新检查初始化；PlayerState 数据变化时通知引用它的 Mini 角色。`PossessedBy`、`UnPossessed`、Pawn 的 `OnRep_Controller`／`OnRep_PlayerState`、PlayerController 的 `OnRep_PlayerState`、`SetupPlayerInputComponent` 和 `BeginPlay` 也触发同一检查。后一个通知覆盖 Controller 先于其 PlayerState 到达的情况。PawnExtension 监听其他 Feature 的状态变化，Hero 监听 PawnExtension 到达 `DataInitialized`；不依赖一次性的 `BeginPlay` 时机或固定等待时间。重复事件只重新评估条件，已经完成的状态不会再次转换。

两个组件在 `OnRegister` 注册 Feature，在 `EndPlay` 注销；本任务没有 ASC、输入映射、相机委托或额外资源句柄需要回收。后续任务建立这些资源时必须补上相应解绑。状态转换会记录 Feature、网络角色、Pawn、起止状态；各组件到达 `DataInitialized` 时还各记录一次 `MiniInitState DEFERRED`，明确 PawnExtension 的 `GameplayReady` 等待 ASC、Hero 等待输入与相机，方便定位停在何处。

## 验证

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\VerifyTask08.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -TimeoutSeconds 300 -Port 18782
```

Controller／PlayerState 配对补丁后，Editor 和 Game target 均已重新构建成功，三进程探针重跑通过。`VerifyTask08.ps1` 使用三个独立、未 Cook 的编辑器游戏进程和 NullRHI：监听服务器与 ClientA 先达到两人出生和初始化快照，之后才连接 ClientB。第三人晚加入后，三端均记录 `Characters=3 ExtensionDataInitialized=3 HeroDataInitialized=3 GameplayReady=0 RepeatNotified=3`；服务器的 `SimulatedDataInitialized=0 ProbeReleased=0`，两个客户端各为 `SimulatedDataInitialized=2 SimulatedWithoutLocalInputInitialized=2 ProbeReleased=3`。后一计数确认客户端两名模拟代理在没有 Controller 和 InputComponent 时仍到达 `DataInitialized`。脚本检查每个 Pawn 的 PawnExtension／Hero 对 `Spawned`、`DataAvailable`、`DataInitialized` 各转换一次，且每个 Feature 的 `DEFERRED` 说明只出现一次；同时拒绝 `GameplayReady` 转换、重复组件及顺序探针失败。GameState 在组件就绪后连续发送三次依赖变化通知，状态转换计数仍保持一次。

两个客户端分别使用 `-MiniProbeInitOrder=DataFirst` 和 `-MiniProbeInitOrder=PlayerStateFirst`。该探针只在非 Shipping 构建中把**已真实复制到达**的 PawnData 和 PlayerState 暂时从初始化条件中隐藏；两份原始数据都到齐后，由测试快照先开放一项、再开放另一项。首次开放时两个 Feature 仍停在 `Spawned`，第二次开放后继续推进。脚本核对每个 Pawn 的实际 `RELEASE: Dependency=...` 日志顺序，并要求两个 Feature 的 `DataAvailable` 转换晚于第二次释放，而非只看 `FIRST_RELEASE`／`SECOND_RELEASE` 标记。这样确定性验证两种**条件可见性顺序**及事件重试，不会改变网络包或证明真实网络传输出现了指定顺序。探针的定期快照仅用于驱动和采样测试；正常角色初始化不使用定时 Delay。

| 检查 | 结果 | 本地日志 |
| --- | --- | --- |
| FPSEditor／FPS Win64 Development 构建 | 通过 | `Saved/Logs/Build-*-Win64-Development.log` |
| 两人、第三人晚加入及三端状态快照 | 通过 | `Saved/Logs/Task08-{Server,ClientA,ClientB}.log` |
| 两种可见性顺序、无本地输入的模拟代理、重复通知 | 最终补丁后重跑通过 | 同上，实际 `RELEASE`、`FIRST_RELEASE`／`SECOND_RELEASE`、`MiniInitState TRANSITION` 与 `MiniInitProbe SNAPSHOT` |
| 任务 07／06 回归 | 通过：任务 07 出生与无效 Experience；任务 06 含任务 05 正反例、插件负例和三轮 World 周期 | `Saved/Logs/Task07-*.log`、`Saved/Logs/Task06-*.log`、`Saved/Logs/Task05-*.log` |

任务 08 没有新增或修改 `.uasset`；现有训练地图、PawnData 和 `BP_MiniCharacter` 继续使用。打包后联机、实际画面及真实网络传输乱序不在此次动态验证范围内。
