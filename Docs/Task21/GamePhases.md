# 任务 21：MiniArena 与简化比赛阶段

日期：2026-10-01。状态：实现、构建、资产和运行验收通过。

竞技 Experience 通过共享 Combat ActionSet 提供角色、输入、装备与 HUD，通过 Arena ActionSet 激活 MiniArena 内容插件，并用引擎的 AddComponents Action 在所属 World 的 GameState 注入复制规则组件。训练 Experience 保持 Combat＋Practice 装配；竞技雏形暂时复用训练地图几何，用 `?Experience=DA_MiniArenaExperience` 选择模式。

## 所有权与阶段合同

GameState 持有独立的阶段 ASC，其 Owner／Avatar 都是 GameState，复制模式 Minimal。玩家能力继续由 PlayerState ASC 管理。阶段能力只在服务器执行，一次授予并激活后自动移除；任一 World 同时最多一个活动阶段。

三个原生阶段 Tag 为 `GamePhase.MiniArena.Warmup`、`GamePhase.MiniArena.Playing` 和 `GamePhase.MiniArena.PostMatch`。查询使用精确相等，无嵌套阶段树。正常计时结束由 Rules 在下一 tick 推进；取消、失败或撤销停止这一轮，不触发后续阶段。来源与 generation 防止旧回调推进新一轮；无效请求不能先破坏当前有效阶段。

Rules 只在本 World Experience Loaded 后启动，迟订阅仍可运行。生产配置为准备 10 秒、进行 60 秒、结束 5 秒，是生命周期演示配置。两人门槛、10 分／5 分钟、伤害门控、计分、胜负和出生保护属于任务 22，尚未实现。诊断配置使用独立资产的 8／20／3 秒，不覆盖生产配置。

## 复制与 HUD

复制的单个阶段快照包含 Tag、服务器开始时间、结束时间与 revision。客户端按 GameState 的服务器时钟计算剩余时间，不逐秒复制倒计时，也不能通过到期自行切换阶段。晚加入读取当前快照即可恢复进行中的阶段，客户端不需要阶段能力实例。

本地 World Subsystem 提供可注销的阶段观察者。HUD 先注册再读快照，只在有效 deadline 时运行 0.2 秒刷新 Timer；Stop 清 Timer 与观察句柄，Pawn 重生不改变阶段监听所属 World。阶段显示独立于计分，准备／进行中／结束展示真实阶段；正常结束后显示“阶段结束”，撤销竞技装配后恢复训练占位。

## 生命周期与装配

MiniArena 是项目内制作的 content-only GameFeature 插件，无需另外安装。`/MiniArena/MiniArena.MiniArena` 的全局 Action 列表为空；World Action 位于 Arena ActionSet，以 Experience 的 required World context 注入组件。服务器创建 Rules，客户端通过组件复制接收；不在客户端再注入第二份。GFD 采用唯一名称，由引擎同插件名的备用路径发现，避免与 Core 的 `GameFeatureData:GameFeatureData` Primary Asset ID 冲突；旧 Arena GFD／redirector 已移除，独立 Verify 检查此边界。

停止或退出会清规则层推进 Timer、取消该来源的能力、清能力 Timer 和 pending 记录。GameState 结束时先停止阶段，再清 ASC ActorInfo；动态 Rules EndPlay 重复停止仍安全。旧 Loaded 弱回调检查 generation，组件已移除后不会晚启动。

## 验收记录

| 项目 | 结果 |
| --- | --- |
| Editor／Game 完整 unity 构建 | PASS，`-DisableAdaptiveUnity`；Rules 的基类构造参数已修正 |
| Create／CreateAgain／独立 Verify | PASS，三个独立 Editor 进程，生产与诊断资产分别验证；训练资源 SHA256 保持 |
| 服务器阶段顺序、客户端拒绝、独立 ASC | PASS，真实 listen＋两个独立客户端，玩家 phase specs=0；客户端四个修改入口均拒绝 |
| Playing 晚加入、同一 deadline 与同步计时 | PASS，直接读 Playing，未重放 Warmup；各 checkpoint 与服务器相同 Tag／revision／deadline，实测误差小于 1 秒 |
| 提前取消、真实 Action 撤销、无旧回调 | PASS，等待超过原 deadline＋1 秒；specs／phase Timer／pending=0，客户端动态 Rules 移除 |
| 默认训练隔离、HUD Timer／句柄释放 | PASS，训练 Rules／phase／spec／倒计时=0，原两枪／HUD／输入／3 靶 1 补给正常；任务 19 的撤销、菜单、重生与迟 HUD 回归通过 |
| 图形阶段与倒计时 | PASS，两张实际客户端截图已逐张复核；显示“竞技场 进行中 时间”，分别捕获剩余 20／9 秒 |
| 普通生产 URL、插件 BP／配置加载 | PASS，无 probe／诊断覆盖，Warmup→Playing 使用生产配置的 60 秒 Playing deadline |
| 训练旅行回归 | PASS，任务 20 的三轮 OpenLevel＋ServerTravel，6 次清理／7 个 World；此项是训练回归，未据此声称竞技旅行通过 |

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor -DisableAdaptiveUnity
.\Scripts\Task21Assets.ps1
.\Scripts\VerifyTask21.ps1
.\Scripts\VerifyTask21.ps1 -WithMedia -SkipStandalone
.\Scripts\VerifyTask21Production.ps1
.\Scripts\VerifyTask19.ps1
.\Scripts\VerifyTask20Travel.ps1
.\Scripts\BuildProject.ps1 -Target FPS -DisableAdaptiveUnity
```

证据日志为 `Saved/Logs/Task21-Assets-{Create,CreateAgain,Verify}.log`、`Task21-{Server,ClientA,ClientB}.log`、`Task21-Cancel.log`、`Task21-Practice.log`、`Task21-Production.log` 和 `Task21-WithMedia-*.log`。图片为 [早期客户端](Evidence/EarlyClientPlaying.png) 与 [晚加入客户端](Evidence/LateClientPlaying.png)；两张在不同时间捕获，共同 deadline 的精确核对由客户端实际 RPC 回报完成。

首次空插件创建出现启动时 GFD 不存在，之后两插件同名 GFD 在运行中产生重复 Primary Asset ID ensure。这些尝试没有算作通过；已改用唯一名称，重新三阶段资产验证与核心／媒体／生产运行均成功，最终 Task21 日志没有此 ensure。媒体首次启动中 renderer 初始化使早期客户端错过短诊断 Warmup；脚本改为与 host 同时启动早期客户端，保留真实 8 秒配置和正式校验，重跑通过。渲染烟测沿用 960×540、30 FPS 上限与简化画质，不作为性能结论。

任务 20 的包不包含新增 MiniArena，不能用它证明本任务已打包。最终生产包按任务 28 验收；跨机器局域网按任务 25／29 验收。
