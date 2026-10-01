# Mini Experience Bootstrap

> 历史文档：2026-09-27 执行任务 01 时，用户要求从空项目开始，下方归档章节涉及的旧实现已经归档至 `Backups/Task01_PreReset_20260927`，不在当前 Source/Config 中启用。请以 `MiniLyraProgress.md` 和 `MiniLyraRoadmap.md` 的任务顺序为准。

## 当前启动链（更新至 2026-10-01）

任务 01–20 已完成实现与验收。当前链路为权威旅行选项／地图／项目配置选择真实 Experience ID → GameState 复制 ID → 每端独立加载 Experience、PawnData、ActionSets 和 GameFeatures → 本地 Loaded → 服务器出生、PawnExtension／Hero 初始化、PlayerState ASC 绑定当前 Avatar → 数据 Loadout、输入与装备能力。下方旧实现的猜测 ID 兜底、网络加密测试和 CommonSession 反射绑定均不属于活动链路。

任务 19 已增加 CommonGame UI Policy 和每 LocalPlayer 的 Game／Menu／Modal 根层栈；MiniShooterCore 经 AddWidgets Action 和 UIExtension 注入战斗 HUD。HUD 从已复制属性／装备取初始快照，GameplayMessage 只发本地通知；根布局的加载／失败界面在玩法 HUD 未创建或撤销后仍可存在。菜单、HUD 监听与异步句柄按对应生命周期回收，主机与客户端独立显示自己的数据。

任务 20 已完成共享 Combat／Practice ActionSet、PawnData Loadout、真实训练靶和服务器弹药补给，地图使用动态光照。Editor／Game 构建、18 阶段资产验证、三进程与配置／旅行专项及新 Development 包的对应运行检查通过，编辑器／包共六张训练图已复核；见 [任务 20 文档](Task20/PracticeExperience.md)。单独停用 Core 撤销 HUD／Cue，Experience 所有的训练 Actor 在退出 World 时撤销。下一入口是任务 21 的简化 GamePhase 与 MiniArena；比分／时间仍为训练占位，生产 cook 与 Cue 路径提示留任务 28 收敛。

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
