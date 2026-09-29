# 任务 16：服务器权威射线步枪

## 已完成的链路

`左键 → InputTag.Ability.Fire → RifleFire GA → 当前装备的 RangedWeaponComponent → ServerFire → 双射线命中 → MiniDamageGameplayEffect → HealthSet → DeathEffect／卸装`。

步枪 `UMiniRifleEquipmentDefinition` 只授予一个 Fire GA，Spec 的 `SourceObject` 仍是对应装备实例；手枪保留任务 15 的无输入探针能力。任务 10 留存的 Pawn 级 Fire 探针只在 `-MiniProbeTask10` 激活，避免普通游戏一次输入激活两套 Fire 逻辑。步枪默认伤害 25、射程 10000 cm、最短间隔 0.12 秒；这些数值由服务端装备定义读取，客户端请求不能指定伤害或目标。

本地 GA 每次按键提交一发请求，客户端立即画一条短青色临时射线。`mini.Weapon.LocalTracer 0/1` 控制该临时反馈；`mini.Weapon.DrawTraces 0/1` 控制服务器相机／枪口长调试线。任务 18 将临时表现换成正式 GameplayCue、枪口与命中反馈。

## 服务器判定

RPC 只携带量化相机位置、单位方向和单调递增序号。服务器要求发射者仍是该 Controller 的活跃 Avatar，当前装备是步枪且来源物品匹配，步枪 Fire 能力仍由该装备授予，弹匣有弹，Fire 能力的 Tag 关系和输入状态未阻断。序号必须新于已处理请求；即使被拒绝也消费该序号，重放不能在状态变化后补发伤害。射速使用服务器世界时间，弹药只在接受射击时通过物品 `SetStat` 扣减。

相机起点必须在 Pawn 附近且落在合法第三人称相机臂附近，相机臂不能穿过 `Camera` 碰撞；方向必须与服务端控制朝向大体一致。服务器以当前世界执行 `Visibility` 相机射线，再从武器 Muzzle（缺少 Socket 时使用角色前肩位置）向准星命中点做第二条射线。只有两条射线首先命中同一存活角色才应用伤害，因此靠肩部视角看见墙后目标、但枪口被挡时无法穿墙。角色 Capsule 显式阻挡 `Visibility`；UE 默认 Pawn 碰撞预设不阻挡它。

伤害统一调用 GameMode 的 `TryApplyDamage`，沿用任务 13 的 GameplayEffect 和死亡／复活生命周期。原 `TryApplyTestDamage` 仅作为旧专项入口转发到同一逻辑。

## 验收

在工程根目录运行：

```powershell
./Scripts/BuildProject.ps1 -Target FPSEditor
./Scripts/BuildProject.ps1 -Target FPS
./Scripts/VerifyTask16.ps1
```

任务 16 三进程专项使用两名远端玩家。射手客户端通过模拟左键触发输入与 GA，服务器核对第一发将目标生命 100→75、弹匣 30→29；随后检查相同序号、反向、远离 Pawn 的起点和过快发射均被拒绝。专项分别把墙放在完整视线和只挡枪口的位置，验证有效射击消耗弹药但不伤害目标，并验证空仓拒绝。最后经同一服务器射击入口补足三发，目标死亡、撤销装备；射手和受害者两个客户端均观察到生命 0 和死亡状态。命令返回 `Task 16 PASS` 才算通过；详细进程日志位于 `Saved/Logs/Task16-*.log`。

本版使用服务器当前世界判定，不提供服务器回溯。高延迟下，客户端短射线可能先于服务器确认且与实际命中不同；视角起点／方向是合理性阈值校验，并非竞技级反作弊。当前为按键单发；任务 17 将完成弹药与装填，任务 18 将完善预测和远端表现。

任务 10–15 的原有联机专项均已回归通过，分别覆盖旧 Fire 输入探针、相机／动画、Tag 取消与拒绝、伤害／复活、私有库存复制、装备能力撤销及两端外观。最终源码的 Editor／Game 构建和任务 16 专项也已通过。
