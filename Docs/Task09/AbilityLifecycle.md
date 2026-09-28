# 任务 09：PlayerState ASC 与 AbilitySet 生命周期

本任务把 GAS 的 Owner 放在 `AMiniPlayerState`，角色只作为可替换的 Avatar。PlayerState 持有复制的 `UMiniAbilitySystemComponent` 和基础 `UMiniHealthSet`；PawnExtension 在 PawnData、PlayerState 与占有关系完成任务 08 的初始化条件后调用 `InitAbilityActorInfo(PlayerState, Pawn)`。角色被解除占有或结束时，只有在 ASC 的当前 Avatar 仍是这个角色的情况下才取消能力并清空 Avatar。重新占有旧角色时会重新放开绑定资格，重生新角色则继续使用原 PlayerState 的 ASC。

## 数据与来源

`UMiniAbilitySet` 支持能力、持续效果、AttributeSet 三类条目。服务器授予时把每个能力句柄、效果句柄和动态 AttributeSet 收集到一份 `FMiniAbilitySetGrantedHandles`；撤销时只按这些句柄清理，避免影响其他来源。能力条目预留 InputTag，任务 10 接上输入。ASC 使用 Mixed 效果复制模式，基础生命值与测试属性复制给相关客户端。

| 来源 | 资产 | 当前测试内容 | 生命周期 |
| --- | --- | --- | --- |
| PawnData | `/Game/Mini/System/AbilitySets/DA_MiniPawnAbilitySet` | 一项 `MiniPawnProbeAbility` | PlayerState 第一次接受 PawnData 时服务器授予一次；同一 PawnData 重复设置不会重授。PlayerState 结束时撤销。 |
| 训练 Experience 的 `MiniTask09_AddAbilities` Action | `/Game/Mini/System/AbilitySets/DA_MiniFeatureAbilitySet` | 一项 `MiniFeatureProbeAbility`、一项无限时长 `MiniProbeEffect`、一项复制的 `MiniProbeAttributeSet` | Action 以 Experience 的 World context 注册 PlayerState 扩展处理器；对已有和后来出现的 PlayerState 去重授予，停用或移除接收者时仅撤销自己的句柄。 |

功能 Action 放在训练 Experience 的 `Actions`，保留任务 06 的 AddComponents Action。它不放进 `MiniShooterCore` 的 GameFeatureData；后者随插件在进程级激活，不能为多个并存 World 提供相同的作用域保证。`AModularPlayerState` 在自己的 `BeginPlay` 完成前就发送 `GameActorReady`，而授予要求 PlayerState 已进入 BeginPlay，因此 MiniPlayerState 在 `Super::BeginPlay` 后补发 `MiniAbilityActorReady`。这个事件使晚加入者也能得到功能授予；已有 PlayerState 由扩展处理器的当前接收者回调覆盖。

## 资产生成与验证

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\Task09CreateAssets.ps1
.\Scripts\VerifyTask09Assets.ps1
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\VerifyTask09.ps1 -TimeoutSeconds 300 -Port 18793
```

资产脚本可重复运行，创建或修复两份 AbilitySet、PawnData 引用和具名 Action；编辑器桥校验条目、唯一 Action 与引用。`VerifyTask09Assets.ps1` 在新编辑器进程重新加载已保存的资产，已通过。项目在任务 03 已启用 UE 自带 GameplayAbilities；本任务只增加 `GameplayAbilities`／`GameplayTasks` 模块依赖，无需安装插件。

## 联机与撤销结果

`VerifyTask09.ps1` 使用未 Cook、NullRHI 的三个独立编辑器游戏进程。先让监听服务器与 ClientA 组成两人场，再让 ClientB 晚加入。服务端在两人就绪后通过非 Shipping 测试入口暂停 Action 授予：两份 PawnData 能力仍在，两份功能能力、效果和属性撤至 0；恢复后各回到 2。随后服务端解除远端角色占有、调用正常 `RestartPlayer`，确认 PlayerState 与 ASC 指针不变、Avatar 指向新角色，并再让旧角色执行解绑与销毁；旧角色未清空新 Avatar。

ClientA 在暂停期间也观察到自己的 PawnData 能力仍为 1，而功能能力、完整效果和两名玩家的测试属性均降至 0；恢复后回到拥有者两项能力、一个效果、两份测试属性。第三人晚加入后的稳定快照：服务端有 3 个 PlayerState、3 份 PawnData 能力、3 份功能能力、3 个效果、3 份测试属性与 3 个已绑定 Avatar；ClientA 和 ClientB 各自只收到**本地拥有者**的两份能力及一个完整效果，但都能看到 3 份测试属性、3 份基础生命属性和 3 个正确 Avatar。这与 ASC 的 Mixed 复制模式一致：模拟代理不以完整能力规格或效果实例作为可见性要求。探针也检查数量恰好相等，防止重复授予。

`SetProbeSuspended` 只用于可控地调用与 Action 停用相同的逐来源撤销／再授予路径，不模拟网络乱序或完整插件卸载。另用 `VerifyTask06.ps1` 的三个连续 World 周期检查真实 `OnGameFeatureDeactivating` 路径：每一轮日志各有一次 `GRANTED`、`REVOKED`、`ACTION_ACTIVE` 和 `ACTION_INACTIVE`。任务 08 的初始化／晚加入探针与任务 07 的正常及无效 Experience 出生探针也已回归通过。两个构建目标均为 Win64 Development；本任务尚未做打包后联机或图形画面验收。

`GameplayReady` 仍保持关闭：ASC 已绑定，但 Hero 的本地输入、InputTag 路由与解绑要在任务 10 完成。
