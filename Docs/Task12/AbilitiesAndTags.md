# 任务 12：基础能力与 Tag 关系

练习角色的能力现在由 PlayerState 持有的 ASC 执行。`DA_MiniPawnAbilitySet` 授予 Fire 测试能力、跳跃能力和瞄准能力；输入仍经任务 10 的 Hero、InputTag 和 ASC 逐帧处理。跳跃保留现有 `IA_MiniJump` 原生输入绑定，由 Hero 将按下和释放转发为 `InputTag.Jump`，因此无需重新制作输入映射资产。三个能力均以 `UMiniGameplayAbility` 为基类，默认使用本地预测；基类提供“按下触发”和“按住期间尝试激活”两种策略，本轮三个能力使用按下触发。

跳跃能力在客户端预测和服务器激活时都检查角色 `CanJump()`，按下后调用 `Jump()`，释放或取消时调用 `StopJumping()`。瞄准能力在激活期间通过 `ActivationOwnedTags` 持有 `State.Aiming`，释放或取消时结束；相机只在 ASC 的 Avatar 是当前 Pawn 且存在该状态时选择瞄准 CameraMode。Fire 目前仍是任务 09 的一次性测试能力，任务 16 会接入真正的射击。

`DA_MiniTagRelationships` 由 PawnData 引用，在服务端授予能力前和客户端收到 PawnData 时安装到 ASC。规则如下：

| 状态 Tag | 阻断激活 | 状态出现时取消 |
| --- | --- | --- |
| `State.Reloading` | Fire、Aim | Fire、Aim |
| `State.Dead` | Fire、Jump、Aim | Fire、Jump、Aim |
| `Gameplay.AbilityInputBlocked` | Fire、Jump、Aim | Fire、Jump、Aim，并清空待处理与按住的输入 |

每个能力自带 `Ability.Fire`、`Ability.Jump` 或 `Ability.Aim` 资产 Tag。ASC 在激活前读取关系表检查 Required／Blocked Tag，在状态 Tag 出现时按能力 Tag 取消已激活能力。Tag 关系放在独立数据资产中，后续死亡和装填流程只需在正确时刻赋予／移除状态；本任务没有实现这两条流程本身。ASC 的 Avatar 解绑会取消持续能力；Hero 停用输入时也会释放并清除已激活的输入能力，防止旧 Pawn 的按住状态进入新 Pawn。

复现构建和资产验证：

```powershell
./Scripts/BuildProject.ps1 -Target FPSEditor
./Scripts/Task12Assets.ps1
./Scripts/VerifyTask12.ps1
./Scripts/BuildProject.ps1 -Target FPS
```

`Task12Assets.ps1` 可重复运行，依次生成资产并在新的编辑器进程重载校验。`VerifyTask12.ps1` 启动监听服务器与客户端，模拟真实按键，检查跳跃和瞄准的释放、重复瞄准不叠加状态、Tag 导致的取消、服务端对不合法激活的拒绝，以及持有瞄准时换 Pawn 后旧能力／状态清理、新 Pawn 重新激活。服务端上远端玩家的 `LocalPredicted` 能力不能用默认 `TryActivateAbility()` 的返回值判断服务端授权：UE 5.8 会把该调用转发给客户端并立即返回成功；探针直接调用 `CanActivateAbility()` 检查服务端规则，并禁止远端转发。

本次验证结果：Editor 与 Game Development 构建通过；任务 12 资产脚本连续两次创建／新进程重载通过；双进程专项探针通过，包括 3 种状态取消、8 组服务端拒绝、输入释放和一次真实换 Pawn。任务 09／10 资产重载及任务 09／10／11 联机回归均通过。任务 09／10 的旧资产配置工具现在会保留 Pawn AbilitySet 后续新增的 Jump／Aim 能力，重复运行不会删掉它们。
