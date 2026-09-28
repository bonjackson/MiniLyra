# 任务 11：第三人称相机与基础移动表现

练习角色现在有普通跟随和右键瞄准两种相机模式。`DA_MiniPracticePawnData` 指向原生 `UMiniCameraMode_ThirdPerson` 与 `UMiniCameraMode_Aim`；本地 `UMiniCameraComponent` 每帧读取当前 PawnData 和瞄准状态，在控制旋转、视野角与相机位置之间插值。相机从角色胶囊体中心到目标位置做球形扫掠，遇到遮挡立即收近，解除遮挡后平滑退回；恢复路径再扫一次，避免插值途中穿过新障碍。控制权变化时重置相机缓存，让重生后的新 Pawn 建立自己的视角。练习地图四个 PlayerStart 的初始俯仰角统一为 −10°。

任务 11 的右键瞄准只切换相机预览：Hero 在本地按下期间临时添加 `State.Aiming`，释放、输入停用或 ASC 重新绑定时清除。镜头还会检查该状态由当前 Pawn 持有、且能力输入没有被阻断。正式瞄准能力、状态复制和能力阻断关系属于任务 12。

`ABP_MiniPractice` 继承 `UMiniAnimInstance`，使用 Manny 原骨架与五段最小动画：持枪待机、持枪慢跑、跳跃、下落、落地。AnimInstance 从 Pawn 的速度与移动组件更新状态；这些值来自复制后的角色运动，因此模拟代理也能切换相同的动画状态。角色的 `PracticeRifle` 是纯外观骨骼网格，附着到身体骨架的 `HandGrip_R` socket（父骨骼 `hand_r`），关闭碰撞。它不参与开火或装备复制；任务 15 的装备流程可替换这件演示步枪。角色网格的相对旋转明确写为 Pitch=0、Yaw=−90、Roll=0，避免 Unreal Python 位置参数把它误写为横躺的 Pitch=−90。

资产生成与检查：

```powershell
./Scripts/BuildProject.ps1 -Target FPSEditor
./Scripts/Task11CameraAssets.ps1
./Scripts/Task11AnimationAssets.ps1
```

五段动画由 `Task11MigrateAnimations.ps1` 从本机 UE 5.8 模板一次性迁入；该脚本故意拒绝覆盖已有动画。相机与动画资产脚本可重复执行，并在全新编辑器进程中重载验证。联机数值与状态验收使用 `./Scripts/VerifyTask11.ps1`；该双进程探针在本次运行验证了四轮本地镜头归属、三次重生、右键模式切换、遮挡收近与恢复（309→79→309 cm）、远端移动与朝向（约 143 cm、74°），以及待机／移动／跳跃／落地状态。任务 10 输入生命周期、任务 07 资产重载、任务 08 三端晚加入回归通过；Editor 与 Game Development 构建通过。

可渲染 PIE 的最终截图见 [ThirdPersonPIE.png](ThirdPersonPIE.png)：角色直立且全身可见，步枪靠近右肩，先前由网格错误旋转导致的横躺姿势与明显悬空的副手已消失。截图光照偏暗，手指与护木的精细贴合仍需在后续装备／IK 任务处理。
