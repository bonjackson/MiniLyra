# 任务 20：完整训练 Experience 与组合边界

日期：2026-10-01。状态：训练实现、编辑器与新 Development 包运行验收通过。

## 训练场如何装配

`DA_MiniPracticeExperience` 只启用 `MiniShooterCore`，引用 `DA_MiniCombatActionSet` 与 `DA_MiniPracticeActionSet`。CombatSet 提供本地 Enhanced Input 映射；PracticeSet 通过 `MiniGameFeatureAction_AddActors` 在权威 World 的 BeginPlay 后异步生成三块训练靶和一个弹药补给区。地图保留灰盒地面、六块掩体、四个出生点与三根靶架，靶板不静态摆放。

PawnData 引用 `DA_MiniSharedCombatLoadout`，把步枪／手枪放到槽 0／1，初始选槽 0。角色 AbilitySet 只授予 Jump／Aim；Fire／Reload 由当前装备实例授予并撤销。初始弹药仍由 ItemDefinition 的 InitialStats 设置。QuickBar 不再硬编码两枪，空 Loadout 产生无武器角色；重复初始化同一 Pawn 不补回删除的物品、不重置弹药。

训练场没有竞技阶段、比分和胜负规则。HUD 的比赛栏保持“训练模式，比分／时间占位”。竞技插件是任务 21 的入口。

## 伤害、复位与补给

- 训练靶是实现 AbilitySystemInterface 的复制 Actor，自身 ASC 的 Owner／Avatar 均为自身，使用 MiniHealthSet 和普通伤害 GameplayEffect。定义资产固定 100 血、2 秒复位；复制的复合状态含 Health／MaxHealth／Enabled／DisableCount／ResetCount／Revision。
- 武器保留序列、装备实例、GUID、射速、弹药和视角验证及相机／枪口双射线；命中一般 Actor 后由 GameMode 的统一伤害入口分派。玩家分支继续使用原死亡规则；训练靶击倒类型为 PracticeTarget，旧 `bKilled` 回调严格只表示玩家死亡。
- 0 血训练靶变色并停止接受伤害，仍挡射线。继续打靶可消耗弹药和显示碰撞，但不重复伤害、不重复命中确认。服务器 Timer 在两秒后复位。
- 蓝色补给区只在服务器检查真实 Pawn overlap，拒绝区域外、死亡、ASC Avatar 不符和装填中的角色。补满已有物品的弹匣及初始备用弹药，不新增物品、不更改 GUID。有人重叠才运行每秒复查 Timer，离开后不再补弹。
- 正式地图使用 Movable Sun、SkyLight、SkyAtmosphere，并禁用预计算光照。没有靠隐藏灯光提示代替修复。

## 生命周期的所有权

AddActors 按 Experience 的 World／Context／generation 跟踪 BeginPlay 委托、异步句柄和已生成 Actor；客户端只观察复制。失败立即释放句柄／绑定并销毁已生成对象；撤销或 World cleanup 同样释放。训练靶 EndPlay 清除复位 Timer、ASC 属性委托、ActorInfo、材质和 Widget；补给区清 Timer 和 overlap 委托。

**单独停用 MiniShooterCore 会撤销其 HUD／Cue，训练靶与补给区仍由当前 Experience 的 PracticeSet 拥有。**退出 World 才撤销它们。验证没有把两种生命周期混为一谈。

旧任务 06–12 的 marker、Probe AbilitySet、输入／Tag 配置集中在 `/Game/Mini/Diagnostics`，通过独立 `DA_MiniDiagnosticsExperience` 运行。生产 Core GFD 只保留 Cue 和 HUD 两项 Action。旧诊断作者及原生桥拒绝写生产资产；任务 04／11 作者识别已装配项目后只读检查，避免重跑旧教程复原生产配置。开发扫描保持可发现诊断资产，cook 使用非递归的开发专用规则；**最终生产包还须启用 `bOnlyCookProductionAssets`，规则本身不能保证 Shipping 自动排除诊断。**

## 只改数据即可更换组合

`?Experience=DA_MiniRifleOnlyExperience` 和 `?Experience=DA_MiniUnarmedExperience` 通过普通 GameMode 的权威旅行选项选择诊断目录中的配置；客户端由 GameState 获取 ID。两种配置只引用 CombatSet，分别使用单枪与空 Loadout，均不生成训练靶或补给。角色和 GameMode 没有按地图名判断武器或训练内容。

## 资源制作与 Cook

总作者 `Scripts/Task20Assets.ps1` 按 Training → Assembly → Loadout → Config → Cook → Map 顺序运行。每组使用三个独立 Editor 进程完成 Create／CreateAgain／Verify，验证进程不修复保存内容。UE 5.8 未公开的 Blueprint 父类、merged AssetManagerSettings 和默认软引用由窄原生桥读取。

`DA_MiniPracticeCook` 精确列出 13 个媒体／材质资产与 3 个 Blueprint 类，覆盖两枪 Mesh／AnimBP、四个 Montage、六个 Sound、训练材质以及仅由 ini 引用的 UI Policy。标签关闭目录全选，扫描 `bIsEditorOnly=False` 且递归管理这些资产的保存依赖；没有 Cook 整份旧 Lyra 内容。

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor -DisableAdaptiveUnity
.\Scripts\Task20Assets.ps1
.\Scripts\VerifyTask20.ps1
.\Scripts\VerifyTask20.ps1 -WithMedia -SkipTravel
.\Scripts\VerifyTask20Configs.ps1
.\Scripts\BuildProject.ps1 -Target FPS -DisableAdaptiveUnity
.\Scripts\PackageTask20.ps1
$package = Get-Content Saved\Task20Package.json -Raw | ConvertFrom-Json
.\Scripts\VerifyTask20.ps1 -PackagedExe $package.Executable
.\Scripts\VerifyTask20.ps1 -PackagedExe $package.Executable -WithMedia -SkipTravel
.\Scripts\VerifyTask20Configs.ps1 -PackagedExe $package.Executable
```

新包使用 C 盘新目录，Pak、跳过 IoStore／Zen Store，避免复用早期任务 06 的旧包。可执行路径和日志写入 `Saved/Task20Package.json`。包的构建成功与运行通过分别记录。

## 实际验收记录

| 项目 | 当前结果 |
| --- | --- |
| Editor／Game Development 完整 unity 编译 | PASS，关闭 adaptive unity，未靠独立编译绕开合并问题 |
| 六组资产 Create／CreateAgain／独立 Verify | PASS，18 个独立 Editor 进程阶段完成 |
| Listen + 两个独立客户端，默认核心专项 | PASS |
| 实际步枪 25×4、禁用额外射击、2 秒复位、手枪 20 | PASS，PlayerKill=0、观察者确认=0 |
| 真实输入装填、序列／GUID／视角／射速／空仓拒绝 | PASS，拒绝不额外扣血；装填 11/36→12/35 |
| 实际补给 overlap、两枪补满、离开后不补、GUID 保持 | PASS |
| 带声渲染核心专项及三张截图 | PASS，已逐张查看；靶板颜色与 Widget 文本均检查 |
| 单枪／无武器配置实际运行 | PASS，库存／装备来源／HUD／输入一致，训练 Actor=0 |
| 三轮 OpenLevel＋非无缝 ServerTravel | PASS，6 次撤销、7 个新 World，每轮恰有 3 靶 1 补给 |
| 旧对象 Timer／ASC／HUD／菜单回收 | PASS，跨过复位延迟后仍无旧回调；引擎 GC 销毁同样作为正确清理 |
| 相关旧任务回归 | 任务 06（含任务 05）、07、09、10、12、15、17、19 与 19 Loading PASS；任务 06 真实连续三次 PIE 也 PASS |
| 新包生成、包中三进程／旅行／配置／截图 | PASS，真实 listen＋两客户端，6 次旅行／7 个 World，两配置与三张包截图均通过并逐张复核 |

新包实际生成于 `C:\Users\sq100\AppData\Local\Temp\MiniLyraTask20-20261001-071020\Stage\Windows\FPS\Binaries\Win64\FPS.exe`，以任务 19 提交 `8496546` 加本次未提交工作区构建。Cook 完成 618 个包，摘要为 **0 errors、1 warning**：`Unable to find package for cooking /Script/FPS. Instigator: AssetManagerModifyCook`。运行另有引擎 shader preload 提示及 `No GameplayCueNotifyPaths ... Falling back to using all of /Game/`；Core 动态 Cue 的实际命中反馈仍通过。任务 28 继续收敛生产 cook 与 Cue 路径，当前结果不等于最终 Shipping 验收。

本地日志为 `Saved/Logs/Task20-PackageConsole.log`、`Task20-Packaged-Server.log`、`Task20-Packaged-Travel.log`、`Task20-Packaged-Config-RifleOnly.log`、`Task20-Packaged-Config-Unarmed.log` 与 `Task20-Packaged-WithMedia-*.log`；编辑器证据保留为不带 Packaged 的同类日志。可随仓库查看的六张图片位于 [Evidence](Evidence)：Editor／Packaged 各有 Initial、Disabled、Refill 三张，分别核对角色／HUD、0 血橙色靶与补给后手枪 `12/36`。`Saved/Task20Package.json` 记录包位置和运行验收状态。

诊断目录迁移补查包含任务 07 的双端／晚加入／无效 Experience 零出生、任务 09 的两人授予／撤销／恢复／重生／晚加入，以及任务 06 三个真实 PIE 会话的 marker 单次注入和单次撤销；均实际重跑通过。任务 17、19 及 Loading 的最后一批日志也已核对服务器终态、拥有者快照及双端 Loaded／Failed 门控。

渲染烟测为 960×540、30 FPS 上限、GI／Reflection quality 0、Shadow quality 1、关闭 motion blur；监听端使用 NullRHI，两位客户端实际渲染。此设置用于清晰复核，不构成任务 27 的性能保证。探针先让朝向经过至少三帧再射击，截图等待 shader 编译及多个渲染帧，避免单次长帧把转向与开火合并、或把尚未提交的材质状态导出。没有放宽正式开火校验或延长训练靶复位时间。

本次尚未验证跨机器局域网、竞技计分／整局或最终 Shipping 包；这些仍按路线图任务 21–29 完成。完成本次单项提交和推送后继续任务 21。
