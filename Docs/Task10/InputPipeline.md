# 任务 10：Enhanced Input、InputTag 与 GAS

本任务把角色输入定义放在资产中：`UMiniPawnData` 引用 `UMiniInputConfig`，训练 Experience 的 `MiniTask10_AddInput` Action 持有 `IMC_MiniDefault`。本地角色由 `UMiniHeroComponent` 将 InputAction 绑定到 `UMiniInputComponent`；移动、视角、跳跃直接操作角色，战斗输入以 InputTag 交给 PlayerState 上的 ASC。映射和绑定随占有关系、输入接管及 Action 生命周期撤销。

## 资产与按键

资产位于 `/Game/Mini/System/Input`。`DA_MiniInputConfig` 将三个原生动作及四个能力动作分别登记；`DA_MiniPracticePawnData` 指向该配置。`DA_MiniPawnAbilitySet` 的测试能力使用 `InputTag.Ability.Fire`，训练 Experience 恰有一个名为 `MiniTask10_AddInput` 的 Action 并指向 `IMC_MiniDefault`。配置资产会校验各组内重复或无效的 Action／Tag。

| 输入 | Action 类型 | 默认按键 | 处理路径 |
| --- | --- | --- | --- |
| 移动 | Axis2D | W／A／S／D | 原生；依据控制器 Yaw 方向移动 |
| 视角 | Axis2D | 鼠标移动 | 原生；控制器 Yaw／Pitch |
| 跳跃 | Boolean | 空格 | 原生；按下跳跃、释放停止 |
| 开火 | Boolean | 鼠标左键 | `InputTag.Ability.Fire` → ASC |
| 装填 | Boolean | R | `InputTag.Ability.Reload` → ASC |
| 切枪 | Boolean | Q | `InputTag.Ability.SwitchWeapon` → ASC |
| 瞄准 | Boolean | 鼠标右键 | `InputTag.Ability.Aim` → ASC |

W／S 映射用 Swizzle 将按键值送到前后轴，A／S 使用 Negate，D 保持正向。`IMC_MiniDefault` 启用 `CountRegistrations`，便于发现重生后重复添加映射。装填、切枪和瞄准目前只完成输入路由；相应装备、武器与瞄准能力分别由后续任务实现。当前 Fire 对应 `UMiniPawnProbeAbility`，用于验证按下激活和释放结束，尚无弹药、命中或伤害逻辑。

## 输入流与状态门控

项目将默认 PlayerInput 指向 `UMiniPlayerInput`（继承 `UEnhancedPlayerInput`，额外提供只读映射注册数诊断），InputComponent 指向 `UMiniInputComponent`。后者为原生动作和能力动作记录 Enhanced Input 绑定句柄，移除时逐个解绑。能力动作以 `Started` 发送按下，以 `Completed`／`Canceled` 发送释放，避免按住时每帧重复触发按下。ASC 从 AbilitySet 授予的能力规格上查找精确匹配的 InputTag，分别记录按下、保持和释放；本地 `AMiniPlayerController::PostProcessInput` 在输入处理后执行 `ProcessAbilityInput`。输入被阻断或角色／Avatar 不匹配时，ASC 清空待处理输入，并仅取消由本输入路径激活且仍活跃的能力；同一 Tag 指向的事件激活能力不会因此被误取消。

`PawnExtension` 到达 `GameplayReady` 需要 PlayerState ASC 已把当前角色设为 Avatar。`Hero` 还要求 `PawnExtension` 已就绪；对本地角色，必须同时完成输入绑定并确认本地玩家持有映射。模拟代理不等待 LocalPlayer、InputComponent 或映射，也不为自己创建输入绑定。初始化状态只表示该角色曾完成门控，功能停用后不会把状态倒退；输入当下是否可用应查询 Hero 的活跃绑定状态。

`MiniTask10_AddInput` 按 Experience 的 World context 注册角色扩展处理器，仅对有 LocalPlayer 的当前角色安装映射。重复通知对同一角色不重绑；换角色时先解绑旧 Hero，再把映射转给新 Hero，旧角色随后清理也不会移除新角色的映射。角色失去占有、结束、Action 停用时移除句柄和映射，清空 ASC 输入，并调用 `StopJumping` 清除持续跳跃。客户端旧角色的 `OnRep_Controller` 在失去本地控制时主动解绑，不必等到角色销毁。`SetMiniInputBlocked` 是供未来菜单／UI 接管使用的 Controller 门控接口：接管时撤销输入，恢复时重新通知 Action 绑定；即使接管期间换了 Pawn，解除阻断后也会给当前新角色重新绑定。目前没有菜单界面。Action 的非 Shipping 测试暂停入口调用同一撤销与重绑路径；任务 06 的三次真实 World 停用另行验证 `OnGameFeatureDeactivating` 实际撤销输入。

## 验收方法与边界

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\Task10CreateAssets.ps1
.\Scripts\VerifyTask10Assets.ps1
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\VerifyTask10.ps1 -TimeoutSeconds 300 -Port 18794
```

资产创建脚本可重复运行；验证脚本已在新的编辑器进程重新加载保存后的七个 InputAction、映射、InputConfig、PawnData、AbilitySet 和 Experience 引用。`VerifyTask10.ps1` 已用两个独立的未 Cook 编辑器游戏进程与 NullRHI 验收：客户端通过 `InputKey` 模拟实际映射的键鼠事件，而非直接注入 InputAction。首轮 W 输入让角色移动超过 10 cm；四个角色周期均记录一次 `FIRE_HELD` 与 `FIRE_ENDED`，每轮映射注册数为 1、绑定句柄数为 16，并有一名绑定数为 0 的模拟代理。

服务端每次解除旧角色占有后保留它 1.25 秒，再生成新角色并销毁旧角色。客户端在三轮替换中均先记录 `UNPOSSESSED_CLEAN`，确认旧角色失去本地控制时映射与绑定已归零，而不是靠销毁或新角色接管顺带清理。第三次重生发生在菜单输入阻断期间，新角色先记录 `BLOCKED_RESPAWN`：映射与绑定为 0，尚未进入 `GameplayReady`；解除阻断后才进入第 4 轮 `CYCLE_READY`。末轮还在阻断前按住 Jump，撤销后确认 `bPressedJump` 清零并记录 `JUMP_CLEARED`。菜单和 Action 测试暂停期间持续 Fire 结束，受阻按键不激活能力，恢复后再次开火成功。脚本核对前三轮各一次、第四轮五次 Fire Tag 按下；服务端按四轮角色分别核查 1／1／1／5 次 `FIRE_ACTIVE` 与 `FIRE_ENDED`，合计各 8 次，避免只凭客户端预测成功就判定能力同步正确。

| 检查 | 状态 |
| --- | --- |
| FPSEditor／FPS Game target 构建 | 通过；`Saved/Logs/Build-*-Win64-Development.log` |
| 修改后资产重建与新进程重载 | 通过；`Saved/Logs/Task10-{CreateAssets,VerifyAssets}.log` |
| 两进程输入、三次重生、旧角色解绑、阻断中重生、跳跃／开火清理及菜单／Action 门控 | 通过；`Saved/Logs/Task10-{Server,ClientA}.log`，客户端最终 `PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1`；服务端 Fire 激活／结束各 8 次 |
| 任务 08／09／07／06 回归 | 通过；任务 08 三端第三人晚加入后均 `GameplayReady=3`，任务 06 三次真实 World 停用均记录输入解绑与 Action 停用。日志位于 `Saved/Logs/Task{06,07,08,09}-*.log` |

专项探针验证的是输入映射、能力输入与生命周期，不验证图形画面、实际菜单、枪械、打包后联机或 2–4 人完整竞技局。三端回归确认 `GameplayReady` 已到达，但第三人称相机与动画仍属于任务 11。Enhanced Input 是项目已启用的 UE 自带插件，本任务无需另行安装插件。
