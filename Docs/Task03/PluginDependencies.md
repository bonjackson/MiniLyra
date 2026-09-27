# 任务 03：插件来源与依赖

更新：2026-09-27（Asia/Shanghai）

## 项目插件

从本机 `F:\LyraStarterGame\Plugins` 迁入下列五个插件。每个插件仅复制 `.uplugin` 和 `Source`；`CommonUser` 另复制 `Resources`。未复制原工程的 `Binaries`、`Intermediate`、`Saved` 或其他插件。

| 项目插件 | 文件数 | 模块及类型 | `.uplugin` 声明的插件依赖 |
| --- | ---: | --- | --- |
| `ModularGameplayActors` | 17 | `ModularGameplayActors`（Runtime） | `ModularGameplay` |
| `GameplayMessageRouter` | 12 | `GameplayMessageRuntime`（Runtime）、`GameplayMessageNodes`（UncookedOnly） | `GameplayTagsEditor` |
| `CommonGame` | 31 | `CommonGame`（Runtime） | `CommonUI`、`CommonUser`、`ModularGameplayActors`、`OnlineFramework` |
| `CommonUser` | 15 | `CommonUser`（Runtime） | `OnlineSubsystem`、`OnlineSubsystemUtils`、`OnlineServices` |
| `UIExtension` | 9 | `UIExtension`（Runtime） | `CommonUI`、`CommonGame` |

共迁入 84 个文件。除按下文修复的 `UIExtension.uplugin` 外，其余 83 个文件逐一与 Lyra 来源计算 SHA256，结果全部相同。五份 `.uplugin` 均能解析为 JSON，且插件目录内没有 `LyraGame` 文本引用。

原版 `UIExtension.uplugin` 在顶层重复声明了两个 `Plugins` 字段，分别列出 `CommonUI` 和 `CommonGame`。迁入副本把它们合并为同一个数组，避免 JSON 解析器只保留后一个字段时漏掉 `CommonUI`。

## 引擎插件与传递依赖

项目在 `FPS.uproject` 中显式启用 UE 5.8 自带的 `GameplayAbilities`、`GameFeatures`、`ModularGameplay`、`EnhancedInput`、`CommonUI` 和 `OnlineFramework`，并显式启用上表五个项目插件。这些插件均位于本机 UE 5.8 或 Lyra 源工程，无需另行下载安装。

`CommonInput` 是 `CommonUI.uplugin` 提供的 Runtime **模块**，不是独立的 `.uplugin`；在 Build.cs 需要使用该模块时写 `CommonInput`，在 `.uproject` 中只启用 `CommonUI`。`GameplayMessageRouter` 的运行时模块名为 `GameplayMessageRuntime`，蓝图节点模块 `GameplayMessageNodes` 仅用于未 Cook 的目标。

引擎插件还会按其描述文件启用传递依赖：`GameFeatures` 依赖 `ModularGameplay`、`DataRegistry` 和 `AssetReferenceRestrictions`；`GameplayAbilities` 依赖 `Niagara`、`DataRegistry` 等；`CommonUI` 依赖 `EnhancedInput` 等；`OnlineFramework` 依赖 `OnlineFrameworkCommon`、`OnlineSubsystem`、`OnlineSubsystemUtils` 和 `OnlineServicesOSSAdapter`。`CommonGame` 对 `CommonUser` 和 `OnlineFramework` 的依赖保持原样，即使首版暂不实现平台登录。

首版只为本地和局域网的 2–4 人玩法预留在线框架，不启用 EOS 或 Steam 服务插件。启用 `OnlineFramework` 及其适配模块不等于配置外部登录后端。

## FPS 模块与基类选择

`FPS.Build.cs` 目前公开依赖 `CommonGame`、`GameplayTags`，私有依赖 `ModularGameplay`、`ModularGameplayActors`。`Core`、`CoreUObject`、`Engine`、`InputCore` 是项目原有基础依赖。其他框架插件在 `.uproject` 中启用，但 FPS 模块尚未调用其 API；在对应任务首次使用时再增加直接模块依赖，避免把“插件已载入”误认为“系统已实现”。

`UMiniGameInstance` 继承 `UCommonGameInstance`，因此 CommonGame 是直接 C++ 依赖。它通过 `UGameFrameworkComponentManager` 注册初始化状态。扩展事件的最小运行探针生成 `AModularPawn`，由 `ModularGameplayActors` 提供接收者基类。当前默认 GameMode 与 WorldSettings 仍是引擎基类；正式模块化角色、GameState 和 Experience 管理器依路线图在任务 04–08 实现。CommonUser／OnlineFramework 只满足 CommonGame 的传递依赖，不表示已经提供登录或局域网会话流程。

`UCommonGameInstance::AddLocalPlayer` 会通知 `UGameUIManagerSubsystem`，而 CommonGame 提供的该类是抽象类。因此本任务增加最小具体子类 `UMiniUIManagerSubsystem`，并在 `Config/DefaultEngine.ini` 配置 `CommonLocalPlayer` 与 `CommonGameViewportClient`。这只保证 CommonGame／CommonUI 的基础对象可以创建；具体 UI 策略、层栈和 HUD 在任务 19 实现。`Config/DefaultGame.ini` 还注册 `GameFeatureData` 的扫描规则，满足 GameFeatures 启动时的资产管理要求；当前 `/Game/Unused` 下没有功能资产，任务 06 才开始制作并激活真正的 GameFeature 插件。

任务 01 归档的旧 `MiniGameInstance` 曾通过反射查找 `CommonSessionSubsystem.OnPreClientTravelEvent`，并在客户端跳转前记录 URL、旅行类型和一组固定加密测试向量的标签及长度。它用于早期会话／加密实验，不参与本版四态初始化或竞技玩法；新实例没有迁入这段反射绑定及演示数据。若后续会话流程确需旅行通知，在对应联机任务中使用明确的类型依赖重新实现。

## Native Tags 与日志

四个 Tag 由 `Source/FPS/System/MiniGameplayTags.cpp` 原生定义，在 `UMiniGameInstance::Init` 中按顺序注册给 `UGameFrameworkComponentManager`。后续 Pawn 初始化组件应沿用同一命名和顺序，不再各自声明一组相似状态。

| C++ 符号 | Tag 名 | 状态含义 |
| --- | --- | --- |
| `MiniGameplayTags::InitState_Spawned` | `InitState.Spawned` | Actor／组件已创建，可接收扩展 |
| `MiniGameplayTags::InitState_DataAvailable` | `InitState.DataAvailable` | 所需配置和关联对象可用 |
| `MiniGameplayTags::InitState_DataInitialized` | `InitState.DataInitialized` | 数据与组件绑定完成 |
| `MiniGameplayTags::InitState_GameplayReady` | `InitState.GameplayReady` | 可以参与玩法 |

`Source/FPS/System/MiniLogChannels.cpp` 定义四个专用日志类别：`LogMiniExperience` 供 Experience 选择与加载、`LogMiniInit` 供初始化和扩展接收者、`LogMiniAbility` 供 GAS 能力、`LogMiniEquipment` 供装备生命周期。后两类本任务只建立分类，不代表对应系统已实现。

## 可重复验证

先从项目根目录构建两个 target，再运行无渲染的编辑器游戏进程探针：

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor
.\Scripts\BuildProject.ps1 -Target FPS
.\Scripts\VerifyTask03.ps1
```

验证脚本使用本机 UE 5.8 的 `UnrealEditor-Cmd.exe -game` 加载默认训练图，传入 `-MiniProbeReceivers`。该开关只用于本任务的短时检查：查询四个原生 Tag，生成并销毁一个临时 `AModularPawn`，检查 `ReceiverAdded`、`GameActorReady`、`ReceiverRemoved` 事件，然后以结果码退出。脚本要求进程退出码为 0、日志含初始化顺序和两个 PASS 标记，并核对五个项目插件均有加载记录。完整日志写入 `Saved/Logs/Task03-Verification.log`；可用 `-EngineRoot` 指向其他 UE 5.8 安装目录。

此探针只证明依赖加载、Tag 查询和最小扩展事件链，不验证 GameFeature 激活与撤销、正式 Pawn 的四态推进、CommonUI 输入路由、联机或 Cook；它们分别在后续任务验收。

Editor 与 Game target 已在本机编译成功。`Scripts/VerifyTask03.ps1` 已复验通过：进程退出码 0，日志包含四态注册、两个 PASS 标记与五个项目插件的 Mounting 记录，未发现 GameFeatures、CommonUI 或 CommonGame 加载错误。该结果是未 Cook 的编辑器游戏进程验证，不代表独立 Game 可执行文件或打包程序已完成运行时测试。
