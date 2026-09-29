# Mini Lyra 项目工作大纲：30 次任务

> 更新日期：2026-09-27
>
> 实施项目：`F:\FPS\FPS`
>
> 本机参考：`F:\LyraStarterGame`
>
> 用户已确认：第三人称小型竞技场，支持 2–4 人联机。
>
> 本文最初根据源码、配置与资产调查编写；执行进度和实际验证结果见下方更新及 `MiniLyraProgress.md`。

> **任务 01–05 执行更新（2026-09-27）：** 用户明确改为从空项目开始。旧源码、配置、Content 与生成目录已移至 `Backups/Task01_PreReset_20260927`，当前活动工程从最小 C++ 模块继续；任务 01 的 Editor 构建与基础地图加载、任务 02 的 Mini 素材和灰盒图命令行加载、任务 03 的插件／Tag／扩展事件探针、任务 04 的真实 Experience 资产扫描与加载探针均已通过。任务 05 的 Editor／Game 构建、监听服务器／客户端的正常及无效 ID 探针均已通过。下方第二节保留为规划时的历史调查，不能视为当前实现。实际进度以 `Docs/MiniLyraProgress.md` 为准。

## 一、项目定位与最终成果

建议在现有 `FPS` 工程中逐步实现一套 `Mini*` 框架，用一个能完整玩完的射击游戏验证 Lyra 的核心设计。保留“数据决定玩法、插件装配功能、组件协作初始化、能力驱动行为、服务器决定结果”的主干，缩减地图、内容数量和平台服务。

不建议把整个 Lyra 项目复制进来再逐个删除，也不建议只模仿 Lyra 的枪械与角色表现。前者依赖过多，后者无法达到保留核心框架的目标。推荐路线是：**复用少量基础插件，按系统移植和精简原生代码，重建 Mini 玩法资产，按需复用美术资源。**

### 1.1 首版产品范围

| 项目 | 首版决定 | 这样取舍的原因 |
| --- | --- | --- |
| 平台 | Windows PC，键鼠 | 控制输入与打包范围 |
| 视角 | 第三人称，普通与瞄准两种 CameraMode | 贴近 Lyra，并验证相机模式与 PawnData 的组合 |
| 联机 | 2–4 人，Listen Server，局域网或直接 IP 加入 | 能验证服务器权威与复制；暂不依赖账号、匹配平台 |
| 核心玩法 | 自由混战 FFA，击杀计分、死亡重生、结算再开局 | 比团队分配与占点规则更容易收敛 |
| 对局默认值 | 5 分钟或先得 10 分；死亡后约 3 秒重生 | 提供可直接验收的默认配置，数值写入数据资产 |
| Experience | 训练场、竞技场各一个；通过地图旅行切换 | 证明框架能组合不同规则，而非硬编码一个地图 |
| 地图 | 一张小训练图、一张灰盒竞技图；一个极简前端地图 | 玩法闭环优先于场景美术 |
| 武器 | 一把步枪、一把手枪，固定两槽，均使用即时射线 | 足够验证装备生命周期、能力切换与弹药 |
| 能力 | 跳跃、瞄准、开火、装填、死亡等基础行为 | 用少量能力跑通 GAS，不扩展英雄技能库 |
| UI | 血量、弹药、准星、比分、计时、结算、创建／IP 加入／返回 | 足够验证模块化 HUD 和完整进入退出流程 |
| 非玩家对象 | 静态训练靶 | 首版不引入机器人导航、感知和行为树 |

“2–4 人联机”以独立进程与打包程序验证为准，不只是在编辑器内打开几个窗口。公网穿透、服务器列表、邀请好友与主机迁移不属于首版。

### 1.2 完成时应该能演示什么

1. 启动程序，选择训练场，或创建竞技对局并让其他玩家通过 IP 加入。
2. Experience 完成加载后生成角色；每个客户端完成自身初始化后才接受输入。
3. 两把武器可以切换、射击、装填；血量、命中结果、死亡与比分由服务器决定。
4. 玩家死亡后重生，旧 Pawn 的装备、输入、相机和 UI 绑定没有残留。
5. 对局达到时间或分数限制后统一结算，能再开一局，退出后能重新加入。
6. 训练场与竞技场共享角色和武器；竞技规则、专用 HUD 由功能插件装配。
7. 在最终 Cook 范围内不再依赖缺失的 `/Script/LyraGame` 类或未安装的原版玩法插件。

## 二、规划时的项目现状（任务 01 重置前，现已归档）

### 2.1 重置前存在的实现

| 已核查内容 | 当前状态 | 后续处理 |
| --- | --- | --- |
| `FPS.uproject` | `EngineAssociation` 为 5.8，单个 `FPS` Runtime 模块 | 保留工程名和模块名 |
| 本机 Lyra | `F:\LyraStarterGame\LyraStarterGame.uproject` 同样标记 5.8 | 作为本机源码参考；不假设它是未经修改的官方版本 |
| `MiniGameInstance` | 注册四个 InitState 的顺序，附有 CommonSession 可选反射绑定和加密测试数据 | 保留初始化职责，移除首版主链中的演示逻辑 |
| `MiniWorldSettings` | 从地图配置取得 Experience ID | 接入正式资产验证与可编辑的地图实例配置 |
| `MiniGameMode` | 下一帧选择“地图配置 → 项目默认” Experience ID | 继续扩展加载完成后的出生门控 |
| `MiniGameState` | 持有 ExperienceManagerComponent | 保留其复制与状态承载位置 |
| `MiniExperienceManagerComponent` | 复制 `CurrentExperienceId`，广播 ID 变化 | 补真实异步加载、状态、失败与生命周期 |
| `Docs/MiniExperienceBootstrap.md` | 描述现有启动壳及后续建议 | 作为历史入口说明，实施时同步更新 |
| `Content` | 已有大量 Lyra 美术和原版蓝图资产 | 先审计，再选择复用；资产存在不代表功能可用 |

现有源码只有上述五个 Mini 类及模块入口，尚无完整 Pawn、PlayerState、GAS、Inventory、Equipment 或 GameFeature 实现；项目下尚无 `Plugins` 目录。默认地图仍是 `/Engine/Maps/Templates/OpenWorld`。

### 2.2 必须先处理的具体问题

- 项目默认 ID 为 `MiniExperienceDefinition:B_DefaultMiniExperience`，尚未发现对应资产或 `MiniExperienceDefinition` 原生类。
- `HasCurrentExperience()` 目前只判断 ID 格式有效，不能当成“玩法已经加载”。正式实现必须区分 `HasExperienceSelection` 与 `IsExperienceLoaded`。
- `SetCurrentExperience()` 需要服务器权限约束。客户端通过复制接收选择，再独立加载本地资源。
- `MiniWorldSettings` 在查不到注册资产时构造 ID 的兜底会掩盖配置问题。正式资产链中应明确报错，不能伪装成功。
- 原版 `B_LyraDefaultExperience`、`SimplePawnData` 等资产可见引用指向 `/Script/LyraGame`，而当前工程没有该模块；部分 UI 还依赖 CommonGame/CommonUser。需要编辑器引用审计、蓝图编译和 Cook 才能确认完整依赖。
- 当前 CommonSession 反射绑定不代表已经完成会话系统；固定 Key/Nonce 也不代表实现了网络加密。它们不应成为首版功能验收项。

以上是大纲编写时的调查。任务 01 已根据用户的新指示建立空项目基线，后续 Mini 类按任务顺序重新实现。

## 三、保留哪些 Lyra 核心，如何缩小范围

### 3.1 必须保留的框架

| 核心系统 | Mini 中保留的最小形态 | 完成标志 |
| --- | --- | --- |
| AssetManager + Primary Assets | 类型注册、扫描、异步加载、Cook 规则、数据校验 | 编辑器和打包程序都能按 ID 找到 Experience |
| Experience + ActionSet | DefaultPawnData、功能插件列表、可组合 Actions | 换 Experience 数据即可装配不同玩法 |
| GameFeatures | 真实插件发现、激活、Action 执行和回收 | 激活会增加功能，停用／退出会撤销功能 |
| ModularGameplay | 扩展接收者、组件注入、扩展事件、InitState | 不靠固定 Delay 碰运气初始化 |
| PawnData + PawnExtension + Hero | 数据配置与玩家初始化职责分开 | 复制先后顺序变化、晚加入、重生都正常 |
| PlayerState ASC | ASC 和基础属性随 PlayerState 存活，Pawn 作为 Avatar | 重生后绑定新 Pawn，能力不重复或残留 |
| GAS + AbilitySet | 能力／属性／效果授予与句柄回收，InputTag 输入，Tag 阻断 | 能力由服务器授予，并能按来源撤销 |
| Enhanced Input | InputAction → InputTag → 原生行为或 GAS | 不在角色里硬编码所有键位与武器能力 |
| Inventory + Equipment | Definition/Instance 分离、装备授予能力、两槽 QuickBar | 切枪会切换真实装备与能力，不只是更换模型 |
| GameplayCue + GameplayMessage | 表现反馈与本地通知解耦 | 权威状态复制到客户端后再更新表现与 HUD |
| CameraMode | PawnData 选择相机，普通／瞄准模式切换 | 相机职责独立于枪械、角色输入代码 |
| CommonUI + UIExtension | 基本层栈、HUD 插槽、功能插件注入与撤销 | 卸载玩法后对应 UI 和输入捕获被释放 |
| GamePhase | GameState ASC 上的简化阶段能力 | Warmup、Playing、PostMatch 有明确开始与结束 |
| 联机生命周期 | 服务器权威、异步初始化、晚加入、重生、旅行清理 | 独立进程完成一整局并再次进入 |

GameFeatures 与 ModularGameplay 都要保留：前者负责功能插件生命周期，后者负责 Actor 上的组件扩展与初始化协作。仅启用其中一个不能替代另一个。

### 3.2 明确精简与延期的内容

| 内容 | 本期决定 | 仍需保留的边界 |
| --- | --- | --- |
| 原版 ShooterCore 整包 | 不整包迁入 | 参考武器、能力和装备设计，按依赖挑选素材 |
| 团队系统、占点、俯视模式 | 延期；本期做 FFA | 阵营／伤害过滤集中在规则接口，便于将来增加队伍 |
| EOS、Steam、跨平台账号、匹配大厅 | 延期 | 联机入口封装为小型流程，后续可替换接入方式 |
| CommonUser／Online 基础模块 | 按 CommonGame 的真实依赖保留 | 保留模块不等于启用平台登录和在线匹配 |
| 专用服务器、主机迁移 | 延期 | 不把客户端表现写进服务器必需逻辑 |
| 运行中热切换 Experience | 延期；首版通过地图旅行切换 | 正常退出、加载失败、异步回调失效仍须处理 |
| 复杂 GameSettings、完整前端 | 延期 | 首版仅最小可用菜单，允许基础鼠标灵敏度配置 |
| 高级动画层、角色部件、完整美容系统 | 延期 | 普通移动、持枪、开火、装填、死亡表现可工作 |
| 复杂背包、道具掉落、配件与稀有度 | 延期 | 仍保留库存与装备两个层次 |
| Bot、EQS、行为树、导航战斗 | 延期 | 用静态靶完成伤害与射击调试 |
| 热修复、回放、分屏、移动平台、完整音频混音 | 延期 | 不引入相应的产品承诺 |
| ReplicationGraph 和大规模网络优化 | 延期 | 保留正常复制、相关性、带宽检查和弱网验证 |

### 3.3 资产形式与迁移策略

**Mini 的 Experience 使用原生 `UPrimaryDataAsset` 的实例资产。** 命名采用 `DA_MiniPracticeExperience`、`DA_MiniArenaExperience`，PrimaryAssetType 固定为 `MiniExperienceDefinition`。扫描设置使用 `bHasBlueprintClasses=false`；WorldSettings 指向对应类型的软对象引用，加载完成后获得实例。

本机 Lyra 的 Experience 采用蓝图类／CDO 路线，其扫描使用 `bHasBlueprintClasses=true`。这里有意简化资产制作方式，同时保留 Experience 的装配职责。不得把 Mini 的实例资产加载代码与 Lyra 的蓝图类扫描规则混用，也不要继续指向缺失的 `B_DefaultMiniExperience`。

迁移遵守以下规则：

1. 新玩法资产放入 `/Game/Mini` 及 Mini 功能插件目录，形成可追踪的引用链。
2. 先建立 C++ 父类与插件依赖，再新建 Mini 蓝图和数据资产。现存原版 Gameplay 蓝图不直接视为成品。
3. 网格、纹理、音效、基础动画可以复用，但材质、骨架、动画蓝图同样需要引用审计。
4. 使用编辑器 Migrate／Reference Viewer 管理资源依赖；Migrate 不会自动迁移 C++、插件描述和 ini 配置。
5. 不做全局 `Lyra → Mini` 类重定向，不用文本替换修改 `.uasset`。必要的个别重定向必须有明确类映射和验证。
6. 首期保留现有 Content；通过地图清单和资产规则控制最终 Cook 范围。确认无引用前不删除旧资源。

## 四、目标架构与职责边界

### 4.1 启动、复制与初始化顺序

```text
MiniGameInstance：注册 InitState 顺序；持有本地入口与 UI 生命周期
MiniAssetManager：资产注册、加载入口、数据校验
                       ↓
服务器 MiniGameMode：选择地图 Experience → 项目默认
                       ↓
MiniGameState.MiniExperienceManager：复制 Experience ID
                       ↓
服务器与每个客户端分别加载自己的资源
Experience → 所需 GameFeatures → Actions／ActionSets → 本地 Loaded
                       ↓
服务器达到 Loaded 后：给 PlayerState 设置 PawnData，允许生成 Pawn
客户端通过 PawnData／PlayerState／Controller 的 OnRep 和初始化事件继续推进
                       ↓
PawnExtension／Hero 协作推进：Spawned → DataAvailable
→ DataInitialized：PlayerState ASC（Owner）绑定 Character（Avatar）
  本地 Hero 接好输入、相机；模拟代理不等待 LocalPlayer
→ GameplayReady
                       ↓
AbilitySet + 装备能力 → InputTag 驱动 → 服务器结算 → 复制／Cue → UI
```

服务器 Loaded 不等于所有客户端已经 Loaded；服务器可以正常生成复制 Pawn，尚未准备完成的客户端继续保持输入／加载门控。晚加入者要能从当前复制状态恢复，不依赖曾经广播过的一次性事件。

### 4.2 建议目录

```text
Source/FPS/
  System/          MiniGameInstance、MiniAssetManager、日志与 Native Tags
  GameModes/       MiniGameMode、MiniGameState、MiniWorldSettings
  GameFeatures/    现有 ExperienceManager、Experience/ActionSet、通用 Actions
  Character/       MiniCharacter、PawnData、PawnExtension、Hero、Health
  Player/          MiniPlayerState、MiniPlayerController、MiniLocalPlayer
  AbilitySystem/   ASC、AbilitySet、基础能力、属性、阶段能力
  Input/           InputConfig、InputComponent
  Camera/          CameraComponent、CameraMode
  Inventory/       ItemDefinition、ItemInstance、InventoryManager、Fragments
  Equipment/       EquipmentDefinition、EquipmentInstance、EquipmentManager、QuickBar
  Weapons/         通用射线武器数据与能力支持
  UI/              UI 策略、基础布局、HUD 数据桥接

Plugins/
  ModularGameplayActors/   复用基础插件
  GameplayMessageRouter/   复用基础插件
  CommonGame/、CommonUser/、UIExtension/
  GameFeatures/
    MiniShooterCore/        角色战斗装配、武器、战斗 HUD；训练／竞技共享
    MiniArena/              阶段规则、竞技 UI；只由竞技 Experience 启用

Content/Mini/
  System/Experiences/      两个入口 Experience，启动时能被扫描
  System/PawnData/          最小角色配置
  Input/、Characters/、UI/
  Maps/                    FrontEnd、Practice、Arena

Docs/
  MiniLyraRoadmap.md        本文
  MiniExperienceBootstrap.md
  MiniLyraProgress.md       从任务 01 开始记录实际完成情况
```

Mini 功能插件首版可采用内容插件，复用 `FPS` 中的通用原生类；不为每个系统拆一个 C++ 模块。`FPS` 不能反向静态依赖 Mini 功能插件。Experience 入口和基础 PawnData 留在启动可见的内容中，插件专属资产由插件注册、挂载后加载，避免“必须先读到插件内 Experience 才知道要激活插件”的循环。

### 4.3 生命周期约定

首版推荐 InventoryManager 和 QuickBar 放在 PlayerController，EquipmentManager 放在 Pawn，ASC 放在 PlayerState。PlayerController 可以跨重生活着，因此默认装备重置流程要主动清空／重建其库存和槽位，不能假定 Pawn 销毁会自动清理库存。

| 对象／资源 | 创建或授予者 | 释放／恢复规则 |
| --- | --- | --- |
| PlayerState 的基础 AbilitySet | 服务器，PawnData 确定后 | 相同 PlayerState 不随每次重生重复授予 |
| Pawn 绑定 ASC | PawnExtension | 旧 Pawn 仅在自己仍是 Avatar 时解绑 |
| 装备与武器 AbilitySet | 服务器 EquipmentManager | 切枪／死亡先撤销句柄，再解除 Pawn 的 ASC 绑定 |
| 初始武器与弹药 | 出生装配流程 | 首版每次重生恢复默认两把武器与初始弹药 |
| 输入映射与绑定 | 本地 Hero／GameFeature Action | 按句柄移除；解绑时清空按住状态，防止持续开火 |
| HUD 与 UI 扩展 | 本地 UI 策略／GameFeature Action | 按注册句柄释放，重建时从当前状态取值 |
| 插件激活与 Action 实例 | Experience 管理器 | 按 World／激活上下文跟踪；结束当前世界时撤销自己的贡献 |
| 比赛分数与阶段 | 服务器 GameState／PlayerState | 通过复制供晚加入者恢复；客户端消息只做通知 |

多 PIE 世界可能共享进程级 GameFeature 状态，不能因为一个 World 退出就停掉其他 World 仍在使用的插件。需要引用计数或等价的使用者追踪。

## 五、30 次任务总览

每次任务代表一个可以提交、演示和验收的工作单元，不保证等于一个晚上。按编号推进；“依赖”列列出直接关键前置条件，不重复列出全部传递依赖。任务 06、09、15、16、19、25 可能需要多个工作时段。

| 完成 | 编号 | 任务 | 关键依赖 | 可演示结果 |
| --- | --- | --- | --- | --- |
| [x] | 01 | 建立范围与构建基线 | 无 | 空项目 Editor 构建、基础地图命令行加载已通过 |
| [x] | 02 | 审计资源与建立 Mini 内容入口 | 01 | 49 包资源白名单、独立灰盒训练图与命令行加载验证 |
| [x] | 03 | 插件、模块、Tag 与日志基础 | 01–02 | 两个 target 编译、插件加载与扩展事件探针通过 |
| [x] | 04 | AssetManager 与数据定义 | 03 | 原生实例资产、地图覆盖项和按 ID 加载已验证 |
| [x] | 05 | Experience 异步加载状态机 | 04 | 两端独立加载、迟订阅与无效 ID 失败均经双进程探针验证 |
| [x] | 06 | GameFeature 激活与可回收 Actions | 05 | 双端与连续三次 PIE、Cook 及打包单机烟测通过 |
| [x] | 07 | Modular 角色骨架与出生门控 | 06 | 双端、第三人晚加入及失败时零出生探针通过 |
| [x] | 08 | PawnExtension／Hero 初始化协作 | 07 | 三进程两人／晚加入、两种条件可见性顺序与重复通知验证通过 |
| [x] | 09 | PlayerState ASC 与 AbilitySet | 08 | ASC／Avatar 复用、能力按来源授予撤销及三进程探针通过 |
| [x] | 10 | Enhanced Input 与 InputTag | 09 | 本地输入驱动测试能力；三次重生及菜单／Action 门控探针通过 |
| [x] | 11 | 第三人称相机与基础移动表现 | 10 | 能正常移动、瞄准、观察远端角色 |
| [x] | 12 | 基础能力与阻断规则 | 09–11 | 跳跃、瞄准等按规则激活和结束 |
| [x] | 13 | 血量、伤害、死亡与重生 | 12 | 双端看到一致的死亡与新 Pawn |
| [x] | 14 | Inventory 定义、实例与复制 | 09、13 | 服务器库存可靠同步给拥有者 |
| [x] | 15 | Equipment、QuickBar 与能力回收 | 14 | 两槽切换无旧武器能力残留 |
| [ ] | 16 | 服务器权威射线射击 | 13、15 | 一把步枪可以在联机中命中并扣血 |
| [ ] | 17 | 弹药、装填与第二把武器 | 16 | 两种武器完整射击／装填／切换 |
| [ ] | 18 | GameplayCue 与战斗表现 | 17 | 开火、命中、装填、死亡反馈一致 |
| [ ] | 19 | 消息、CommonUI 与模块化 HUD | 18 | HUD 随玩法注入并随退出清理 |
| [ ] | 20 | 完成训练 Experience | 19 | 一个可独立运行的训练模式 |
| [ ] | 21 | GamePhase 与竞技功能插件 | 20 | 三个比赛阶段按服务器节奏切换 |
| [ ] | 22 | FFA 规则、计分与出生点 | 21 | 完成有胜负的一局比赛 |
| [ ] | 23 | 灰盒竞技地图与数据装配 | 22 | 2–4 人规模的小地图可持续对战 |
| [ ] | 24 | 创建、IP 加入、退出与前端 | 23 | 从启动程序进入并退出对局 |
| [ ] | 25 | 晚加入、断线、重生与旅行收尾 | 24 | 生命周期反复执行无残留 |
| [ ] | 26 | 异步故障与弱网回归 | 25 | 加载／网络异常有可验证结果 |
| [ ] | 27 | 小规模性能与资源检查 | 26 | 有实测基线，并处理主要瓶颈 |
| [ ] | 28 | Cook 规则与打包资源收敛 | 27 | 最终白名单资源能完整打包 |
| [ ] | 29 | 打包后 2–4 人验收 | 28 | 独立程序完成完整联机闭环 |
| [ ] | 30 | 架构文档、交付与扩展示范 | 29 | 有可复现操作说明和后续扩展入口 |

## 六、逐次任务说明

### 阶段 A：工程与 Experience 主链（01–05）

#### 任务 01：冻结首版范围，建立可复现基线

- **目标：** 明确当前工程能构建到什么程度，后续改动有恢复点。
- **工作：** 核对本机 UE 5.8 的实际版本和编译工具；建立本地版本管理或完整备份；记录现有源码、配置、默认地图及打开工程时的错误。大资源使用合适的版本管理策略，排除 Binaries、Intermediate、Saved、DDC。
- **产物：** `Docs/MiniLyraProgress.md`，记录引擎位置、构建方式、基线问题及首版范围；保留已有启动文档。
- **验收：** 从明确的源码状态完成一次 `FPSEditor Win64 Development` 构建；能打开一个安全的测试场景。若原版蓝图报父类缺失，单独记录并交由 02 处理，不把它隐去。
- **边界：** 本次仅建立基线，不重写角色、不迁入整套 Lyra。

#### 任务 02：审计现有资产，建立 Mini 内容入口

- **目标：** 找到能安全复用的素材，建立不依赖原版 Lyra 类的工作空间。
- **工作：** 检查角色骨架、步枪／手枪网格、动画和基础材质；用 AssetRegistry 的硬／软包依赖图区分纯素材与依赖 LyraGame 的玩法资产；创建 `/Game/Mini` 和最小灰盒测试地图，设置 Mini 默认入口。
- **产物：** 可复用素材清单、需要重建的蓝图清单、`L_MiniPractice` 雏形；标注来源与引用路径。
- **验收：** 新地图打开时不依赖缺失的 LyraGame 玩法类；选中的基础素材可正常加载；使用引擎 WorldSettings 即可。MiniWorldSettings 的创建与地图 Experience 配置在 04 验收。
- **边界：** 任务 01 归档的旧 Content 留在 `Backups`；不整体加载／修复几千个原版资产，不先追求漂亮场景。
- **执行结果：** 8 个种子及其依赖共 49 个素材包迁入 `/Game/Mini`，另有 2 个静态枪模仅审计；`L_MiniPractice` 的 4 个出生点、6 处掩体和 3 组静态靶已建立。UE 5.8 命令行验证加载全部 49 包并重开地图，地图使用引擎 WorldSettings，未发现 `/Script/LyraGame` 或旧 `/Game` 路径引用；GUI、PIE 与联机尚未执行。详见 `Docs/Task02/AssetSelection.md`。

#### 任务 03：建立插件、模块、Native Tags 与日志基础

- **目标：** 一次核清框架基础依赖，后续逐项实现行为。
- **工作：** 按需启用引擎侧 GameplayAbilities、GameFeatures、ModularGameplay、EnhancedInput、CommonUI；从本机 Lyra 复用 ModularGameplayActors、GameplayMessageRouter、CommonGame、CommonUser、UIExtension，并核查传递依赖与运行时模块。集中定义 Mini Native Tags，从空基线创建 MiniGameInstance 并注册四阶段状态顺序；建立 Experience／Init／Ability／Equipment 日志分类。
- **迁移细节：** 本机 `UIExtension.uplugin` 有重复的 `Plugins` 字段，迁入副本应整理为合法的单一依赖数组。保留 CommonGame 所需 CommonUser／OnlineFramework 等基础依赖，首版不启用 EOS／Steam。移出 GameInstance 主链中无关的反射式会话探测与加密演示数据，记录原用途。
- **产物：** 明确的 `.uproject`、Build.cs、插件依赖清单与 Tag 命名表；核定后续 Common／Modular 基类选择。
- **验收：** Editor 与 Game target 的编译均不缺模块，Tag 可查询，插件载入无依赖错误。先用日志验证扩展接收者机制，不把所有插件都设为自动 Active。
- **执行结果：** 五个 Lyra 项目插件已按源码迁入，所需引擎插件已启用；`FPS` 建立 CommonGame／ModularGameplay 的直接模块依赖、四个 Native InitState Tag 与四类日志。`UMiniGameInstance` 继承 `UCommonGameInstance`；最小 UI Manager 子类、本地玩家／Viewport 配置和 `GameFeatureData` 扫描规则满足基础启动要求。Editor、Game target 编译通过；`Scripts/VerifyTask03.ps1` 在未 Cook 的编辑器游戏进程中检查到五个插件加载、Tag 查询和三个扩展事件，退出码为 0。正式 UI、GameFeature 激活和角色初始化留给后续任务；详见 `Docs/Task03/PluginDependencies.md`。

#### 任务 04：实现 MiniAssetManager 与三类数据定义

- **目标：** 让 Experience ID 真正对应可以加载的数据。
- **工作：** 创建 `MiniAssetManager`、`MiniWorldSettings`、`MiniExperienceDefinition`、`MiniExperienceActionSet` 与最小 `MiniPawnData`。Experience 包含 DefaultPawnData、GameFeaturesToEnable、Actions、ActionSets；PawnData 先有 PawnClass，后续任务再逐项加能力／输入／相机字段。设置稳定 PrimaryAssetType、扫描与基本 Cook 规则，并验证地图实例上的 Experience 配置可编辑。
- **产物：** 实例资产 `DA_MiniPracticeExperience` 和空 ActionSet；把项目默认 ID 更新为真实资产，WorldSettings 改为明确类型与验证逻辑。
- **验收：** 用 ID 能查到唯一资产并加载；未知 ID、空 PawnData 等必填数据可给出明确错误。确认扫描 `bHasBlueprintClasses=false` 与实例加载匹配。
- **边界：** 保留通用 AssetManager，不照搬 Lyra 的大型启动任务调度、热修复或全部 GameData。
- **执行结果：** `MiniAssetManager` 与三类原生 `UPrimaryDataAsset` 已建立，项目默认 ID 指向真实 `DA_MiniPracticeExperience`。训练图保存 `MiniWorldSettings` 和该 Experience 的覆盖项；PawnData 暂用引擎 `Pawn` 类，ActionSet 保持空。Editor／Game target 构建成功；新进程重开资产与地图、未 Cook 编辑器游戏按 ID 加载及未知 ID／空 DefaultPawnData／空 PawnClass 负例均通过。三类扫描项配置 `AlwaysCook`，本次未实际 Cook 或打包，异步加载留给任务 05。详见 `Docs/Task04/AssetDefinitions.md`。

#### 任务 05：补全 Experience 异步加载与失败状态

- **目标：** 从“复制 ID”升级为可观察、可订阅的加载流程。
- **工作：** 从空基线创建 MiniGameMode、MiniGameState 和 MiniExperienceManagerComponent，接通地图配置优先、项目默认回退的服务器选择逻辑。明确 Unloaded、LoadingAssets、LoadingFeatures、ExecutingActions、Loaded、Failed、Deactivating 等状态；服务器选择 ID，客户端 OnRep 开始本地加载。实现一次性或立即调用的 Loaded 订阅、失败原因、加载句柄与无效回调防护。
- **产物：** `MiniExperienceManagerComponent` 的新状态接口与状态日志；调试加载提示。
- **验收：** 双端复制同一个 ID 后各自执行加载；迟订阅仍收到当前 Loaded 状态；无效 ID 会 Failed 而非永久转圈；结束世界后异步回调不会访问销毁对象。
- **阶段限制：** 本任务仅在没有 GameFeature 的测试 Experience 上闭环。完整 Loaded 必须在 06 的插件激活与 Actions 完成后才成立；不能收到 ID 就通知角色开始游戏。
- **执行结果：** 服务器选择与 ID／选择错误复制、各端异步加载、七态状态机、终态迟订阅、失败原因和退出回调防护已写入。Editor／Game target 构建通过；`Scripts/VerifyTask05.ps1` 在监听服务器／客户端的正常与无效 ID 场景均通过，任务 03、04 回归也通过。在途加载与 EndPlay 交错的保护已做源码审查，尚无受控延迟探针的动态竞态证明。实现与验证边界见 `Docs/Task05/ExperienceFlow.md`。

### 阶段 B：模块装配与角色初始化（06–10）

#### 任务 06：实现 GameFeature 装配与可撤销 Actions

- **目标：** 让 Experience 真实启用功能，而不只是保存插件名。
- **工作：** 创建 `MiniShooterCore` 内容插件与 GameFeatureData，核清发现／注册／挂载策略；根据 Experience 与 ActionSet 去重加载插件。先实现 World 上下文 Action 支持与一条 AddComponents 示例，保存注入句柄并逆序回收；处理插件激活失败、途中结束和多 PIE 使用者追踪。
- **产物：** 一个通过 Experience 激活的最小插件；一个可观察的测试组件；后续 AddAbilities／Input／Widget 共用的生命周期基础。
- **验收：** 功能出现与撤销都能观察；连续三次 PIE 不重复注入；服务器与客户端状态独立；任一必需插件失败时不进入 Loaded。早做一次最小地图 Cook／打包烟雾检查，确认插件被发现。
- **边界：** 不承诺运行中热切换整个 Experience；正常退出和失败回滚必须可用。
- **执行结果：** `MiniShooterCore` 内容插件、GameFeatureData、两个 AddComponents marker、按 URL 去重激活、Experience 自有 Action 的 World 作用域撤销及进程级插件使用者计数已接通。Editor／Game 构建、资产脚本重复运行、双进程有效／缺失插件探针、三轮 World travel 与 `Scripts/VerifyTask06PIE.ps1` 的真实连续三次 PIE 均通过。最小地图 Cook 退出码 0，报告 513 个已 Cook 包、0 error／0 warning；Cook `AssetRegistry.bin` 确认包含 GameFeatureData、训练 Experience 和地图。完整打包前两次在 staging 失败；第三次用 `-pak -skipiostore -AdditionalCookerOptions=-SkipZenStore` 成功 Cook／Stage，独立 `FPS.exe` 单机启动后 Experience `Loaded` 且两种 marker 注入。IoStore staging 的 Zen 问题、打包后联机及同进程不同 Experience 的 World 并存未验收；UE 内置 GameFeatureData AddComponents 是进程级激活，可能影响未请求插件的并存 World。详见 `Docs/Task06/FeatureAssembly.md`。

#### 任务 07：建立 Modular 角色骨架与出生门控

- **目标：** Experience 和 PawnData 决定生成什么角色。
- **工作：** 建立 MiniPlayerState、MiniPlayerController、MiniLocalPlayer、MiniCharacter 与基础 HUD 类。采用合适的 ModularGameplayActors／CommonGame 基类，或完整实现等价扩展接收者生命周期；配置类入口。GameMode 等待 Experience Loaded 后设置 PawnData、选择 PawnClass，并通过延迟生成流程在 FinishSpawning 前设置必要数据。
- **产物：** `BP_MiniCharacter`、真实 PawnData、角色生成与玩家加入日志。
- **验收：** 双客户端和晚加入场景生成正确角色；Experience 未就绪时不提前生成默认 Pawn；插件能对正确 Actor 类型注入组件。
- **边界：** 此时允许只有简单可见角色和调试摄像机；不以外观表现替代初始化验证。
- **执行结果（2026-09-28）：** 已加入 `AMiniPlayerState`、`AMiniPlayerController`、`UMiniLocalPlayer`、`AMiniCharacter` 与最小 `AMiniHUD`；GameMode 将出生与重启限定在服务端 Experience `Loaded` 之后，使用 PawnData 选类，并在延迟生成的 `FinishSpawning` 前向 Pawn 注入数据。`BP_MiniCharacter` 使用 Manny Simple 网格，训练 PawnData 指向该蓝图；`MiniShooterCore` 为后生成的 Modular 角色注入专用 marker。`MiniPawnData::ValidatePawnData` 还要求 PawnClass 继承 `AMiniCharacter`，普通 `APawn` 负例会在资产校验时失败；任务 04 资产脚本仅给新资产设置原生 MiniCharacter 占位，不重置已有蓝图引用。补丁后 Editor／Game target 重编译、任务 04 运行／资产验证、任务 07 资产重复创建／全新进程复核和联机探针均通过：双玩家开局、第三人晚加入后三方的 PlayerState／角色／有效角色／marker 数量均为 3，本地 Pawn 各 1；服务端三次出生均晚于 Experience `Loaded`。无效 Experience 双端进入 `Failed` 且零角色出生。`VerifyTask06.ps1` 含任务 05 双进程、缺失插件负例与三轮 World 周期的回归也通过。这些联机探针运行于未 Cook、NullRHI 的编辑器游戏进程；详见 `Docs/Task07/SpawnGate.md`。

#### 任务 08：实现 PawnExtension 与 Hero 的四阶段协作

- **目标：** 消除对 BeginPlay、固定 Delay 和复制顺序的依赖。
- **工作：** PawnExtension 管 PawnData 与跨组件依赖，Hero 管玩家输入／相机准备。接入 `IGameFrameworkInitStateInterface`；在 PawnData、PlayerState、Controller 到达、Possess 和扩展事件发生时重新检查条件；按 Authority、自治代理、模拟代理区分准备条件。
- **产物：** 四状态推进日志与角色条件表，明确每个状态等待哪些数据。
- **验收：** 延迟 PawnData／PlayerState 时可以等待并继续；模拟代理不等待本地 LocalPlayer 或 InputComponent；重复通知不重复初始化。
- **阶段限制：** 08 验收状态协作骨架；09 接入 ASC、10 接入本地输入后，才验收完整 GameplayReady。未实现的条件明确标注，不能用 Delay 假装完成。
- **执行结果（2026-09-28）：** 已给 `AMiniCharacter` 加入原生 PawnExtension／Hero 组件，以 `IGameFrameworkInitStateInterface` 推进到 `DataInitialized`；Pawn 与 PlayerState 的 PawnData 一致性、Authority／AutonomousProxy 的 Controller／PlayerState 配对、其他 Feature 的状态均纳入条件。Editor／Game target 构建与 `Scripts/VerifyTask08.ps1` 三进程两人／晚加入探针通过；最终配对补丁后 Editor／Game 与三进程探针又重跑通过。ClientA／ClientB 分别验证 PawnData 优先和 PlayerState 优先的合成条件可见性顺序，实际释放日志先于 `DataAvailable`；两名无 Controller／InputComponent 的模拟代理仍完成初始化，重复通知没有重复转换。该探针不操纵真实网络包；任务 07 回归及 `VerifyTask06.ps1` 所含任务 05／06 回归通过。`GameplayReady` 保持关闭，ASC 与输入分别留给任务 09／10。详见 `Docs/Task08/InitStateCoordination.md`。

#### 任务 09：把 ASC 放到 PlayerState，建立 AbilitySet 生命周期

- **目标：** 建立 Lyra 的能力宿主与 Pawn Avatar 分离结构。
- **工作：** MiniPlayerState 持有复制 ASC 和基础 AttributeSets，实现 AbilitySystemInterface；PawnExtension 负责 InitAbilityActorInfo 与安全解绑。实现 MiniAbilitySet、GrantedHandles，支持能力、效果、属性成组授予和撤销；接入一条 GameFeature AddAbilities 动作，明确各授予来源。
- **产物：** 测试 AbilitySet、测试能力、ASC 绑定日志与句柄清单。
- **验收：** 服务器只授予一次；拥有者与模拟代理获得正确可见状态；重新生成 Pawn 后复用同一 PlayerState ASC 且 Avatar 正确；撤销测试 Action 后能力数量恢复。
- **边界：** 不把 ASC 简单移到 Character 来回避生命周期。旧 Pawn 清理时必须检查自己仍是当前 Avatar。
- **执行结果（2026-09-28）：** PlayerState 已持有复制 ASC 与基础 HealthSet；PawnExtension 绑定可替换 Avatar，并在旧 Pawn 清理时检查当前 Avatar。PawnData 与 World 作用域 AddAbilities Action 各用 AbilitySet／句柄单独授予和撤销，训练资产已在新编辑器进程重新加载验证。Editor／Game target 构建通过；三进程探针验证两人授予、功能来源撤销／恢复、复用同一 PlayerState ASC 重生以及第三人晚加入后的拥有者／模拟代理状态。任务 06 的三个真实 World 停用周期均出现相应 Action 撤销日志；任务 08／07 回归通过。`GameplayReady` 继续等待任务 10 的本地输入。详见 `Docs/Task09/AbilityLifecycle.md`。

#### 任务 10：接通 Enhanced Input → InputTag → GAS

- **目标：** 输入由数据定义，并能在重生与功能停用时完整解绑。
- **工作：** 创建 MiniInputConfig、MiniInputComponent 与最小 IMC／InputActions；移动／视角走原生输入，能力输入通过 Tag 传给 ASC。实现按下、按住、释放处理，由本地 Controller 在合适输入处理阶段调用 ASC；接入可撤销 InputMapping／InputBinding Action。
- **产物：** 移动、视角、跳跃、开火、装填、切枪、瞄准的输入配置；完整 GameplayReady 门控。
- **验收：** 本地玩家控制自己的 Pawn；重生三次不产生多重绑定；菜单接管或插件撤销时不继续开火；模拟代理不创建输入映射。
- **参考注意：** 本机 Lyra 的 `RemoveAdditionalInputConfig` 仍有 TODO，Mini 必须补齐自身实际使用的解绑，不直接把 TODO 当成已完成功能。
- **执行结果（2026-09-28）：** 已实现数据化 InputConfig／InputComponent、七个输入资产和 InputTag，接通本地 Hero 绑定、Controller 输入处理与 PlayerState ASC；Experience 的 World 作用域 AddInput Action 管理映射和解绑，`GameplayReady` 按 ASC／本地映射条件推进。Editor／Game target 构建、资产新进程重载和双进程专项探针通过：客户端移动、四轮开火／释放、服务端三次重生、单份映射、模拟代理隔离、菜单与 Action 测试暂停／恢复均已验证。任务 08／09／07／06 回归通过；任务 08 三端第三人晚加入后各有 3 个角色到达 `GameplayReady`，任务 06 三次真实 World 停用各有输入解绑。Fire 仍是测试能力，菜单仅有输入门控接口；详见 `Docs/Task10/InputPipeline.md`。

### 阶段 C：可玩角色与装备模型（11–15）

#### 任务 11：建立第三人称 CameraMode 与基础移动表现

- **目标：** 得到可用于战斗调试的第三人称角色。
- **工作：** 实现 MiniCameraComponent、CameraMode 与简单模式切换，普通跟随／瞄准模式从 PawnData 和能力状态选择；处理墙体遮挡与基础插值。以现有骨架重建最小动画蓝图，覆盖待机、移动、跳跃和持枪。
- **产物：** 两个相机模式、最小动画蓝图、角色与武器挂点约定。
- **验收：** 本地移动和镜头正常；远端看到移动与朝向；靠墙时相机不会明显穿透；重生后镜头回到新 Pawn。
- **边界：** 暂缓原版全部动画层、脚步材质效果、复杂 IK、角色美容部件。
- **执行结果（2026-09-28）：** 已实现 PawnData 驱动的普通／瞄准 CameraMode、墙体球扫掠与恢复插值；四个练习地图出生点调整为统一的第三人称初始角度。已创建五段动作的最小 Manny 动画蓝图，角色与演示步枪使用 `HandGrip_R` 挂点。修正了早期角色资产脚本的网格旋转错误，渲染 PIE 确认角色直立、全身可见且步枪贴近肩部。Editor／Game target 构建、相机与动画资产新进程重载、任务 11 双进程专项探针、任务 10 输入生命周期、任务 07 资产及任务 08 三端晚加入回归通过；详见 `Docs/Task11/CameraAnimation.md`。正式瞄准能力已由任务 12 完成，复杂左手 IK 留在后续任务。

#### 任务 12：实现基础 GameplayAbility 与 Tag 阻断规则

- **目标：** 建立后续开火、装填、死亡能够共享的能力行为约定。
- **工作：** 创建 MiniGameplayAbility 基类，支持实际使用的输入激活策略；完成跳跃／瞄准等小能力；实现精简的激活组或 TagRelationshipMapping，统一死亡、装填、输入阻断的规则与取消方式。
- **产物：** 可复用能力基类、Tag 关系数据与基础 AbilitySet。
- **验收：** 能力正常结束与取消；瞄准能退出；服务器拒绝不符合条件的激活；输入释放和切换 Pawn 不遗留持续能力。
- **边界：** 不做冲刺、翻滚、英雄大招等扩展能力。
- **执行结果（2026-09-28）：** 已实现通用 GameplayAbility 基类、跳跃／瞄准持续能力和 `DA_MiniTagRelationships`；Pawn AbilitySet 授予 Fire 测试能力、Jump、Aim，PawnData 将 Tag 关系交给 PlayerState ASC。死亡、装填、输入阻断分别按规则拒绝并取消能力，瞄准状态随能力结束撤销，切换 Pawn 后旧输入与持续能力清理。Editor／Game target 构建、资产两次重复生成与新进程重载、任务 12 双进程专项探针通过；任务 09／10 资产及任务 09／10／11 联机回归通过。详见 `Docs/Task12/AbilitiesAndTags.md`。

#### 任务 13：完成血量、伤害、死亡与重生

- **目标：** 在枪械接入前先验证一条稳定的服务器生命流程。
- **工作：** 实现 HealthSet、HealthComponent 和最小 Damage GameplayEffect／结算逻辑，服务器使用测试伤害入口触发扣血；伤害过滤先处理自己、已死亡目标和无效目标。建立一次性死亡流程、约 3 秒重生与新 Pawn 初始化，清除本局约定的死亡状态、临时效果并重置血量。
- **产物：** 可重复使用的训练靶或伤害测试入口、死亡状态、重生流程。
- **验收：** 服务端与客户端血量一致；同一次死亡只处理一次；连续死亡重生不重复授予基础能力，旧 Pawn 不清掉新 Avatar；复活后输入与镜头恢复。
- **边界：** 血量和死亡结果不由客户端自行修改；装备清理在 15 接入这条流程。
- **执行结果（2026-09-29）：** 已接入 PlayerState ASC 上的 HealthSet 伤害结算、服务端测试伤害入口、HealthComponent 一次性死亡状态和 GameMode 重生流程。死亡效果只由创建它的旧 Pawn 清理；出生失败重试及旧 Pawn 提前销毁后的 Avatar 绑定检查均已处理。Editor／Game target 构建、任务 13 双进程专项探针与任务 10／11／12 联机回归通过。专项探针验证客户端血量 100→75→0→100、两次约 3.02 秒定时重生、第三次强制解绑后的立即重生及迟到计时器清理；ASC 与 4 项基础能力保持稳定，新 Pawn 的输入和镜头恢复。详见 `Docs/Task13/HealthDeathRespawn.md`。

#### 任务 14：实现 Inventory 的定义、实例与复制

- **目标：** 明确“拥有物品”这一层，为装备和弹药提供数据来源。
- **工作：** 创建 ItemDefinition、ItemInstance、InventoryManager；保留少量 Fragment，例如初始数值、可装备定义与图标。库存由服务器管理，使用 FastArray 和匹配的 UObject 子对象复制机制；只向拥有者发送其私有库存。
- **产物：** 两个武器物品定义、服务器添加／移除接口、拥有者调试库存视图。
- **验收：** 拥有者能收到添加、移除和初始快照；非拥有者无须获取完整私有库存；对象移除时不存在失效子对象引用。
- **边界：** 先核定项目使用的复制路径；传统子对象复制、registered subobject list、Iris 支持不能未经核查混搭。暂不做背包格子和拖拽 UI。
- **执行结果（2026-09-29）：** 已实现原生步枪／手枪 ItemDefinition、初始数值／可装备定义预留／图标 Fragment、可复制 ItemInstance，以及 PlayerController 私有 InventoryManager。服务端通过 FastArray 和当前 Generic 模型下的传统 UObject 子对象复制增删物品；移除时通知远端销毁旧实例。`MiniDumpInventory` 输出拥有者调试快照。Editor／Game target 构建、任务 14 三进程专项及任务 10–13 联机回归通过；专项验证两个拥有者各收初始快照、运行时弹药 30→29、客户端越权修改被拒、删除后远端弱引用失效、重新添加生成新 GUID。任务 11 的无渲染探针延长远端移动观察窗口后重复通过。普通游戏流程的默认发放与装备留待任务 15；详见 `Docs/Task14/InventoryReplication.md`。

#### 任务 15：实现 Equipment、两槽 QuickBar 与可撤销能力

- **目标：** 让装备实例、外观、能力与物品实例建立稳定关系。
- **工作：** 创建 EquipmentDefinition、EquipmentInstance、EquipmentManager、FromEquipment 能力基类与两槽 QuickBar；服务器负责装备切换，装备实例关联来源物品；生成持枪外观并复制当前装备。能力只由对应装备授予并记录句柄。
- **产物：** 步枪／手枪装备定义、两槽切换、出生默认装备流程。
- **验收：** 自己与远端看到同一当前武器；切换后旧装备能力被撤销；死亡时先卸装与撤销能力，再解绑 ASC；重生恢复默认武器和弹药，不复制旧实例。
- **边界：** 不把“显示哪把枪”和“当前有哪些可激活武器能力”分成两套互不关联的状态。
- **执行结果（2026-09-29）：** 已实现原生步枪／手枪 EquipmentDefinition、来源物品关联的 EquipmentInstance、Pawn 上的 EquipmentManager、`FromEquipment` 能力基类及 PlayerController 上的私有两槽 QuickBar。`Q` 输入经服务器切换；当前装备与持枪网格向观察者公开，库存与弹药仅供拥有者查看。死亡先卸装并回收能力，重生重新创建物品和默认步枪；旧任务 13／14 探针的默认装备初始化已隔离。Editor／Game Development 构建、任务 15 三进程专项及任务 10–14 联机回归通过；专项核对两名客户端实际按 Q 后的服务器换装、旧能力句柄撤销与当前 SourceObject、双端外观、死亡清理、新 GUID／初始弹药，以及同一 Pawn 删除物品后不会被重复初始化通知补回。详见 `Docs/Task15/EquipmentQuickBar.md`。

### 阶段 D：射击闭环与可组合 HUD（16–20）

#### 任务 16：完成一把服务器权威的射线步枪

- **目标：** 跑通 InputTag → GA → 装备 → 命中 → GameplayEffect → 死亡。
- **工作：** 实现射线武器能力和最小瞄准／TargetData 或开火请求传输。本次先接入物品数值上的最小弹药检查与扣除，17 再完成弹匣／备用弹药／装填系统。客户端负责即时表现，服务器核验当前装备、存活状态、射速、弹药、射程、发射起点与方向合理性，再决定命中和伤害；处理第三人称相机射线与枪口遮挡。
- **产物：** 步枪 Fire Ability、服务器命中逻辑、可开关的调试射线。
- **验收：** 双端射击扣血一致；朝墙／枪口被挡时不能穿墙；客户端上报的目标和伤害数值不能直接成为结果；单次请求不会重复伤害。
- **边界：** 首版不做服务器回溯和竞技级延迟补偿。使用服务器当前世界判定，记录延迟下的表现局限。
- **执行结果（2026-09-29）：** 当前步枪装备只授予一项绑定 `InputTag_Fire` 的 RifleFire GA；按下左键由本地 GA 发送相机起点、方向与递增序号。Pawn 上的 RangedWeaponComponent 在服务器验证存活、当前步枪与来源物品、阻断 Tag、序号、视点／方向、射速与弹匣，再用相机射线及枪口射线核定首个遮挡，最后经 GameMode 的 GameplayEffect 扣血／死亡。临时本地短射线与可开关的服务器调试射线已接入。Editor／Game 构建和三进程专项通过，覆盖命中、墙体、枪口单独遮挡、伪造视角、重放、射速、空弹匣及两端死亡复制；任务 10–15 回归结果见 `Docs/Task16/AuthoritativeRifle.md`。当前每次按键一发，连续开火与装填体验留待后续打磨。

#### 任务 17：完成弹药、装填与手枪配置

- **目标：** 让两把武器具备完整、互不污染的运行状态。
- **工作：** 明确弹匣和备用弹药的唯一数据源，服务器负责最终消耗与装填；实现装填能力、结束／取消路径和射击阻断；以数据配置第二把半自动手枪，复用共用能力代码。
- **产物：** 两组武器数据、装填能力、空仓反馈接口。
- **验收：** 空仓不能产生有效伤害；满弹匣不浪费备用弹药；切枪／死亡会正确取消装填；两把武器弹药独立；拥有者显示与服务器一致。
- **边界：** 若添加本地弹药预测，必须包含服务器纠正；首版可先以权威复制保证准确。

#### 任务 18：用 GameplayCue 和动画完善战斗反馈

- **目标：** 玩家能够看懂命中、受伤、装填和死亡。
- **工作：** 添加少量枪口、命中、受伤 Cue，接入基础开火／装填 Montage 与声音；确保插件内容中的 Cue 路径被发现；区分拥有者即时反馈与远端复制表现。
- **产物：** 可见可听的步枪／手枪反馈、准星命中事件、基本死亡表现。
- **验收：** 本地预测与服务器确认不会导致双重枪声或双重特效；远端可看见正确射击／装填；结束能力后循环效果会停止；表现丢失不影响服务器伤害结算。
- **边界：** 不追求完整 Lyra 音频调制和动画表现体系。

#### 任务 19：建立消息总线、CommonUI 层栈与 HUD 注入

- **目标：** HUD 能读取当前状态，且随功能启用／停用。
- **工作：** 使用 CommonGame 的 UI 策略和 PrimaryGameLayout，接入 CommonUI 的 Game／Menu／Modal 最小层栈及 UIExtension 插槽；实现 GameFeature AddWidget 动作。HUD 通过属性、装备与复制状态桥接，再使用 GameplayMessage 做本地通知；每次新建 HUD 先取得当前快照再订阅变化。
- **产物：** 血量、弹药、准星、比分／计时占位、调试加载／错误界面，明确所有监听句柄的释放位置。
- **验收：** 晚创建 HUD 也显示正确初值；重生后重新绑定正确对象；主机与客户端看到自己的数据；插件撤销后 Widget、监听、输入捕获都消失。
- **边界：** GameplayMessage 默认不是网络消息；分数、死亡等仍先经复制／必要 RPC 到达客户端。前端的产品流程留到 24。

#### 任务 20：完成训练 Experience，并验证组合边界

- **目标：** 交付第一个完整可玩的模式，作为后续竞技场的稳定底座。
- **工作：** 将角色、两武器、训练靶和战斗 HUD 装配进 `DA_MiniPracticeExperience`；训练场启用 MiniShooterCore，不启用竞技阶段和计分插件；整理共用 ActionSet，清理临时硬编码与一次性测试入口。
- **产物：** 可以独立进入的 `L_MiniPractice`，共享战斗装配数据；再做一次打包烟雾检查。
- **验收：** 单人能练习射击／换弹／切枪；双端能观察正确动作；退出重进无多重 UI 和能力；改变 Experience 配置即可增减测试功能，核心角色代码不用分地图判断。
- **里程碑：** 到这里应有“可运行的 Mini Lyra 框架 + 可玩训练场”，即使后面的竞技规则尚未完成。

### 阶段 E：竞技玩法与进入退出（21–25）

#### 任务 21：建立简化 GamePhase 与 MiniArena 功能插件

- **目标：** 竞技阶段具备独立、服务器驱动的生命周期。
- **工作：** 创建 MiniArena 内容插件及对应 GameFeatureData；实现 GameState 上独立于玩家 ASC 的阶段 ASC、MiniGamePhaseSubsystem／PhaseAbility 最小版本。仅保留 Warmup → Playing → PostMatch 的开始、结束与查询；通过复制状态和服务器结束时间支持客户端计时与晚加入。
- **产物：** 三个阶段能力／配置与竞技 Experience 雏形；竞技规则组件由 Action 注入。
- **验收：** 只有服务器切换阶段；阶段结束能取消该阶段工作；晚加入直接读到当前阶段与剩余时间；训练场不启动竞技阶段。
- **边界：** 不实现任意嵌套阶段树，也不复用玩家 ASC 保存整场比赛状态。

#### 任务 22：实现 FFA 计分、胜负与出生规则

- **目标：** 做出有明确开始、过程与结束的一局游戏。
- **工作：** 服务器记录 PlayerState 击杀／死亡与 GameState 比赛状态；定义自杀／环境伤害不增加击杀分、同一死亡只结算一次，平分到时按并列结果处理。默认至少两人进入后结束 Warmup，少于两人时等待；对局中离开至不足两人时结束并返回等待流程。实现 10 分或 5 分钟结束条件、基础出生点选择、短时出生保护；Playing 外阻止有效伤害。
- **产物：** MiniMatchRules 数据、服务器规则组件、比分与结算数据；竞技 HUD 注入。
- **验收：** 击杀、死亡、超时、达分、平局均有一致结果；PostMatch 不继续加分；同一事件不会被消息回调重复计分；客户端不能自行增加分数。
- **边界：** 原版 Lyra Elimination 以团队玩法为基础，FFA 不直接套用其团队计分蓝图。

#### 任务 23：完成小型灰盒竞技地图与装配

- **目标：** 让规则在适合 2–4 人的小空间中可持续运行。
- **工作：** 完成 `L_MiniArena`，设置掩体、可读动线、多个出生点与跌落处理；用少量现有素材提高辨识度。配置 `DA_MiniArenaExperience` 启用 MiniShooterCore + MiniArena，配置地图覆盖与项目默认回退。
- **产物：** 一张完整竞技图、两张玩法地图对应的 Experience 与 ActionSet。
- **验收：** 2–4 人能连续对战，不困在出生点；两个 Experience 的功能差异由数据和插件决定；加载入口不存在插件挂载循环或缺失软引用。
- **边界：** 不增加更多地图、复杂场景破坏或大型开放世界结构。

#### 任务 24：实现极简前端、创建、IP 加入与退出

- **目标：** 用户无需控制台即可从程序入口完成进入／退出。
- **工作：** 创建轻量前端地图和 CommonUI 菜单，提供训练、创建竞技 Listen Server、输入 IP 加入、返回与退出；封装服务器 Travel／客户端 Travel，显示连接中、失败与断线原因。服务器执行最多四人的加入限制并向超额连接反馈原因。首版采用普通地图旅行并重建对局对象，明确再开局与返回的目的地图。
- **产物：** `L_MiniFrontEnd`、最小前端 Widget 与 Travel 流程；更新默认地图和地图打包清单。
- **验收：** 两个独立进程通过界面加入；错误 IP／连接失败能恢复交互；主机结束对局后客户端获得明确结果；前端不需要账号登录即可使用。
- **边界：** CommonUser 可作为依赖存在，但不做 Session 搜索、邀请、平台认证。提前定下首版使用普通旅行，避免同时承诺无缝旅行和跨局对象保留。

#### 任务 25：收紧晚加入、断线、重生与地图旅行生命周期

- **目标：** 把“第一次能玩”提升为“能反复稳定进入和退出”。
- **工作：** 系统检查 Loaded 迟订阅、PawnData／PlayerState 到达顺序、装备撤销与 ASC 解绑次序、HUD 重绑定、输入解除、异步回调取消、插件使用者释放；处理离开玩家与主机断开。普通地图旅行后按约定重建状态，不隐式依赖旧 PlayerState。
- **产物：** 生命周期检查记录与修复；每类注册句柄都有对应释放点。
- **验收：** 一人晚加入正在进行的比赛；每人连续死亡重生 10 次；训练／竞技往返至少 3 次；退出后重新加入；主机关闭后客户端能返回前端。期间无重复装备能力、残留 UI 或持续输入。
- **边界：** 热切换 Experience 和主机迁移仍延期，但销毁世界过程中的安全回调与回收不能延期。

### 阶段 F：验证、打包与交付（26–30）

#### 任务 26：异步故障、网络时序与弱网回归

- **目标：** 覆盖正常玩法演示容易遗漏的失败路径。
- **工作：** 对未知 Experience、缺失必需插件、资源加载失败、加载中返回、迟到复制、重复能力通知做定向验证；在可复现网络模拟中检查延迟与丢包；仅为关键状态与规则编写有价值的自动化测试，保留多人场景的人工验收步骤。
- **产物：** 测试矩阵、失败修复记录；关键状态转换和计分／授权边界的针对性测试。
- **验收：** 选择并记录一组如约 100 ms RTT、1% 丢包的实际模拟条件；对局不永久卡 Loading，伤害不重复，拒绝开火后能恢复正确状态。记录仍可观察到的服务器当前时刻射线判定局限。
- **边界：** 不以一组弱网测试声称覆盖所有网络条件；未修复的问题需标明复现方式与影响。

#### 任务 27：检查四人规模的性能与资源开销

- **目标：** 为小型项目建立有证据的运行成本基线。
- **工作：** 记录测试机器、分辨率、画质、四人运行方式；检查 Game／Render／GPU 时间、网络流量、Actor／组件与 UI 数量。优先修复持续增长、重复 Tick、重复绑定、过量 RPC 和不必要常驻资源，再评估 Lumen、光追、阴影等画质设置。
- **产物：** 简短性能报告与主要问题修复记录。
- **验收：** 在记录的目标机器与设置下评估 1080p／60 FPS 目标；如果达不到，记录实测瓶颈并收敛画质或内容。重复开局后对象数量与内存趋势应稳定，不出现持续累积。
- **边界：** 不为四人项目提前引入整套大规模 ReplicationGraph，也不把硬件未测的帧率写成保证。

#### 任务 28：收敛 Cook 规则并制作可分发包

- **目标：** 让资产扫描与插件引用在编辑器之外同样成立。
- **工作：** 审计地图、Experience、ActionSet、PawnData、GameFeatureData、能力和 GameplayCue 的加载／Cook 链；从前面烟雾检查继续收敛到最终白名单。检查软引用、插件挂载路径、加载错误与丢失原生类；制作 Win64 Development 包，再验证适当的交付配置。
- **产物：** 可启动的打包程序、完整打包命令／步骤与资源审计记录。
- **验收：** 从独立包能进入前端、训练场和竞技场；所有必需功能插件与 Cue 正常；最终加载链无缺失的 LyraGame 类；不依赖编辑器已加载资源才能成功。
- **边界：** 不通过无差别 Cook 全部原版内容来掩盖引用问题；旧内容的物理删除需要另行依据审计结果安排。

#### 任务 29：完成打包后 2–4 人整局验收

- **目标：** 以可交付程序验证首版承诺。
- **工作：** 使用独立进程，优先至少两台同局域网电脑，验证创建／IP 加入、四人规模、晚加入、战斗、结算、再开局和断线；同时检查训练入口和返回流程。记录程序版本、机器、玩家数及日志。
- **产物：** 验收记录、已知问题清单、可演示版本。
- **验收：** 至少完整完成两局竞技对局，其中覆盖达分与超时；血量、弹药、比分、阶段一致；无崩溃、永久 Loading、重复扣血、重复 HUD 或切枪后错误能力。
- **条件说明：** 只有一台电脑时先完成 2–4 独立进程验证，跨机器局域网验证明确记为待补，不能宣称已经测试通过。

#### 任务 30：整理架构说明、交付与下一步扩展入口

- **目标：** 项目以后可以继续扩展，也可以被重新搭建和理解。
- **工作：** 更新启动链、对象职责、插件清单、Tag 表、网络权威边界、资产创建步骤、构建／运行／打包说明；整理 30 次任务完成记录。用现有机制示范“新增一种武器配置”或“新增一个小型 ActionSet”，证明无须大改角色与 GameMode。
- **产物：** `Docs/MiniLyraArchitecture.md`、`Docs/MiniLyraRunbook.md`、最终 `MiniLyraProgress.md` 与本文状态更新。
- **验收：** 按文档可从工程构建并完成双端进入；能够解释一次开火、一次重生、一次地图旅行的完整调用与数据流；已知限制和延期项可查询。
- **完成条件：** 前面所有必需验收有实际结果。未做的跨机器验证或未解决的核心缺陷保持未完成状态，不用“代码写完”替代交付。

## 七、里程碑与每次任务的执行方式

### 7.1 阶段门槛

| 节点 | 必须已经成立 | 后续才能开始的工作 |
| --- | --- | --- |
| 06 完成 | Experience 真正装配功能，失败可退出，退出可回收 | 角色与能力深度接入 |
| 10 完成 | 双端初始化和 InputTag／GAS 链路成立 | 战斗玩法 |
| 15 完成 | 死亡／重生与装备能力回收稳定 | 完整枪械内容 |
| 20 完成 | 可玩的训练场，HUD 与能力由装配得到 | 竞技规则扩展 |
| 25 完成 | 可以连续进行完整对局及进入退出 | 交付收敛 |
| 29 完成 | 打包程序中的多人验收有记录 | 宣布首版完成 |

不要等到任务 26 才测试联机。05 开始双端加载，07 测晚加入，09–10 测复制和输入，13–17 持续测死亡、切枪和服务器权威；06、20 先做小包验证，再在 28 完成最终打包收敛。

### 7.2 每次任务的固定交付格式

1. **本次目标与前置状态：** 哪项能力将从不可用变为可用，依赖是否完成。
2. **实现与资产操作：** 涉及哪些 C++、配置、蓝图、数据资产及编辑器步骤。
3. **验证证据：** 实际运行的构建／PIE／独立进程／打包场景与结果；未执行的明确写出。
4. **清理检查：** 本次新增的委托、能力、组件、输入、Widget、异步句柄由谁释放。
5. **进度记录：** 更新状态、已知问题和下一次入口，保留可恢复版本。

后续可以直接使用这样的任务请求：

> 按 `Docs/MiniLyraRoadmap.md` 执行任务 09。先检查任务 08 的实际状态，只实现本次范围；列出需要在编辑器中创建或修改的资产，完成可执行验证，并更新 `Docs/MiniLyraProgress.md`。

若中途发现前置功能尚未成立，先修复阻塞当前任务的最小问题，再继续；新增武器、地图、账号服务和美术范围进入待办，不挤占原来的框架验收。

## 八、本机源码阅读索引

以下路径相对于 `F:\LyraStarterGame`，用于理解职责与挑选移植内容。不要把整份 `.cpp` 拷贝过来后靠不停增加依赖来“修好编译”。

| 对应任务 | 阅读入口 | 主要看什么 |
| --- | --- | --- |
| 03–04 | `Source/LyraGame/System/LyraAssetManager.cpp`、`System/LyraGameInstance.cpp`、`LyraGameplayTags.cpp` | 启动、资产入口和状态 Tag 注册 |
| 04–06 | `Source/LyraGame/GameModes/LyraExperienceDefinition.h`、`LyraExperienceActionSet.h`、`LyraExperienceManagerComponent.cpp` | 数据结构、加载阶段、插件与 Actions |
| 06–07 | `Source/LyraGame/GameFeatures/GameFeatureAction_WorldActionBase.cpp`、`Plugins/ModularGameplayActors/` | World 上下文、扩展接收者与撤销 |
| 07–10 | `Source/LyraGame/Character/LyraPawnData.h`、`LyraPawnExtensionComponent.cpp`、`LyraHeroComponent.cpp` | Pawn 数据与协作初始化 |
| 09–12 | `Source/LyraGame/Player/LyraPlayerState.cpp`、`Player/LyraPlayerController.cpp`、`AbilitySystem/LyraAbilitySystemComponent.cpp`、`AbilitySystem/LyraAbilitySet.cpp` | ASC 归属、输入处理与授予句柄 |
| 10 | `Source/LyraGame/Input/LyraInputConfig.h`、`Input/LyraInputComponent.h`、`GameFeatures/GameFeatureAction_AddInputBinding.cpp` | InputTag 与输入动作生命周期 |
| 11 | `Source/LyraGame/Camera/LyraCameraComponent.cpp`、`LyraCameraMode.cpp`、`LyraCameraMode_ThirdPerson.cpp` | 模式、混合与相机碰撞 |
| 13 | `Source/LyraGame/AbilitySystem/Attributes/LyraHealthSet.h`、`Character/LyraHealthComponent.cpp` | 属性与角色死亡表现职责分离 |
| 14–15 | `Source/LyraGame/Inventory/`、`Source/LyraGame/Equipment/` | Definition／Instance、复制、QuickBar、撤销能力 |
| 16–18 | `Source/LyraGame/Weapons/LyraGameplayAbility_RangedWeapon.cpp`、`LyraRangedWeaponInstance.cpp`、`Plugins/GameFeatures/ShooterCore/Content/Weapons/Rifle/` | 射击目标数据、装备参数及素材依赖 |
| 19 | `Plugins/CommonGame/Source/Public/PrimaryGameLayout.h`、`GameUIPolicy.h`、`Plugins/UIExtension/Source/Public/UIExtensionSystem.h`、`Source/LyraGame/GameFeatures/GameFeatureAction_AddWidget.cpp` | 本地 UI 层栈、插槽和注册句柄 |
| 21 | `Source/LyraGame/AbilitySystem/Phases/LyraGamePhaseSubsystem.cpp`、`LyraGamePhaseAbility.cpp` | 阶段能力的开始、取消与观察 |
| 28 | `Config/DefaultGame.ini`、`Config/DefaultEngine.ini` | 扫描、Cook、类配置与插件政策 |

本机参考实现也有未完成的分支：Experience 的运行中切换／部分加载取消、完整卸载，以及附加输入移除等位置存在 TODO。Mini 只实现本文承诺的范围，但其正常退出、失败反馈和实际使用的注册回收必须自行验证完整。

## 九、后续扩展顺序

完成 30 次任务之后，优先根据兴趣选择一条支线：小型团队模式 → 简单 Bot → 更好的动画和武器手感 → Session 浏览／Steam 或 EOS → 专用服务器。每条支线都应继续通过 Experience、GameFeature、PawnData、AbilitySet 和 UI 扩展点接入。

本期保留核心框架的判断标准，是新增玩法能复用既有装配与生命周期规则；首版内容数量保持克制，框架承诺通过真实可玩的联机闭环证明。
