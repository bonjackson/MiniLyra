# Mini Experience Bootstrap

> 历史文档：2026-09-27 执行任务 01 时，用户要求从空项目开始，下方归档章节涉及的旧实现已经归档至 `Backups/Task01_PreReset_20260927`，不在当前 Source/Config 中启用。请以 `MiniLyraProgress.md` 和 `MiniLyraRoadmap.md` 的任务顺序为准。

## 当前启动链（更新至 2026-10-04）

任务 01–25 已完成实现与验收，提交／推送版本以 Git 为准。程序默认进入 `L_MiniFrontEnd`，地图与项目回退均选 `DA_MiniFrontEndExperience`。当前链路为权威旅行选项／地图／项目配置选择真实 Experience ID → GameState 复制 ID → 每端独立加载 Experience、PawnData、ActionSets 和 GameFeatures → 本地 Loaded → 服务器按 Experience 是否为前端决定出生。玩法 World 随后执行 PawnExtension／Hero 初始化、PlayerState ASC 绑定当前 Avatar → 数据 Loadout、输入与装备能力。下方旧实现的猜测 ID 兜底、网络加密测试和 CommonSession 反射绑定均不属于活动链路。

任务 19 已增加 CommonGame UI Policy 和每 LocalPlayer 的 Game／Menu／Modal 根层栈；MiniShooterCore 经 AddWidgets Action 和 UIExtension 注入战斗 HUD。HUD 从已复制属性／装备取初始快照，GameplayMessage 只发本地通知；根布局的加载／失败界面在玩法 HUD 未创建或撤销后仍可存在。菜单、HUD 监听与异步句柄按对应生命周期回收，主机与客户端独立显示自己的数据。

任务 20 完成训练内容，任务 21 完成独立 GameState 阶段 ASC／MiniArena 注入，任务 22 建立生产 FFA。任务 23 增加独立竞技图与地图 Experience 覆盖，八个安全出生点、真实环境 GE 跌落／同生命返回及全堵恢复工作接入既有死亡重生链；修复新 Avatar 等待旧死亡 Tag 解除时的输入重试事件。最终构建、资产三阶段、普通无探针四进程、两局真实 GE 与第三局清分、训练及输入／HUD／撤销回归、媒体复核均通过；见 [任务 20](Task20/PracticeExperience.md)、[任务 21](Task21/GamePhases.md)、[任务 22](Task22/FFAMatchRules.md) 与 [任务 23](Task23/ArenaMap.md)。

任务 24 的前端 Experience 显式 `bIsFrontEnd`，没有 PawnData、战斗 GameFeature 或 ActionSet，唯一 AddWidgets 在 Menu 层注入前端。正常 PlayerState 与默认 ASC／HealthSet 仍存在，但没有战斗 Avatar 与能力授予。训练按钮进入 Practice，创建按钮进入 Arena Listen Server，加入按钮使用 IPv4[:port] 发起客户端旅行；前端不需要账号登录。`MiniGameSession` 在 PreLogin／Login 共同批准最多四人，死亡和临时无 Pawn 仍占位。

所有地图切换采用普通旅行，`bUseSeamlessTravel=false`；新 World 重建 PlayerState／ASC 和对局统计。GameInstance 的 `MiniTravelSubsystem` 仅持久保存请求与失败提示，按 World／请求 generation／实际 Driver 归属过滤回调。主机返回通过可靠通知保存 `MINI_HOST_LEFT` 后沿引擎返回链恢复客户端前端；创建驱动失败／绑定失败以及真实连接失败都显示可关闭的中文 Modal。游戏中退出先完成前端旅行，再正常退出进程。最终七场景媒体专项与三个实际退出进程通过，见 [任务 24](Task24/FrontEndTravel.md)。生产 cook 与 Cue 路径提示留任务 28 收敛。

任务25确认同World重生保留PlayerState ASC并替换Pawn／life／物品GUID，普通旅行创建新PS／ASC；Loaded迟订阅立即一次，重复初始化不叠加能力／输入／HUD绑定。死亡仅停止尸体Movement Tick与owning客户端预测，活paused recovery仍由真实Tick恢复。默认及最终媒体六模式、四人40次死亡／重生、三完整往返、退出重连及真主机丢失、四类待恢复工作回收和08／09／10／23回归通过；旧活动能力、Avatar、输入及UI监听归零。详见[任务25](Task25/Lifecycle.md)。任务26补齐必需Action软类屏障、authority场景Ready与真实UI挂载观察、Loaded后权威失败复制、60秒期限及坏前端退出；Dedicated跳过本地Travel／UI。19故障、四角色、三端失败与正常／弱网RPC及既有回归通过，见[任务26](Task26/FaultNetworkRegression.md)。下一入口为任务27性能与资源。

## 归档实现的目标

为 FPS 项目搭建一套最小可演示的 Experience 启动链路，用于后续接入地图 Experience、项目默认 Experience、Game Feature 加载、网络加密测试数据和初始化状态协作。

## 扩充后的需求

- 项目需要具备一套 Lyra 风格但更轻量的 Experience 入口。
- 初始化状态 Tag 由 `GameInstance` 统一注册，避免每个组件各自定义状态顺序。
- Experience 的来源先从地图读取，再回退到项目默认配置，便于单张地图覆盖默认玩法。
- GameState 负责承载 Experience Manager，并将当前 Experience ID 复制到客户端。
- CommonSession 的客户端 Travel 前事件用于网络切图前的演示逻辑，例如输出或准备加密测试数据。
- 在 CommonUser 插件尚未接入时，工程仍应保持可编译；插件启用后再自动绑定可见的 `OnPreClientTravelEvent`。

## 新增类

- `UMiniGameInstance`
  - 继承 `UGameInstance`。
  - 在 `Init` 中注册 Gameplay 初始化状态 Tag：`InitState.Spawned`、`InitState.DataAvailable`、`InitState.DataInitialized`、`InitState.GameplayReady`。
  - 将上述 Tag 按顺序注册到 `UGameFrameworkComponentManager`，供后续组件通过 ModularGameplay 初始化状态系统同步。
  - 准备用于演示的网络加密测试数据，包括测试 Key、Nonce 和标签字符串。
  - 尝试绑定 `UCommonSessionSubsystem` 的 `OnPreClientTravelEvent`。当前工程未直接依赖 CommonUser，因此这里使用反射方式进行可选绑定：插件存在且事件可见时自动绑定，否则静默跳过。

- `AMiniWorldSettings`
  - 继承 `AWorldSettings`。
  - 提供地图级 `DefaultGameplayExperience` 软对象配置。
  - 在 `GetDefaultGameplayExperience` 中将配置的 Experience 资源转换为 `FPrimaryAssetId`。
  - 优先通过 `UAssetManager::GetPrimaryAssetIdForPath` 查询真实 Primary Asset ID；若资源尚未配置 Primary Asset Type，则回退为 `MiniExperienceDefinition:<AssetName>`，方便后续接入资产扫描规则。

- `AMiniGameMode`
  - 继承 `AGameModeBase`。
  - 构造函数中将项目默认 `GameStateClass` 指向 `AMiniGameState`。
  - 在 `InitGame` 中延迟到下一帧调用 `HandleMatchAssignmentIfNotExpectingOne`，避免 WorldSettings/GameState 初始化时序过早。
  - Experience 选择优先级：
    1. 地图默认 Experience，即 `AMiniWorldSettings::GetDefaultGameplayExperience`。
    2. 项目默认 Experience，即 `AMiniGameMode::DefaultGameplayExperience` 配置项。
  - 找到合法 `FPrimaryAssetId` 后，将其写入 GameState 上的 `UMiniExperienceManagerComponent`。

- `AMiniGameState`
  - 继承 `AGameStateBase`。
  - 构造时创建并挂载 `UMiniExperienceManagerComponent`。

- `UMiniExperienceManagerComponent`
  - 继承 `UGameStateComponent`。
  - 保存当前 `CurrentExperienceId`。
  - 支持复制 `CurrentExperienceId` 到客户端。
  - 提供 `OnCurrentExperienceChanged` 事件，方便 UI、加载器或调试逻辑监听 Experience 变化。

## 配置

构建依赖写入 `Source/FPS/FPS.Build.cs`：

- `GameplayTags`
- `ModularGameplay`

插件依赖写入 `FPS.uproject`：

- `ModularGameplay`

项目默认类写入 `Config/DefaultEngine.ini`：

- `GameInstanceClass=/Script/FPS.MiniGameInstance`
- `GlobalDefaultGameMode=/Script/FPS.MiniGameMode`
- `WorldSettingsClassName=/Script/FPS.MiniWorldSettings`

项目默认 Experience 写入 `Config/DefaultGame.ini` 的 `[/Script/FPS.MiniGameMode]`：

- `DefaultGameplayExperience=(PrimaryAssetType="MiniExperienceDefinition",PrimaryAssetName="B_DefaultMiniExperience")`

后续创建真实 Experience DataAsset 后，可以将地图 `World Settings` 面板里的 `Default Gameplay Experience` 指向该资源；也可以继续使用项目默认值作为兜底。

## 后续扩展建议

- 新增 `UMiniExperienceDefinition : UPrimaryDataAsset`，把 Experience 的 GameFeature、PawnData、输入配置、HUD 配置等放入资产。
- 在 Asset Manager 中配置 `MiniExperienceDefinition` 的扫描路径，让 `UAssetManager` 能返回真实 Primary Asset ID。
- 在 `UMiniExperienceManagerComponent` 中根据 `CurrentExperienceId` 异步加载 Experience 资产，并在加载完成后推进初始化状态。
- CommonUser 插件接入后，可将 `UMiniGameInstance` 的反射式绑定替换为强类型 `UCommonSessionSubsystem` include 和委托绑定。

## 验收标准

- `FPSEditor Win64 Development` 能通过编译。
- 项目启动时使用 `UMiniGameInstance`。
- 默认 GameMode 使用 `AMiniGameMode`。
- 默认 WorldSettings 使用 `AMiniWorldSettings`。
- `AMiniGameMode::InitGame` 后下一帧执行 Experience 选择。
- 有地图默认 Experience 时优先使用地图配置；没有时使用项目默认 Experience。
- 合法 Experience ID 会写入 `UMiniExperienceManagerComponent::CurrentExperienceId` 并复制到客户端。
