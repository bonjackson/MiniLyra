# 任务 25：晚加入、重生、断线与普通旅行生命周期

更新：2026-10-04。前置版本为任务 24 已提交并推送的 `86ffd9b`。任务25实现与验收完成：默认及最终媒体All六模式均通过，最终Editor／Game构建与任务08／09／10／23本轮回归通过，八张精选截图已复核。独立提交／推送版本以Git记录为准。

## 本项约定

同一个 gameplay World 内重生保留 PlayerState 与其 ASC，替换 Pawn、生命 ID、装备实例及库存物品；普通地图旅行重建 World／Controller／PlayerState／ASC，不依赖旧世界身份。GameInstance 可以持久保存旅行请求，CommonGame 可以复用同一 LocalPlayer 的 Root，但旧 World 的 UI、输入与功能贡献必须释放。

Playing 期间晚加入须从当前复制状态建立 Pawn、阶段与 HUD，不能依赖曾经发生过的一次性广播。Loaded 后迟订阅应立即执行一次回调；重复依赖通知不能重复授予能力、生成物品或刷新弹药。

本项不增加运行中 Experience 热切换、主机迁移、Session 服务或新插件。真实前端按钮、地址输入、满员与返回提示的基础证明引用 [任务 24](../Task24/FrontEndTravel.md)，本项用反复生命周期补证；生产 Travel API 调用不能写成新的真实按钮验收。本机独立进程不代表跨机器 LAN；最终包仍由任务 28／29 验证。

## 当前最小生产修复

已有任务 23 日志曾在死亡／结算间出现 `CreateSavedMove: Hit limit of 96 saved moves`。`MOVE_None` 并不保证本地 autonomous CharacterMovement 停止生成预测记录，因此仅 DisableMovement 不能解决尸体继续预测的问题。

当前 `MiniHealthComponent::ApplyDeathPresentation()` 在原有 `Pawn && bDeathStarted` 门内，先 StopMovement／DisableMovement，再关闭该尸体的 CharacterMovement Tick；仅本地控制的 `ROLE_AutonomousProxy` 调用公开 `ResetPredictionData_Client()` 清旧预测队列。新生命新建 Movement 组件。Actor／Mesh Tick、死亡表现及生产重生时长保持既有规则。

活 Pawn 的全堵跌落恢复仍可暂为 `MOVE_None`，但其 Movement Tick 必须继续，撤销后真实下一 Tick 才能重新检查落界。不能把尸体修复推广到所有 `MOVE_None`。测量预测数据时先调用 `HasPredictionData_Client()`，只有存在时才读 SavedMoves，避免测量本身重新分配对象；死态允许预测对象不存在或队列为零，不要求对象永久为空。

默认及最终媒体All均完整通过四人40次真实环境GE死亡／40次生产重生、三位remote每轮Dead／Live真实ACK、FireHeld／ReloadActive死亡取消、新生命真实输入，以及退出重连和实际主机丢失。正常联机与全部Dead checkpoint严格检查Prediction=0；本轮任务23的四人PostMatch及下一局回归通过。

## 已集成的诊断

| 文件 | 实际职责与限制 |
| --- | --- |
| `Source/FPS/Diagnostics/MiniTask25ProbeActor.h/.cpp` | transient、owner-only复制检查点；携带serial、iteration、预期Pawn／PS／life、phase／match及客户端命令。服务器ACK核验实际PC所有权、身份、统计、服务器时钟与owner实测输入／prediction／HUD快照，不接受提前或重复ACK |
| `Source/FPS/Diagnostics/MiniTask25ProbeSubsystem.h/.cpp` | GI持久tickable诊断，仅非Shipping且`-MiniProbeTask25=`选择时创建；实现六模式，有限强保留旧对象，PostWorldCleanup与真实NetworkFailure观测；Deinitialize移除两项delegate、释放观察／owner记录并清碰撞fixture |
| `Scripts/VerifyTask25.ps1` | All按Stress、RoundTrip、RecoveryAction、RecoveryLogout、RecoveryTravel、RecoveryDeath运行；独立GUID信号目录，跟踪自己启动的进程。严格核对每人／每轮死亡与重生、每轮3个remote Dead／Live ACK、输入及旅行计数；HostLoss只在armed后实际终止tracked host，允许相应客户端真实断线，其他异常立即失败 |

runner默认端口18926、每模式超时480秒，支持单模式、Recovery组、`-WithMedia`和可选`-PackagedExe`。未Cook默认使用Editor-Cmd `-game`；媒体配置为960×540、30 FPS及脚本列明画质。默认All命令行请求五秒连接超时并未成为实际GameNetDriver阈值；本轮三个客户端的真实`ConnectionTimeout`均记录`Threshold: 60.00`。媒体保存真实PNG后再检查文件与marker，不能将截图请求当图形验收。

正常联机与死亡检查不允许`CreateSavedMove: Hit limit of 96 saved moves`。实际终止tracked host后，UE等待六十秒连接超时期间仍存活的本地Pawn可能出现该提示；本轮只在已armed并由runner真正停止host后的断线等待阶段允许，仍要求三客户端由真实驱动失败返回前端并清旧源。不能将这轮日志写为“无任何warning”，也不能用此例外放宽尸体预测或正常联机告警。

无人值守runner显式带`-MiniTask25IsolateInput`，仅在非Shipping且同时选择Task25 probe时隔离送到PlayerController的非模拟设备事件，并记录按下／释放；正常游戏不开启。计划按键仍使用引擎`CreateSimulated`并经过PlayerInput／EnhancedInput／GAS完整链，Slate按钮继续真实路由鼠标事件。死亡取消状态在释放模拟硬件按键之前测量。新增节流`ACK_WAIT`输出身份、弹药、raw key及HUD差异，失败不会靠补弹或伪造ACK消除。

此前媒体尝试在ClientC第10次重生已确认满弹药与移动之后出现一对新的Fire按下／释放，服务器真实接受一发，将相同GUID步枪变为29/90，使下一Fresh检查等待；该owner没有计划Arm命令，截图函数没有输入路径。旧日志不能进一步区分物理设备与其它非模拟Slate来源。最终开启上述显式诊断隔离后完整六模式媒体通过；产品开火、补弹及死亡取消规则未放宽。

## 已确认的回收观测与诊断修正

旧PS／ASC被诊断强保留供测量时，客户端在World销毁后可能仍持有复制的静态ability spec描述；它不代表能力仍被授予或活动。此前诊断把客户端旧`EquipmentSpecs=2`直接当残留授予，导致完整Stress在退出观察阶段误fail。当前仅调整诊断区分静态副本和实际活动来源，不改变产品GAS撤销规则。

2026-10-03独立RecoveryLogout的客户端真实观测为：`PC=0 PS=0 Pawn=0 Avatar=0 Input=0 Held=0 EquipmentSpecs=2 ActiveSpecs=0 ASCRegistered=0`；VM运行／绑定／snapshot、HUD元素／插槽、菜单／gate／前端及Modal listener全部为0。服务器权威旧装备SourceObject specs与授予handles仍严格要求0，所有端旧active specs／held input为0、ASC未注册且Avatar解绑。见 [RecoveryLogout.txt](Evidence/RecoveryLogout.txt)。

此区别只适用于已结束并刻意保留的客户端旧World对象。同World每个新life仍要求全部spec总数与活态基线一致、previous SourceObject specs=0、当前装备来源唯一；新World必须采用不同PS／ASC。不能用静态副本说明放宽重复授予、当前生命旧来源或跨World身份检查。

## 注册、授予与释放映射

下表来自 `Saved/Task25Draft/LifecycleAudit.md`、`ImplementationPlan.md` 及必要活动源码复核。释放函数存在只证明代码路径明确，运行覆盖另列；本次有限审查未确认新的必然泄漏，不据此宣称所有异步时序均已通过。

| 资源与所有者 | 注册／授予 | 释放与身份约束 | 本项观察 |
| --- | --- | --- | --- |
| Experience 异步 handle | `StartExperienceLoad` | EndPlay／Fail 先递增 `LoadGeneration`，再 `CancelPendingLoad`；callback 检查 generation 与结束状态 | 旧 World 不再 Loaded／出生 |
| Loaded／Failed 一次性委托 | `CallOrRegister_OnExperienceLoaded/Failed` | 已终态直接 Execute；终态 Broadcast 后 Clear，EndPlay Clear | Loaded 后迟订阅一次，旧 World 不再回调 |
| GameFeature activation lease | `ActivateNextGameFeature` | 激活完成与释放请求协调；最后使用者才停用，未完成 lease 待 completion 处理 | 旧使用者释放，不影响其他 World |
| Experience Action | `ExecuteExperienceActions` | 按 World context 逆序 deactivate；cleanup 持有 Action／lease，等 pausers 后 unload／unregister／release | 旧 context 贡献归零 |
| PawnData AbilitySets | `MiniPlayerState::SetPawnData` | 同 asset 幂等、拒绝更换；PS EndPlay `TakeFromAbilitySystem` | 同 World 重生不重复基础能力，旅行新 ASC |
| Pawn ASC Avatar | `MiniPawnExtensionComponent::InitializeAbilitySystem` | Uninitialize 先清成员，只有 ASC 当前 Avatar 仍是自己才 Cancel／SetAvatar(null)；EndPlay 标记 superseded | 旧 Pawn 不清新 Avatar／不重新抢 ASC |
| InitState receiver／listener | PawnExtension／Hero 的 Register／Bind | 两组件 EndPlay `UnregisterInitStateFeature` | 新 Pawn 在两种依赖顺序都 Ready |
| Hero 死亡 Tag 重试监听 | `RefreshInputReadinessASC` | `ClearInputReadinessASC` 解除自身 handle；输入停用、ASC 更换、EndPlay 均清理 | 旧死 Tag 清除只重试当前活 Pawn |
| 输入绑定／held input | `Hero::ActivateInput` | `DeactivateInput` RemoveBinds、ClearAbilityInput、StopJumping；Unpossess／Action remove／EndPlay 调用 | 旧绑定与 held input 为零，新 owner 一组 |
| Enhanced Input mapping | AddInput 以 LocalPlayer／Hero 所有权添加 | 换 Hero 先 RemoveInputFeature；Hero 移除自己记录的 mapping context | 新世界一份 mapping，旧清理不删新贡献 |
| 武器 AbilitySet／装备 | `EquipmentInstance::InitializeEquipment` | 死亡、Unpossess、物品移除及 Equipment EndPlay 撤能力；授予句柄清空 | 同World旧 SourceObject specs为零、当前装备来源唯一；结束World的客户端保留静态副本按上节另查活动与注册状态 |
| QuickBar／私有库存 | Controller 的 `InitializeForPawn` | 新 Pawn 先 Unequip／清槽与 BoundPawn，再移除旧物品、生成新 GUID；同 Pawn 幂等 | 每生命两件新物品，同生命通知不补弹 |
| 生命属性监听／死亡与保护 GE | `InitializeWithAbilitySystem` | `UninitializeAbilitySystem` Remove 自己 GE、健康 delegate 与伤害上下文；StartDeath 幂等 | 死亡一次，旧效果不影响新生命 |
| Fire／Reload timer、装填 Cue／GE | 武器能力 Activate | `EndAbility` ClearTimer；Reload 移除自己的 Cue／effect 并清来源引用 | 旧 timer 不消耗新生命物品 |
| 阶段／比赛来源与 timer | Arena／Match Rules BeginPlay／Loaded | Stop 递增 generation、清 Advance／Transition／Initialization timer、解除 phase delegates；EndPlay unregister source | 旧 deadline 不推进新世界 |
| 重生 work | `GameMode::ScheduleRespawn` | Logout／规则 Stop／GM EndPlay 取消；serial、Controller／PS／Round／life 重新核验 | 离开者不重生、旧 life 不再派生 Pawn |
| 全堵跌落恢复 work | `HandlePlayerFellOutOfWorld` | 先移除 entry／ClearTimer，再只向同一当前活 life 归还自己保存的 movement；Death／Unpossess／Logout／Rules Stop／GM EndPlay 取消 | 活暂停可归还，尸体仍不可移动 |
| HUD ViewModel 本地身份／World 来源 | `Start` 的 Controller／PS／Pawn、WorldCleanup、phase／match listener | `Stop` 逐项 Remove／ClearTimer／清 snapshot；`RebindSources` 先 Unbind 旧源 | 老 VM 停止、bindings 为零，无旧 World/Pawn |
| HUD ASC／Inventory／Item／init／hit listener | `RebindSources`／`BindActiveItem` | `UnbindSources` 移除属性／Tag／库存／物品／init／动态事件 handle | 每生命监听数稳定，一条更新只刷新当前对象 |
| HUD 消息与 UIExtension point | Layout 激活与 DataWidget StartListening | `ClearGameplayUI` 先关闭菜单、Unregister points、StopListening；VM Stop；DataWidget 解除三个 message handle | 保留旧 Widget 仍监听为零，新 HUD 四元素 |
| AddWidgets 异步与 UI 贡献 | World／HUD／context generation | ResetHUD 先 ++generation／CancelLoad；移除 ownership 后拆 Widget／Extension；ResetWorld／Context 解除 policy、receiver、全局 World delegate | 迟到 load 不重新注入，旧 gate 为零 |
| AddActors 异步与训练 Actor | World generation／BeginPlay／软类 load | RemoveWorld／Context 先移除 entry，解除 BeginPlay／全局 delegate、CancelLoad、Destroy owned actors | Practice 三靶一补给，Arena 零训练 Actor |
| GameplayCue path | 按 activation context 引用计数 | context deactivate 释放，unregister 兜底，最后使用者 RemoveCuePath | 重入计数稳定、不提前删另一 World 的路径 |
| GI Travel 全局 delegate／watchdog | `Initialize`、请求时 StartWatchdog | Deinitialize 移除 Network／Travel／PreClientTravel／PostLoadMap／WorldCleanup／policy；终态 StopWatchdog | 多轮仅一个当前请求，旧失败不覆盖新状态 |
| Travel World binding | `ObserveWorld`／`BindGameState` | `ClearWorldBinding` Remove GameStateSet、++WorldGeneration；weak World／generation 筛 callback | 新 World Loaded，仅采用新身份 |
| 前端／Modal／菜单 state listener 与输入 gate | Widget 激活时订 Travel 状态／登记来源 gate | 停用／Destruct StopListening、移除按钮委托／自己的 gate；Travel RemoveModal 先释放 ownership | 前端 gate=1，前端＋错误 Modal gate=2；旧贡献归零 |
| Loading World delegate／ticker | `StartMonitoring`／EnsureStatusTicker | `StopMonitoring` Remove GameStateSet／WorldCleanup／ticker，移除自己的 gate；终态停 ticker | 旧 Loading 不挡新输入 |

Inventory／QuickBar 自身没有外部 listener／timer，不因没有 EndPlay override 就判为泄漏。普通旅行 Controller 不跨 World 保留；本项以真实 EndPlay、旧物品能力撤销、旧对象状态静止与新身份验证回收。刻意强保留对象用于观察不等于保持旧 World 活跃，也不以没有 GC 误判释放失败。

## 六模式验收矩阵

默认All的六个模式均已实际通过；最终媒体All、Game构建与必需回归仍待补入，不能据默认专项宣告任务25完成。

| 模式 | 必需实际场景 | 严格计数／成功条件 | 当前结果 |
| --- | --- | --- | --- |
| Stress | Host＋2 Client 已 Playing，再启动 ClientC；四人各十次真实环境 GE 死亡／生产重生；Fire／Reload 中死亡；一人退出再加入，最后真正终止 tracked host | 40 唯一死亡＋40 重生，每位十次；同 Round／PS／ASC；三 Client 各由真实 NetDriver failure 返回前端 | 默认All PASS：40／40、每轮3remote Dead／Live ACK、真实FireHeld／ReloadActive取消、Playing晚加入／重连、tracked host实际停止后三client真实timeout回前端并关闭提示 |
| RoundTrip | FrontEnd→Practice，再完成三次 Practice→FrontEnd→Arena(listen)→FrontEnd→Practice，最终 FrontEnd | 3 完整往返、15 loaded World／14 cleanup／7 gameplay World；旧 work／输入／UI 静止 | 默认All PASS：精确15World／14cleanup／7gameplay／3fullcycles，旧对象跨delay静止 |
| RecoveryAction | 真实引擎落界造成活 life 全堵暂停后，按实际 World context 撤销 assembled `MiniArena_AddRules` | 旧 work=0、规则来源释放、同活 Pawn 归还 movement 且仍 Tick；安全落点跨 delay 不被旧 work 修改 | 默认All PASS：实际stock Action撤销、无预取消、旧work0、活Movement归还 |
| RecoveryLogout | 两进程 Playing，远端新生命保护期内全堵暂停；该 owner 经生产 ReturnToFrontEnd 真实离开 | 真实 Logout／旧 PC与Pawn结束、work=0；其他身份保持，离开者不再重生 | 独立专项与默认All均PASS：真活life Pending1／MOVE_None／Tick1→生产Leave／Logout→新前端；旧work0跨3.5秒，双端确认 |
| RecoveryTravel | 单人 Arena Warmup 全堵暂停后经生产 Travel 返回前端，保留旧 GM／Pawn 观察 | 真实 World cleanup、work=0；新 FE／PS／ASC；跨3.5秒旧状态静止 | 默认All PASS：真实Worldcleanup／旧work0／新前端及旧状态静止 |
| RecoveryDeath | 两人 Playing，保护期造成暂停；等自然保护到期，用真实环境 GE 杀死，再正常重生 | 尸体 MOVE_None／Tick=false／旧work=0；新life正常Avatar／输入／movement | 默认All PASS：真实GE、旧work0、CorpseMovementNone1／Tick0、新life及真实owner死亡／重生ACK |

## Stress 验收细节

晚加入按 Playing 与当前 owner ACK 条件启动，不能仅按固定延时。读取当前阶段 deadline、Round、PawnData、ASC Avatar、装备与 HUD 快照，Loaded 后订阅检查立即一次 callback；重复 SetPawnData／依赖通知前后能力数和库存 GUID 不变。

保留生产 300 秒／10 分／三秒重生／两秒保护。每轮等待真实保护到期，经 `TryApplyEnvironmentDamage` 为四人各造成一次 GE 死亡；环境死亡不增加主机击杀分，避免十次死亡被提前终局打断。死亡重复入口应拒绝且统计不变。Dead checkpoint 必须在生产重生之前等齐远端真实复制 ACK，不能提前确认或修改重生时长。

每位检查初始＋十个不同 Pawn／life、同 World PS／ASC 保持；旧装备 specs／授予句柄归零，新装备来源唯一；库存两件新 GUID，初始步枪 30/90、手枪 12/36、槽0；Hero 当前16个 binding、一份 mapping，HUD／VM绑定数与活态基线一致。服务器也检查 Host，不能只检查三 Client。

指定轮通过真正输入启动持续 Fire 与 Reload，确认能力／弹药变化后再死亡，跨原 timer delay 旧 Item 不再变化；新生命重新按键及短按移动实际生效。死态 prediction absent／SavedMoves=0，Movement Tick=false，新 Movement Tick=true；扫描全部新日志的 saved-move 上限告警，不静音或提高限额。

十轮后一个远端通过生产返回前端，确认实际 Logout 与新前端身份；再连接同一主机应有新 PC／PS／ASC、干净统计和装备，其余人仍保留本局身份与统计。全部 ACK 后 runner 真正终止自己启动的 host，三个 Client 分别由实际驱动失败恢复默认前端。关闭错误后恢复一个前端 gate；错误显示期间允许前端与 Modal 各一个 gate。

## RoundTrip 与旧对象观察

```text
FrontEnd → Practice
  [Practice → FrontEnd → Arena(listen) → FrontEnd → Practice] × 3
→ FrontEnd
```

本路径精确为15个 loaded World、14次真实 cleanup，Practice四次、Arena三次；Arena单人 Warmup验证回收，Playing战斗由Stress验证。任务20同图旅行不能代替两玩法往返。

离开前让真实菜单／held input／武器 timer处于活跃状态；Practice用真实 GE击倒训练靶挂reset timer，并覆盖补给来源。有限强保留旧GM／PC／PS／Pawn／ASC／Hero／Equipment／Item／HUD／VM／Widget／Menu及Action用于检查EndPlay后的状态。

cleanup后核验旧GM pending work=0、旧Hero bindings／held=0、旧装备句柄空、ASC不再指旧Pawn、VM停止且清snapshot、旧message listener／UIExtension／Action贡献为0。Root可以复用，应区分旧来源gate释放和新前端合法gate，不能要求Root地址变或新世界总gate为0。

新世界Ready后真实等待至少3.5秒，跨旧recovery0.5秒、靶reset2秒、respawn3秒及reload delay；旧物品弹药、训练计数、HUD刷新及生命状态不再被旧工作修改。旧work=0和状态静止不等于证明从未调度过一个空返回timer delegate。每次玩法Loaded检查当前PS／ASC与保留旧身份不同、训练Actor数量正确、HUD／输入稳定，核对逐World featurelease激活与释放。

## Paused recovery 验收约束

每个入口先制造八个PlayerStart实际碰撞全堵，teleport到真实KillZ下方并让Movement Tick触发 `FellOutOfWorld`，确认同Pawn／life、活态、PendingRecovery=1、MOVE_None、Tick=true。使用真实Warmup／保护规则拒绝fall GE，不能直接调用handler或先Cancel来伪造入口。

Action只通过真实assembled Action与正确World context撤销；取消后同活Pawn可放回安全地面，以区分旧timer残留和新的真实落界事件。Logout必须真实远端旅行／退出；Travel必须真实World销毁；Death必须等保护自然结束后真实GE成立。尸体取消不能归还保存的Walking／Falling，替代Pawn不得受旧serial影响。测试fixture在取消／死亡关键观察之前保留，避免提前开放出生点改变工作路径。

## 构建、回归与证据

| 必需检查 | 实际结果 | 证据 |
| --- | --- | --- |
| 最终FPSEditor／FPS Development构建 | 输入隔离与等待日志后的两target均ExitCode=0，`-DisableAdaptiveUnity`；Editor17.35秒、Game90.04秒 | `Evidence/Builds.txt`，完整本地Build日志 |
| 六模式专项 | 默认All及最终媒体All六模式全部通过，两个runner均ExitCode=0 | `Evidence/Verification.txt`与`Verification-WithMedia.txt` |
| 任务08：依赖顺序／重复通知 | PASS，三端／晚加入／两种依赖顺序及重复通知 | `Evidence/Regression.txt` |
| 任务09：来源撤销／旧Avatar安全解绑 | PASS，撤销／恢复／重生及晚加入 | 同上 |
| 任务10：输入／菜单／Action撤销 | PASS，真实移动／Fire、三次重生与两个输入门控 | 同上 |
| 任务23：Arena／Practice／PostMatch | All PASS，四人两局20次真实GE击杀、下一局清分及独立训练 | 同上；正常联机saved-move上限扫描通过 |
| 媒体及旧对象观察 | 最终媒体20张960×540实际PNG，精选八张逐图复核；旧对象检查通过 | `Evidence/`中的精选截图 |
| PS语法／diff检查与独立提交推送 | 语法及diff检查通过；版本以Git记录为准 | 本次独立任务25提交 |

已执行独立RecoveryLogout及默认`VerifyTask25.ps1 -Mode All`，两个runner均ExitCode=0；诊断输入隔离后的最终`-Mode All -WithMedia -TimeoutSeconds 480`也ExitCode=0。端口18926，先跑Stress四进程，再RoundTrip单进程、两个单进程／两个双进程Recovery模式。默认日志为`Saved/Logs/Task25-<Mode>-<Peer>.log`，最终媒体为`Task25-WithMedia-<Mode>-<Peer>.log`。证据文件保存真实marker与原日志派生计数，不以日志marker代替runner退出码。默认通过后未修改生产死亡逻辑，最后诊断改动由完整媒体All覆盖。

任务25所需六模式、必需回归及旧对象状态检查均有本轮实际结果。下一项为任务26异步故障／网络时序／弱网；最后打包与跨机器条件仍按路线图任务28–30记录。
