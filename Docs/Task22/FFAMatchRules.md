# 任务 22：FFA 计分、结算与出生规则

日期：2026-10-01。生产实现、Editor／Game 构建及联机验收完成。竞技地图暂用训练场几何；任务 23 制作正式灰盒地图，任务 24 增加前端与四人容量限制。

## 规则与装配

生产 `DA_MiniArenaExperience` 继续使用共享 CombatSet／PracticePawnData，没有 PracticeSet，因此不生成训练靶或补给区。ArenaSet 经同一个 stock `MiniArena_AddRules` Action，向本 World 的 GameState 注入两个复制组件：阶段 Rules 和 MatchRules。两者分别承担阶段计时和 FFA 策略，GameState 的 ASC 仍只保存阶段能力，玩家能力仍属于 PlayerState ASC。

| 配置 | 生产 | 独立诊断 |
| --- | --- | --- |
| Warmup／Playing／PostMatch | 无期限／300 秒／5 秒 | 无期限／20 秒／3 秒 |
| 至少参与人数 | 2 | 2 |
| 获胜击杀数 | 10 | 10 |
| 出生保护／死亡重生 | 2 秒／3 秒 | 2 秒／3 秒 |

人数来自参与的 PlayerController／PlayerState，死亡和临时缺少 Pawn 不减少人数。Loaded 迟订阅后扫描当前 roster，Logout 在 Controller 销毁前显式排除退出者。单人无限期等待，两人后下一 tick 再检查并开始新局；Playing 中人数降至不足两人，以 `InsufficientPlayers` 结束，无胜者，PostMatch 后回到等待。仍有人数门槛满足时自动开启新局，不进行地图旅行。

比赛组件复制 RoundId、revision、参与人数、比分行、结算原因、冻结结果行和胜者 ID。每局开始才重置 PlayerState Kills／Deaths，晚加入该局从零分进入。达分或超时结束，超时同分列为并列胜者；结果冻结后不再增加比分。PostMatch 晚加入直接获得结果快照，不依靠历史消息重建。

## 伤害与唯一计分入口

`MiniHealthSet::PreGameplayEffectExecute` 在玩家 `IncomingDamage` 的共同 GE 执行位置检查 FFA 状态。Warmup、PostMatch、终局、已过服务器 deadline、无效配置或出生保护期间，玩家伤害不损血。没有 MatchRules 或配置为 null 的训练／纯阶段世界保持原行为；练习靶不进入玩家 FFA 检查。

HealthSet 将 IncomingDamage 转换为 Health 时，在本次同步 SetHealth 周围复制 GE 归因，再立即释放。Health 的变化回调不能依赖仍有原始 `GEModData`。Instigator 使用来源 PlayerState，EffectCauser 使用原始 Pawn；环境 GE 明确清空默认 instigator，避免把受害者误判为自杀来源。上一发非致命伤害不会污染下一次环境死亡。

死亡 GE 成功且本生命首次进入死亡后，HealthComponent 向服务器 MatchRules 提交 RoundId、VictimLifeId、Victim／Instigator PlayerState 和原始 Pawn。规则按当前局及受害者生命去重：玩家击杀增加 killer Kills 和 victim Deaths，自杀／环境只增加 Deaths。武器命中 RPC、GameplayCue 和 HUD 消息均不计分。PlayerState 修改 API 拒绝客户端调用；没有客户端加分 RPC。

## 出生与释放

新 Pawn 实际成为玩家 ASC Avatar 时递增独立 life ID，重复初始化不递增。两秒保护是本生命持有 handle 的有限时 GE，重复初始化不续期，旧 Pawn 只移除自己的效果。新局释放所有旧 Pawn 后重新出生，恢复血量并清理前一生命的装备及死亡效果。

竞技出生选择使用现有 PlayerStart，先检查胶囊阻挡，再优先选择离其他活玩家最远的可用点；相同距离按稳定路径和轮换顺序选择。禁用 Controller 的旧 StartSpot 缓存。全部受阻时保留可取消的重试，不回退到世界原点或强行重叠生成。

GameMode 保存每个 Controller 的真实 Timer handle。重生和 Avatar 重试核验弱 Controller／PlayerState／Match 来源、RoundId、Rules generation、life ID 和工作 serial。Logout、终局、新局和 EndPlay 清除计时器。唯一留下的死亡玩家在回到 Warmup 后恢复生命；人数重新满足时，新局重置会取消等待阶段的旧重生工作。

MatchRules 在已 Loaded 的 World 中早于配对阶段组件创建时，只允许一次下一 tick 初始化检查；配对缺失或无效后停止，不永久轮询。配置是否存在与 HUD 上下文是否有效分别判断：停止规则撤销 MatchSubsystem 来源，并复制上下文关闭；configured 状态仍保持伤害拒绝。GameState EndPlay 先停止比赛，再停止阶段与 ASC。

HUD ViewModel 独立监听本 World 的 PhaseSubsystem／MatchSubsystem，Pawn 重绑定不重装 World 监听。使用服务器时钟刷新倒计时；显示等待人数、各玩家击杀／死亡、胜者或并列结果。冻结结果使用 ResultRows，Warmup 优先显示等待。Stop／World 清理移除委托和倒计时，释放快照中的对象引用。

## 资产及历史作者兼容

生产新增：

- `/MiniArena/Config/DA_MiniFFAPhaseConfig`、`DA_MiniFFAMatchRules`
- `/MiniArena/Components/B_MiniFFAArenaRulesComponent`、`B_MiniFFAMatchRulesComponent`
- 原生产 `DA_MiniArenaActionSet` 改为两个服务器组件条目，Experience 保持共享 CombatSet。

诊断新增：`/Game/Mini/Diagnostics/Arena/` 中对应 FFA Config／Blueprint、`DA_MiniFFADiagnosticsActionSet` 和 `DA_MiniFFADiagnosticsExperience`。旧任务 21 的 10／60／5 Config／BP 和 8／20／3 四件诊断资产保留。MiniArena 的 GFD 仍为唯一 `/MiniArena/MiniArena.MiniArena`。

`Task22Assets.ps1` 依次运行 Create、CreateAgain、独立 Verify，每步独立 Editor 进程。原生桥限制固定路径，验证不编译、修复或保存。作者用哈希保护训练资产、默认配置、GFD 和任务 21 纯阶段资产。旧 Task21 作者发现 FFA 后只读校验生产分支，不会降回单组件；旧生产运行脚本转入两人 FFA 烟测。

## 实际验证

本机 UE 5.8.2，Editor `-game` 独立进程。所有专项均检查最终 PASS，使用真实 GE／计时器／连接；不直接写 Health 或分数制造结果。短时诊断不修改生产配置。

| 验证 | 结果 |
| --- | --- |
| 最新 FPSEditor／FPS Game Development 构建 | 两个目标 `-DisableAdaptiveUnity` 最终构建均通过 |
| Task22 Create／CreateAgain／独立 Verify | 三个独立 Editor 进程通过，均 0 errors／0 warnings；训练和旧阶段资产哈希不变 |
| 无 probe 生产 URL，单人等待、两人 300 秒开局 | 通过；修正 UE 登录时先于 PostLogin 选择出生点的问题；普通客户端 Loaded 与 Avatar 绑定均成立 |
| 生产真实 10 击杀、自杀／环境、去重、2 秒保护／3 秒重生、PostMatch 晚加入 | 通过；单人等待超过 10 秒后加入，12 次真实死亡，10 次玩家击杀；11 次实际重生后保护测试，最后死亡不再重生；两客户端复制比分／PlayerState／HUD／deadline 一致 |
| 20 秒超时并列／有领先者、Playing 晚加入 | TimeDraw／TimeWin 通过；三人零分并列，晚加入 Playing 不改变 deadline；领先者通过真实 GE 获得一分后超时获胜 |
| 真实断开、唯一死亡玩家等待恢复、再加入新局 | Logout 通过；两人均死亡后远端实际 disconnect，剩余死亡主机在等待中恢复；再加入后 RoundId 增加，旧事件拒绝且分数归零 |
| 真实 Action 撤销，客户端组件／HUD 回收、旧 deadline 与重生清理 | Revoke 通过；实际 stock Action 撤销后双端来源／组件消失，阶段 spec／保护／待重生归零，跨旧 deadline＋1 秒没有旧回调 |
| 训练及任务 21 纯阶段回归／HUD／枪械回归 | 缺少组件的训练和实际注入 null 配置组件的旧阶段 Legacy 均通过；Task21 Core／Cancel／Practice、Task19 三进程、Task17 枪械三进程回归通过；旧 Task21 资产三阶段仍通过 |
| 渲染截图检查 | 生产 Score 媒体专项重跑通过，三张 960×540 图逐张复核；Playing、原客户端结算与无 Pawn 的结果晚加入均显示正确比分 |

媒体专项还直接调用客户端 PlayerState 的 Kill／Death／Reset API、MatchRules 修改入口及 Phase 跳转，全部拒绝且统计未变化。晚加入 PostMatch 暂不生成 Pawn，HUD 保持冻结结果，下一局才出生。

截图：[进行中](Evidence/ClientPlaying.png)、[原客户端结算](Evidence/ClientScoreResult.png)、[结果晚加入](Evidence/LateClientScoreResult.png)。后一张默认摄像机位置沿用当前地图，最终地图／进入退出表现留后续任务处理。

本次纠正了构建中的属性重名、作者包装脚本的旧标记及 PowerShell 单场景数组解包问题；修正后重新编译／重跑对应专项。没有以失败尝试作为通过证据。

本次不新建分发包。任务 20 的包不含本次 FFA，最终 Cook／打包及包内整局验收分别属于任务 28／29。当前单机多进程证据不能替代跨机器局域网验证。

## 重跑入口

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor -DisableAdaptiveUnity
.\Scripts\Task22Assets.ps1
.\Scripts\Task21Assets.ps1
.\Scripts\VerifyTask22Production.ps1
.\Scripts\VerifyTask22.ps1
.\Scripts\VerifyTask22.ps1 -Mode Score -WithMedia
.\Scripts\BuildProject.ps1 -Target FPS -DisableAdaptiveUnity
```

每次通过后核验日志中的最终 PASS，而非只看进程启动。日志位于 `Saved/Logs/Task22-*`，截图位于 `Saved/Screenshots/Task22-*`。
