# 任务 14：物品定义、实例与私有库存复制

## 本任务的范围与 Lyra 取舍

库存负责回答玩家**拥有哪些物品，以及每个物品当前有哪些运行时数值**。定义是只读配置；服务器为加入库存的物品创建独立实例，并负责增加、移除与数值变更。任务 15 的装备、两槽 QuickBar、持枪外观和能力授予读取这些实例，不在本任务提前实现。

保留 Lyra 的 `ItemDefinition`／`ItemInstance`／`InventoryManager` 分层，以及定义上的轻量 Fragment。`UMiniRifleItemDefinition` 和 `UMiniPistolItemDefinition` 是两个原生定义，不另建蓝图或 DataAsset。各自的 `UMiniInventoryFragment_InitialStats` 设置弹匣／备用弹药：步枪 30／90，手枪 12／36。`UMiniInventoryFragment_Equippable` 和 `UMiniInventoryFragment_Icon` 已作为可扩展 Fragment 建立，但装备定义和图标暂未配置；本任务不新增图标素材，也不把现有武器 UV 贴图当作 UI 图标。物品实例的运行时数值属于实例，不能改写定义的 CDO 或影响另一件物品。

`UMiniInventoryManagerComponent` 位于 `AMiniPlayerController`，因此可跨 Pawn 死亡和重生保持归属。`MiniDumpInventory` 控制台命令输出当前控制器的本地调试快照。普通游戏流程暂不自动发放武器；`-MiniProbeTask14` 专项诊断才向两个玩家预载步枪与手枪。任务 14 不擅自重置玩家库存；任务 15 在默认装备与 QuickBar 接入时明确规定死亡卸装、重生清空旧槽位并重建初始物品的顺序。首版不做背包格子、拖拽、拾取物或完整 UI。

## 复制路径

当前项目使用 UE 5.8 的 Generic 复制模型与传统子对象复制路径。`FMiniInventoryList` 使用 `FFastArraySerializer`，由服务器标记新增及删除；列表项引用可网络复制的 `UMiniInventoryItemInstance`。实例把定义类、唯一 `FGuid` 和运行时数值作为属性复制，经组件的传统 `ReplicateSubobjects` 路径发送。该组件和所属 `PlayerController` 未启用 registered subobject list，也没有把 Iris 的注册片段或注册列表调用混入同一实现。将来切换复制模型时必须重新验证这条子对象路径。

`PlayerController` 只存在于服务器和其拥有者客户端，因此完整库存只送给拥有者。其他玩家可看到后续装备系统公开的当前武器外观，但不需要拿到对方的私有库存或弹药明细。服务端增加、移除接口检查权限、定义及实例有效性；客户端展示来自复制的列表与实例，不能自行创建权威库存项。

物品被移除时，服务器先从 FastArray 删除对应项并标记数组变化，再调用 `DestroyReplicatedSubObjectOnRemotePeers` 通知客户端销毁旧子对象。拥有者收到删除后，调试视图按当前列表重建，不能保留旧实例引用。列表项与子对象的到达顺序可能不同，读取时只把已解析、有效的实例计入完整快照。

## 验收

`VerifyTask14.ps1` 启动监听服务器与两个独立客户端，在训练图运行相同的完整流程。服务器预载两件物品，两个拥有者分别收到 2 件私有初始快照、不同的步枪实例 GUID 和正确数值。服务器将步枪弹匣从 30 改为 29 后，两个拥有者均收到数值变化，物品 GUID 保持稳定。随后服务器删除步枪：各客户端只剩原手枪，旧步枪从列表消失且其弱引用失效。重新加入步枪时得到不同于旧实例的新 GUID，初始数值恢复。

专项探针还检查客户端直接调用 `AddItem`、`RemoveItem`、`SetStat` 均被拒绝；每个客户端只看到自己的 `PlayerController` 和归属自己的诊断 Actor，不能读取对方的库存。服务端与客户端日志按阶段匹配实例 GUID、数量及数值。2026-09-29 已通过 Editor／Game Development 构建、三进程 `VerifyTask14.ps1` 专项验收及任务 10–13 联机回归。任务 11 的无渲染远端动画探针延长服务器移动观察窗口后连续两次通过；这项调整仅作用于测试探针。

验证命令：

```powershell
./Scripts/BuildProject.ps1 -Target FPSEditor
./Scripts/BuildProject.ps1 -Target FPS
./Scripts/VerifyTask14.ps1
./Scripts/VerifyTask10.ps1
./Scripts/VerifyTask11.ps1
./Scripts/VerifyTask12.ps1
./Scripts/VerifyTask13.ps1
```

任务 10–13 回归用于检查 PlayerController 新增库存组件后，输入、动画镜头、能力、死亡与重生仍正常。任务 15 再验证装备与库存实例的关联，以及死亡／重生时的完整清理和恢复。
