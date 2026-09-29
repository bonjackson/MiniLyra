# 任务 15：装备、两槽 QuickBar 与能力回收

## 实现范围与 Lyra 取舍

本任务保留 Lyra 的 `EquipmentDefinition`／`EquipmentInstance`／`EquipmentManager` 分层，并让装备通过库存物品的 `Equippable` Fragment 找到定义。原生步枪与手枪物品分别指向原生装备定义；定义提供武器 Skeletal Mesh 和仅在装备期间授予的 `UMiniAbilitySet`。当前能力集各含一个无输入绑定的 `UMiniGameplayAbility_EquipmentProbe`，用于观察授予、来源和回收；真正的射击与装填留给任务 16、17。

`UMiniEquipmentManagerComponent` 位于 `AMiniCharacter`，服务端只接受**当前控制器库存中**有效且可装备的物品。它创建以 Pawn 为 Outer 的 `UMiniEquipmentInstance`，记录来源物品 GUID，将该实例作为能力 Spec 的 `SourceObject` 授予 PlayerState ASC，并保存授予句柄。`UMiniGameplayAbility_FromEquipment` 从 Spec 取得对应实例，激活前确认它仍是当前 Pawn 的当前装备。换枪时先撤销旧句柄、取消旧装备授予的能力，再授予新装备，避免旧武器能力残留。

装备实例复制装备定义与来源 GUID，并通过 Pawn 的传统 `ReplicateSubobjects` 路径公开给观察者；完整库存和弹药仍留在拥有者的 PlayerController。拥有者可用 GUID 解析自己的来源物品，模拟代理只看到当前装备，不会获得他人的私有物品。角色现有的 `PracticeRifleMesh` 组件按当前装备定义显示步枪或手枪网格；卸装后隐藏。这里复用现有持枪组件，没有另外生成一套会与当前能力状态脱节的外观 Actor。

## 两槽与生命周期

`UMiniQuickBarComponent` 位于 `AMiniPlayerController`，固定两个槽：0 为步枪，1 为手枪。槽位复制的是库存实例 GUID，活动槽索引也随控制器只到达拥有者。ASC Avatar 与角色初始化就绪后，服务器向库存发放两件默认物品，写入槽位并装备步枪。物品初始弹药分别是步枪 30／90、手枪 12／36。`MiniDumpQuickBar` 与已有 `MiniDumpInventory` 可输出本地控制器视图。

输入映射中的 `Q` 对应 `InputTag.SwitchWeapon`。Hero 组件仅在输入激活时请求切换；QuickBar 在服务端校验活动 Pawn、存活状态、槽位和库存实例，再调用 EquipmentManager 换装。直接移除活动槽物品时，库存先通知 QuickBar 卸装并清空槽位；即使通过 EquipmentManager 直接装备了非槽位物品，移除其来源物品也会先卸装，避免能力和来源悬挂。

死亡开始时，服务器在 PlayerState ASC 仍绑定旧 Pawn 时调用 `HandlePawnLost`：先卸装并撤销装备授予的能力，再将活动槽设为无。`UnPossess`、控制器失去 Pawn 和 Pawn `EndPlay` 也有清理入口。控制器本身跨重生保留；新 Pawn 初始化时先清掉旧库存物品及槽位，再创建带新 GUID 和初始弹药的两件物品，最后重新装备步枪。旧装备实例被通知远端销毁，不能在下一条命复用。

任务 13、14 的专项探针分别依赖旧的能力数量或自行管理库存，因此默认 QuickBar 初始化对 `-MiniProbeTask13`、`-MiniProbeTask14` 隔离。这只影响这两个旧探针；普通流程和任务 15 探针仍执行默认发放、装备与重生重建。

## 验收结果

2026-09-29，Editor／Game Development 构建和三进程任务 15 专项通过。监听服务器与两个独立客户端核对了私有两槽及初始弹药、自己和远端一致的武器网格；两位客户端各模拟一次 `Q` 输入，服务器执行步枪→手枪切换并确认旧装备句柄已撤销、新能力 Spec 的 `SourceObject` 指向当前装备；服务端随后切回步枪并重复检查。两名玩家依次死亡时立即卸装、清除旧能力与外观；约 3 秒后重生得到新的物品 GUID、初始弹药和默认步枪。专项还在监听服的本地玩家身上依次删除非活动槽与当前装备，并重复发出初始化通知，确认槽位不会自行补满，旧装备能力不会残留。任务 10–14 联机回归也全部通过；修正重复初始化后又重跑了任务 10 和任务 15。

验证命令：

```powershell
./Scripts/BuildProject.ps1 -Target FPSEditor
./Scripts/BuildProject.ps1 -Target FPS
./Scripts/VerifyTask15.ps1
./Scripts/VerifyTask10.ps1
./Scripts/VerifyTask11.ps1
./Scripts/VerifyTask12.ps1
./Scripts/VerifyTask13.ps1
./Scripts/VerifyTask14.ps1
```

专项探针源码为 `MiniTask15ProbeActor` 和 `MiniTask15ProbeSubsystem`，由 `VerifyTask15.ps1` 依照双端日志核对各阶段。此阶段尚无正式射击能力或射击输入消费；探针能力只验证装备授予与回收，射线射击属于任务 16。
