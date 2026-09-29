# 任务 13：血量、伤害、死亡与重生

## 生命与伤害归属

`AMiniPlayerState` 持有可跨 Pawn 重生复用的 ASC 和 `UMiniHealthSet`。`Health`、`MaxHealth` 由 GAS 复制，初值均为 100；`IncomingDamage` 是不复制的临时结算属性。服务器用瞬时 `UMiniDamageGameplayEffect` 的 `Data.Damage` SetByCaller 数值修改 `IncomingDamage`，HealthSet 在效果执行后扣除血量，将结果限制在 0 至 `MaxHealth`，并清零临时属性。武器接入时可以复用这条 GameplayEffect 结算链，命中与攻击资格仍需由服务器验证。

`AMiniGameMode::TryApplyTestDamage` 是本任务的服务器测试入口。它拒绝无效或非正的伤害、无效目标、自身伤害、已死亡的攻击者／目标，以及 ASC Avatar 与当前 Pawn 不一致的请求。效果由攻击者的 ASC 创建并应用到目标 ASC；客户端不能直接改写权威血量。`AMiniTask13ProbeActor` 的服务器 RPC 仅供 `-MiniProbeTask13` 诊断使用，不是正式武器伤害接口。

## 死亡与重生

每个 `AMiniCharacter` 的 `UMiniHealthComponent` 监听当前 ASC 的血量变化。服务端血量首次归零时，它施加无限时长的 `UMiniDeathGameplayEffect`，向 ASC 授予 `State.Dead`，并把一次性的 `bDeathStarted` 复制给客户端。Tag 接入任务 12 的能力取消与激活阻断；死亡状态使旧 Pawn 停止移动、碰撞和本地输入。重复伤害不会重复启动死亡。

正常流程由 GameMode 在约 3 秒后重生：先移除**该旧 Pawn 自己施加**的死亡效果，解绑旧 Pawn，恢复血量并清零临时伤害，再通过已有的 `RestartPlayer` 生成并绑定新 Pawn。ASC 和基础能力仍归 PlayerState，不因每次重生重新授予；其他来源的常驻效果不会被批量移除。确认新 Pawn 已成为 ASC Avatar 后才销毁旧 Pawn；出生失败会重试，依赖初始化尚未完成时会延迟检查。旧 Pawn 的迟到清理只解除自己持有的绑定，不能清掉新 Avatar。

若服务端流程提前 `UnPossess` 或销毁死去的 Pawn，HealthComponent 的解绑也会移除该 Pawn 的死亡效果，避免 `State.Dead` 留在持久 ASC 上。服务器可随后直接调用 `RestartPlayer`；旧 Pawn 后续的清理回调不得影响新 Pawn。这一路径是异常生命周期防护，不受正常死亡的 3 秒等待约束。装备及装备能力的死亡回收由任务 15 接入。

## 验收方式

```powershell
./Scripts/BuildProject.ps1 -Target FPSEditor
./Scripts/BuildProject.ps1 -Target FPS
./Scripts/VerifyTask13.ps1
```

`VerifyTask13.ps1` 启动监听服务器与独立客户端，在训练地图运行三轮诊断：

1. 首轮验证自身／空目标伤害被拒绝，服务器伤害使客户端血量按 100→75→0 复制，`State.Dead` 与死亡状态到达客户端；服务端拒绝死者再次受伤。
2. 前两轮均等待约 3 秒后生成不同的新 Pawn，客户端恢复 100 血、默认／瞄准镜头与输入；PlayerState ASC 与基础能力数量保持不变。服务端还检查旧 Pawn 的迟到 ASC 清理不破坏新 Avatar。
3. 第三轮在死亡后强制 `UnPossess` 并立即重生，验证死亡效果已撤销、旧 Pawn 的迟到清理安全、客户端新 Pawn 的输入和镜头再次恢复。这轮不要求等待正常的 3 秒计时。

脚本以服务端三次 `DEATH_STARTED`、两次定时重生、一次强制清理、原死亡计时器触发后的第三次 Avatar 确认，以及客户端最终 `PASS` 日志为通过条件。修改源码后须重新构建并运行脚本；任务 10–12 的联机回归用于确认输入、相机和能力生命周期未受影响。

2026-09-29 验收：Editor／Game Development 构建、任务 13 双进程专项探针、任务 10／11／12 联机回归均通过。专项日志记录两次正常重生延迟均为 3.02 秒；第三轮强制替换后，旧 Pawn 被销毁，原死亡计时器随后确认新 Pawn 仍为 ASC Avatar。三轮的 ASC 和 4 项基础能力不变，客户端血量、死亡状态、输入与镜头均按预期恢复。
