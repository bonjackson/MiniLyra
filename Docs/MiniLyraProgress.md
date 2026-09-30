# Mini Lyra 实施进度

更新：2026-10-01（Asia/Shanghai）

## 当前进度

- [x] 任务 01：范围冻结、空项目基线、本地恢复点、Editor 构建、基础场景加载。
- [x] 任务 02：资产审计、49 包迁入 `/Game/Mini`、灰盒训练图及命令行加载验证。
- [x] 任务 03：基础插件、模块、Tag 与日志完成；Editor／Game 编译和运行探针通过。
- [x] 任务 04：三类原生数据资产、AssetManager 与地图 Experience 入口完成；Editor／Game 构建及正反例探针通过。
- [x] 任务 05：GameMode／GameState／Experience 异步状态机完成；Editor／Game 构建及双进程正反例探针通过。
- [x] 任务 06：GameFeature 装配与回收、双端探针、连续三次 PIE、Cook 和独立打包单机烟测通过。
- [x] 任务 07：Modular 角色骨架、PawnData 预注入及 Experience 出生门控完成；双端、晚加入和失败时零出生探针通过。
- [x] 任务 08：PawnExtension／Hero 四态协作骨架完成；三进程两人／晚加入、两种条件可见性顺序和重复通知探针通过。
- [x] 任务 09：PlayerState ASC、AbilitySet 来源与 Avatar 生命周期完成；两人／晚加入、撤销／恢复和重生探针通过。
- [x] 任务 10：Enhanced Input→InputTag→GAS、可撤销本地映射与完整 GameplayReady 门控完成；构建、双进程专项和任务 06–09 回归通过。
- [x] 任务 11–15：第三人称相机与动画、能力 Tag 规则、生命死亡复活、私有库存、装备及两槽 QuickBar；详见各任务文档。
- [x] 任务 16：服务器权威射线步枪、双射线遮挡、GameplayEffect 伤害与联机专项；详见 `Docs/Task16/AuthoritativeRifle.md`。
- [x] 任务 17：独立弹药、服务器装填、步枪连发／手枪单发及装备实例校验；构建、三进程专项、任务 10–16 回归通过，已提交推送 `04887b1`。
- [x] 任务 18：GameplayCue、战斗反馈与武器动画完成；最终 Editor／Game 构建、增强三进程、缺失媒体、有声渲染、资产及 Cue 生命周期专项、任务 11／15／16／17 回归通过，观察者截图已复核。
- [x] 任务 19：GameplayMessage 本地 HUD 桥接、CommonUI 层栈和 UIExtension 注入完成；最终 Editor／Game 统一编译、资产三阶段、默认／媒体三进程与加载失败双端专项、任务 10／15／17／18 回归通过，十张截图已复核。
- [ ] 任务 20–30：下一入口为完整训练 Experience；任务 20 的设计／Loadout 草稿尚未整合到活动代码。

用户已确认第三人称、2–4 人竞技场，并明确允许忽略旧实现、从空项目开始。任务 01 据此重置活动源码和配置，保留旧工程文件作为本地备份；任务 02 在该空基线上建立独立的 Mini 内容入口。之前的 MiniExperience 启动壳不计作已完成框架。

## 任务 01 的实际产物（当时的基线快照）

| 项目 | 结果 |
| --- | --- |
| 工程 | `F:\FPS\FPS\FPS.uproject`，仍使用原工程名／模块名 `FPS` |
| C++ | 两个 Target、`FPS.Build.cs`、模块头／源文件；没有自定义 Gameplay 类 |
| 模块依赖 | Core、CoreUObject、Engine、InputCore |
| 默认类 | Engine.GameInstance、Engine.GameModeBase、Engine.WorldSettings |
| 默认地图 | 当时编辑器与游戏均为 `/Engine/Maps/Templates/Template_Default`；任务 02 已改为 `/Game/Mini/Maps/L_MiniPractice` |
| Content | 任务 01 时仅有 `.gitkeep`；任务 02 已增加 `/Game/Mini` |
| 配置 | 移除对旧 Mini 类、缺失 Experience、CommonUI、EnhancedInput 类型的显式绑定 |
| 构建脚本 | `Scripts/BuildProject.ps1` |
| 地图验证 | `Scripts/VerifyBaseline.ps1`、`Scripts/VerifyBaseline.py` |
| 版本管理 | 本地 Git `main`，Git LFS 已做仓库内初始化；已有 origin 配置，本次不上传 |
| 忽略规则 | Binaries、Intermediate、Saved、DDC、IDE 生成文件、Backups 不进入 Git |
| 大资源规则 | `.uasset`、`.umap`、`.ubulk`、`.uexp` 使用 Git LFS |

## 已冻结的首版范围

Windows／键鼠、第三人称、2–4 人 Listen Server、局域网或直接 IP、FFA、一张训练图和一张竞技图、步枪与手枪、基础 HUD 与结算。保留 Experience、GameFeatures、ModularGameplay、Pawn 初始化、GAS、InputTag、Inventory／Equipment、CameraMode、模块化 UI 和简化 GamePhase。

EOS／Steam 登录与匹配、专用服务器、主机迁移、Bot、复杂背包、更多地图和运行中 Experience 热切换延期。具体取舍沿用 `MiniLyraRoadmap.md`。

## 已核实的工具链

| 工具 | 本次实际版本／位置 |
| --- | --- |
| Unreal Engine | 5.8.2，Changelist 56702186，`++UE5+Release-5.8` |
| 引擎目录 | `C:\Program Files\Epic Games\UE_5.8` |
| Visual Studio | Community 2022，17.14.23 |
| C++ 工具链 | 构建日志报告 14.44.35222；安装目录 `VC\Tools\MSVC\14.44.35207` |
| Windows SDK | 本次构建使用 10.0.22621.0；另已安装 10.0.26100.0 |
| .NET | UE 构建脚本自带 .NET SDK 10.0 win-x64 |
| Git | 2.51.0.windows.2 |
| Git LFS | 3.7.0 |

不需要为本次任务安装额外 C++ 工具链、.NET SDK 或第三方插件。

## 验收证据

### 1. Editor 编译：通过

执行位置：`F:\FPS\FPS`。

```powershell
.\Scripts\BuildProject.ps1
```

脚本使用 `FPSEditor Win64 Development`，将项目文件、构建日志和 UBA 缓存位置显式传给引擎 Build.bat。需要更换引擎位置时使用 `-EngineRoot`。

本次是移出旧 Binaries／Intermediate 后的构建，共执行 7 个动作，结果 `Succeeded`，耗时 57.22 秒。产出新的 `Binaries/Win64/UnrealEditor-FPS.dll`。本次没有借用原 Mini 代码的旧 DLL 证明成功。

- 本地完整日志：`Saved/Logs/Build-FPSEditor-Win64-Development.log`。
- 可随版本保存的证据摘要：`Docs/Task01Verification.md`。

### 2. 基础场景加载：通过

```powershell
.\Scripts\VerifyBaseline.ps1
```

使用 `UnrealEditor-Cmd.exe`、NullRHI 和 Python commandlet，加载引擎内置基础场景，不保存或修改它。脚本同时检查编辑器 World 和 WorldSettings 类型，并要求以下成功标记：

```text
MINI_BASELINE_MAP_LOADED=/Engine/Maps/Templates/Template_Default.Template_Default
MINI_BASELINE_WORLD_SETTINGS=/Script/Engine.WorldSettings
MINI_BASELINE_VERIFICATION_PASSED
```

进程正常退出，脚本返回成功。完整日志：`Saved/Logs/Task01-BaselineMap.log`。

这是**命令行编辑器场景加载验证**，没有进行图形界面视觉验收、PIE、Game target 编译、联网或最终打包；这些不应误记为本次已经完成。任务 01 的空项目构建与安全地图加载门槛已经满足。

### 3. 静态基线与日志检查：通过

当前 Source／Config 没有旧 Mini 类、LyraGame 父类、CommonUser 或缺失 Experience 的活动引用。地图验证没有报告缺失项目类／包的加载错误。日志中的 `LoadErrors: New Page` 是加载日志页面标题，不是失败记录。

日志提示未找到 aqProf／VTune／WinPix 的可选分析器 DLL；它们不影响本次成功结果，无需为了消除这些提示安装分析器。

## 任务 02：Mini 内容入口与灰盒训练图

从独立审计工程筛选 UE 5.8 模板的 Manny 角色网格与基础移动动画、Lyra 的步枪和手枪网格及开火动画。8 个种子资源的硬／软 `/Game` 依赖闭包共 49 包，已迁到 `/Game/Mini/Characters` 和 `/Game/Mini/Weapons`。两把枪的静态网格只审计、未迁入；原版 Experience、PawnData、Hero 和武器能力蓝图留给后续 Mini 实现。具体来源、种子路径及重建清单见 [任务 02 资产取舍](Task02/AssetSelection.md)。

`/Game/Mini/Maps/L_MiniPractice` 已建立约 4000 × 3000 cm 的地面、四周边墙、6 处掩体、4 个出生点、3 组静态训练靶和方向光。`Config/DefaultEngine.ini` 的编辑器与游戏默认地图均指向该图。任务 02 完成时，活动 `Content/Mini` 共 49 个 `.uasset` 和 1 个 `.umap`；任务 04 又加入 3 个数据资产并更新地图的 WorldSettings。旧 Content 仍保存在 `Backups/Task01_PreReset_20260927`，没有作为活动资产整体回迁。

任务 02 当时执行 `Scripts/VerifyTask02.ps1`，UE 5.8 编辑器 commandlet 正常退出并写入 `MINI_TASK02_VERIFICATION_PASSED`。验证实际加载全部 49 个资源包，重开地图，确认当时的 `/Script/Engine.WorldSettings`、地图结构，以及没有 `/Script/LyraGame` 或旧 `/Game` 路径的包依赖。任务 04 已将该地图更新为 `MiniWorldSettings`；任务 02 的回归脚本现兼容两种 WorldSettings，而任务 04 的资产复核严格要求 Mini 类。[任务 02 验证结果](Task02/Verification.json) 是重跑后的当前报告，记录 53 包及 `/Script/FPS.MiniWorldSettings`，本地日志为 `Saved/Logs/Task02-Verification.log`。尚未进行 GUI、PIE、独立程序、打包或联机验证，也未实现可玩的训练模式。

## 任务 03：插件、模块、Tag 与日志基础（完成）

已从本机 Lyra 工程迁入 `ModularGameplayActors`、`GameplayMessageRouter`、`CommonGame`、`CommonUser`、`UIExtension` 的源代码和插件描述，共 84 个文件；`UIExtension.uplugin` 的两个重复 `Plugins` 字段已合并。`FPS.uproject` 启用所需的引擎插件及这五个项目插件。插件来源、传递依赖、FPS 直接模块依赖与后续使用边界见 [任务 03 依赖说明](Task03/PluginDependencies.md)。均来自已安装的 UE 5.8 和本机 Lyra，当前不需要额外下载安装 UE 插件。

新 `UMiniGameInstance` 继承 `UCommonGameInstance`，统一注册 `InitState.Spawned → DataAvailable → DataInitialized → GameplayReady`；四个 Native Tags 与 `LogMiniExperience`、`LogMiniInit`、`LogMiniAbility`、`LogMiniEquipment` 日志类别已建。为满足 CommonGame 的本地玩家生命周期，增加最小具体 `UMiniUIManagerSubsystem`，配置 `CommonLocalPlayer` 和 `CommonGameViewportClient`；为 GameFeatures 增加 `GameFeatureData` 扫描规则。这些是基础依赖配置，不包含实际 UI 或 GameFeature 玩法。

`FPSEditor Win64 Development` 与 `FPS Win64 Development` 均编译成功。执行 `Scripts/VerifyTask03.ps1` 时，未 Cook 的 `UnrealEditor-Cmd.exe -game` 进程退出码为 0；日志确认五个项目插件加载、四态注册、全部四个 Tag 可查询，并收到临时 `AModularPawn` 的 `ReceiverAdded`、`GameActorReady`、`ReceiverRemoved` 事件。日志没有 `Error:` 记录；完整记录在本机 `Saved/Logs/Task03-Verification.log`。该探针不验证独立 Game 程序运行、Cook、正式 Experience、GAS、UI 或联机；后续任务分别验收。

## 任务 04：AssetManager 与数据定义（完成）

`UMiniAssetManager` 取代引擎默认 AssetManager，注册 `MiniExperienceDefinition`、`MiniExperienceActionSet`、`MiniPawnData` 三类原生数据资产实例的扫描项；项目默认 ID 为 `MiniExperienceDefinition:DA_MiniPracticeExperience`。`DA_MiniPracticeExperience` 引用 `DA_MiniPracticePawnData` 和空的 `DA_MiniPracticeActionSet`。任务 04 当时的 PawnData 用引擎 `Pawn` 类作为必填占位；任务 07 已将该资产改指 `BP_MiniCharacter`，现行资产脚本新建时则用原生 `AMiniCharacter` 占位。训练图现使用 `AMiniWorldSettings`，并保存指向该 Experience 的软对象引用。详细路径、配置、验证方法与边界见 [任务 04 资产定义](Task04/AssetDefinitions.md)。

Editor 与 Game target 再次构建成功。在新编辑器进程中，三个数据资产仍是预期原生实例，地图的 `MiniWorldSettings` 和 Experience 覆盖项持久存在；`Scripts/VerifyTask04.ps1` 的未 Cook 编辑器游戏探针按 ID 找到并加载目标资产，验证地图覆盖项相同。未知 ID、空 DefaultPawnData 和空 PawnClass 均按预期返回明确错误，探针退出码为 0。任务 01–03 的回归脚本也通过。三类资产已配置 `AlwaysCook`，但尚未执行 Cook、打包、独立 Game 运行或联机验证；正式 Experience 状态机和 GameFeature 装配分别属于任务 05、06。

## 任务 05：Experience 异步加载与失败状态（完成）

已建立 `MiniGameMode`、`MiniGameState` 和 `MiniExperienceManagerComponent`。服务器下一帧按“地图覆盖 → 项目默认”选 ID，覆盖项非空但无效时明确失败；GameState 组件复制 ID，客户端收到后独立异步加载。组件区分 `Unloaded`、`LoadingAssets`、`LoadingFeatures`、`ExecutingActions`、`Loaded`、`Failed`、`Deactivating`，并提供失败原因、立即或稍后调用的终态订阅、调试状态文本及结束世界时的回调失效处理。任务 05 只允许当前无 GameFeature／Action 的训练 Experience 进入 `Loaded`；配置了这些功能但尚未执行时会报错。GameMode 暂将玩家保留为 spectator，不提前生成默认 Pawn。

`Scripts/VerifyTask05.ps1` 已通过两次真实进程的监听服务器／客户端验收：正常场景确认地图覆盖项、同一复制 ID、两端本地 `Loaded` 和迟订阅；无效 ID 场景确认两端 `Failed` 与明确原因。Editor 和 Game target 均构建成功，任务 03、04 回归脚本再次通过。结束世界时的回调安全依靠弱引用、加载代次和取消顺序做了源码审查；本次没有稳定重现正在加载时结束世界的竞态，因此不声称这项已被动态探针覆盖。实现、日志路径、运行命令和范围界限见 [任务 05 加载流程](Task05/ExperienceFlow.md)。

## 任务 06：GameFeature 激活与 Action 回收（完成）

新增内容插件 `MiniShooterCore` 及其 `GameFeatureData`，使用引擎自带 `UGameFeatureAction_AddComponents` 向 `MiniGameState` 注入测试组件。训练 Experience 自身也通过一个 AddComponents Action 注入另一类组件；Experience 与 ActionSet 都声明同一插件，用来验证按 URL 去重。Manager 现在按本地 World 依次解析／激活必需插件、执行 Actions，完成后才进入 `Loaded`。World 结束或失败时逆序撤销 Experience 自有 Action 和插件 lease；进程级使用者计数保护同进程其他 World。另有缺失必需插件的负例 Experience，要求服务器与客户端分别进入 `Failed`。

Editor／Game target 构建通过；`Scripts/Task06CreateAssets.ps1` 重复运行没有 GameFeatureData 加载错误。`Scripts/VerifyTask06.ps1` 的真实双进程有效与缺失插件场景均通过，两端各只激活一次插件、各注入两种活动数量为 1 的组件；额外的三轮 World travel 均观察到撤销及释放。`Scripts/VerifyTask06PIE.ps1` 又在同一 `UnrealEditor.exe` 进程内完成**真实连续三次 PIE**，每轮 `UEDPIE` World 的两种 marker 各添加和撤销一次，Action 与插件 lease 均回收，无重复注入；证据在 `Saved/Logs/Task06-PIE.log`。最小 Windows 地图 Cook 进程退出码 0，报告 513 个已 Cook 包、0 error／0 warning；用 UE `DumpAssetRegistry` 确认 Cook 产物包含 `/MiniShooterCore/GameFeatureData.GameFeatureData`、训练 Experience 与地图。

完整打包前两次未成功，分别因为 staging 所需 Cook 目录结构不符，以及 IoStore staging 的 Zen 服务连接未就绪。第三次 `RunUAT BuildCookRun` 使用 `-pak -skipiostore -AdditionalCookerOptions=-SkipZenStore`，从 C 盘新目录 Cook／Stage，`Saved/Logs/Task06-PackageLooseConsole.log` 报告 513 包、0 error／0 warning 和 `BUILD SUCCESSFUL`。从 stage 目录直接运行 `FPS.exe`，`Saved/Logs/Task06-PackagedSmoke.log` 确认训练图 Experience `Loaded`、两种 marker 注入和 `MiniFlowProbe PASS`。这验证了**打包程序单机启动**；打包后联机尚未测试，IoStore 的 Zen staging 问题也未解决。实现、日志与测试边界见 [任务 06 装配说明](Task06/FeatureAssembly.md)。

## 任务 07：Modular 角色骨架与出生门控（完成）

`AMiniGameMode` 的默认 PawnClass 为空，玩家出生和重启要等服务端 Experience 进入 `Loaded`。此前到达的 Controller 在加载完成回调中重启，晚加入者走相同的状态判断。GameMode 从 Experience 的 PawnData 选择 `AMiniCharacter` 子类，先给 `AMiniPlayerState` 指定并复制 PawnData，再延迟生成角色、在 `FinishSpawning` 前给 Pawn 注入 PawnData。`AMiniPlayerState`、`AMiniPlayerController`、`AMiniCharacter` 采用 ModularGameplayActors 基类；`UMiniLocalPlayer` 采用 CommonGame 基类并写入引擎配置；最小 `AMiniHUD` 接入扩展 receiver 生命周期。

`BP_MiniCharacter` 继承原生角色，挂 Manny Simple 网格；训练 PawnData 的 PawnClass 已改指该蓝图。`MiniShooterCore` 的 GameFeatureData 新增 Character 目标的 AddComponents Action，用 `UMiniCharacterFeatureMarkerComponent` 观察晚生成角色在各 World 的组件注入。Editor／Game target 构建通过；`Scripts/Task07CreateAssets.ps1` 重复执行后，`Scripts/VerifyTask07Assets.ps1` 在全新进程中核实蓝图父类、最新编译状态、Manny Mesh、PawnData 蓝图类引用及任务 06／07 两个 Action 各一份。新增防护后 `Scripts/VerifyTask07.ps1` 再次通过：独立监听服务器和 ClientA 两人开局后再启动 ClientB，三个进程最终均记录 `PlayerStates=3 Characters=3 ValidCharacters=3 LocalPawn=1 CharacterMarkers=3`。服务端三次 `COMMITTED` 均晚于 Experience `Loaded`；无效 Experience 的服务器与客户端都进入 `Failed`，且没有角色或角色 marker。`VerifyTask06.ps1` 含任务 05 双进程、缺失插件负例和三轮 World 周期的回归也通过。详见 [任务 07 出生门控说明](Task07/SpawnGate.md)。任务 07 完成时尚未加入 PawnExtension／Hero、GAS、输入与相机。

代码审查新增 PawnData 的类约束：`ValidatePawnData` 要求 PawnClass 继承 `AMiniCharacter`，防止误填普通 `APawn` 后 Experience 到达 `Loaded` 却无法生成角色。任务 04 资产脚本现在只为**新建**的 PawnData 使用原生 MiniCharacter 占位，不改动已有资产；`MiniGameInstance` 的负例探针新增普通 `APawn` 被拒绝的检查。补丁后的 Editor／Game 重编译及任务 04 运行／资产验证均通过；旧探针脚本已修复读取尚未写完的空日志时的竞争。本次任务 04／05／06／07 探针运行在未 Cook 的编辑器游戏进程并使用 NullRHI，不能据此认定打包后联机或图形呈现已经验收。

## 任务 08：PawnExtension／Hero 初始化协作（完成）

`AMiniCharacter` 现在持有原生 PawnExtension 和 Hero 默认子组件。两个组件通过 `IGameFrameworkInitStateInterface` 注册独立 Feature，并按 `Spawned → DataAvailable → DataInitialized` 推进；Pawn 与 PlayerState 的 PawnData 必须一致，Authority／AutonomousProxy 还等待 Controller 与 Pawn PlayerState 配对，模拟代理不等待本地 Controller、LocalPlayer 或 InputComponent。PawnData 复制、PlayerState 数据变化、Possess／UnPossess、Pawn 和 PlayerController 上相关的 OnRep，以及输入组件建立均会重新检查。PawnExtension 等 Hero 与其他 Feature 数据就绪，Hero 等 PawnExtension 完成初始化；重复通知不会重复转换。

Editor／Game target 均构建成功；配对条件补丁后两个 target 与 `Scripts/VerifyTask08.ps1` 又重跑通过。三进程验收中，两人就绪后晚加入第三人，三端最终各有 3 个角色且两个组件均到 `DataInitialized`；两个客户端各有 2 个**没有 Controller／InputComponent**的模拟代理达到此状态，三端重复通知后每个角色／Feature／状态仍只转换一次。ClientA／ClientB 分别以 PawnData 优先和 PlayerState 优先的**测试可见性顺序**验证等待与继续；脚本核对真实释放日志及状态转换发生顺序，但不控制真实网络包到达顺序。任务 08 当时的 `GameplayReady` 为 0，ASC 与本地输入随后由任务 09／10 补齐；任务 10 后重跑该探针，三端晚加入稳定快照均为 `GameplayReady=3`。任务 07 出生／失败路径回归，以及 `VerifyTask06.ps1` 所含任务 05 正反例、任务 06 插件负例和三轮 World 周期回归均通过。实现、条件表和验证边界见 [任务 08 初始化协作](Task08/InitStateCoordination.md)。

## 任务 09：PlayerState ASC 与 AbilitySet 生命周期（完成）

`AMiniPlayerState` 持有复制 ASC 和基础 HealthSet，PawnExtension 把当前角色绑定为 Avatar，旧角色只在仍是当前 Avatar 时解绑。PawnData 首次指定时授予一份 AbilitySet；训练 Experience 的 World 作用域 AddAbilities Action 单独授予另一份 AbilitySet，按来源句柄撤销。资产包含两种测试能力、一种无限时长效果和复制的测试属性。晚创建的 PlayerState 在 BeginPlay 后补发就绪事件，避免框架过早的 `GameActorReady` 导致漏授。

Editor／Game target、资产新进程重载、三进程两人／晚加入、功能授予暂停与恢复（含客户端撤销快照）、旧角色到新角色的 ASC 复用和安全解绑均通过。任务 06 三轮真实 World 停用各记录一对功能授予／撤销；任务 08／07 回归通过。服务器第三人加入后的稳定计数是 3 份 PawnData 能力、3 份功能能力／效果／测试属性和 3 个正确 Avatar；每个客户端仅持有自己的一份能力／完整效果，同时看到三人的测试属性和 Avatar。详见 [任务 09 能力生命周期](Task09/AbilityLifecycle.md)。任务 09 当时尚未开放 `GameplayReady`，输入绑定现已在任务 10 完成。

## 任务 10：Enhanced Input、InputTag 与 GAS（完成）

已加入 `UMiniInputConfig`、`UMiniInputComponent` 和七个原生 InputTag；训练资产提供移动、视角、跳跃、开火、装填、切枪、瞄准七个 InputAction 及一个 IMC，PawnData 指向配置，Experience 的 World 作用域 AddInput Action 管理本地映射。Hero 把原生输入接到角色，把能力输入 Tag 交给 PlayerState ASC；Controller 在输入处理阶段推进能力按下、保持和释放。角色更换、菜单输入接管及 Action 停用均有撤销映射和句柄的路径；`GameplayReady` 现在按 ASC Avatar 与本地输入映射条件门控。当前 Fire 仍为测试能力，菜单仅有输入门控接口，尚未实现枪械或界面。详见 [任务 10 输入链路](Task10/InputPipeline.md)。

Editor／Game target 构建、资产创建及新进程重载通过。两个独立的未 Cook 进程已验证本地移动、四个角色周期的 Fire 按住／释放、三次服务端重生、每轮单份映射和 16 个句柄、模拟代理零绑定，以及菜单／Action 测试暂停和恢复。客户端最终记录 `PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1`。任务 08／09／07／06 回归通过；任务 08 三端第三人晚加入后各有 3 个角色到达 `GameplayReady`，任务 06 三次真实 World 停用各记录输入解绑及 Action 停用。该专项未验证实际菜单、枪械、图形画面或打包后联机。

## 任务 17：弹药、装填与手枪（完成）

两把武器各自的私有库存物品保存弹匣和备用弹药，装备定义提供容量、伤害、射速和装填时长。步枪支持按住连发，手枪一次按键只发一发；共用服务器射线和装填链路。服务器装填能力用 `State.Reloading` 阻断射击，完成时重新核对 Pawn、来源装备和物品，切枪、死亡或能力撤销时清理计时器与效果。每次射击 RPC 同时验证物品 GUID 和当前装备实例，防止切枪或重新装备后旧会话请求影响当前武器。空仓通知按耗尽周期去重，并停止拥有者的步枪持续开火。

Editor／Game Win64 Development 构建、`Scripts/VerifyTask17.ps1` 三进程专项及任务 10–16 联机回归全部通过。专项覆盖满弹匣、空仓、部分／零备用弹药、装填中射击阻断、切枪／死亡取消、步枪／手枪命中、手枪长按单发、旧请求拒绝和拥有者私有弹药一致。提交 `04887b1` 已推送到远端 `main`。详见 [任务 17 文档](Task17/AmmoReloadPistol.md)。

## 任务 18：GameplayCue、动画与战斗反馈（完成）

Pawn 上新增 `UMiniCombatFeedbackComponent`，经角色 `IGameplayCueInterface` 接收 ASC Cue。拥有者即时开火以 ShotSequence 去重服务器回声，观察者只播放服务器接受的射击；服务器命中、受伤和死亡分别触发 Cue。可靠 owner-only 命中通知暴露明确的 `OnHitConfirmed(ShotSequence, AppliedDamage, bKilled)` 事件，为任务 19 的准星 HUD 提供真实确认来源。六个独立 SoundWave、四个武器 Fire／Reload Montage 和对应 Mini 武器 AnimBP 已接入，角色原移动图保留并加入 DefaultSlot；死亡使用简单网格侧倾姿态。

装填使用可复制的活动 Cue，携带来源装备并随能力结束撤销；处理 Cue 先到、装备先到及来源对象延迟映射，避免旧装备动画和同帧取消留下孤立效果。`MiniShooterCore` 的 Action 按进程路径引用计数注册／撤销 `/MiniShooterCore/GameplayCues`；独立 AssetProbe Cue 验证真实插件扫描，不叠加实战特效。

最终 Editor／Game Win64 Development 构建、Cue／Animation 资产幂等创建及新进程验证、CuePath 激活／撤销专项、增强 `VerifyTask18.ps1` 默认和 `-NoMediaAssets` 三进程专项均通过。增强专项核对拥有者即时开火一次且回声抑制一次、观察者开火一次、可靠命中确认、撞击／受伤、切枪取消、同帧快速取消无孤立 Cue、重新装填恢复及死亡停止。清空媒体引用后两端声音／动画播放计数为零，但服务器仍结算 25 伤害并正确死亡。

`-WithMedia` 重跑通过渲染与成功播放断言，生成四张 Reload／Death 截图，并导出两端实际 AudioMixer master mix WAV；一轮录音为双声道 48 kHz、约 2.3 秒、峰值 32763，两端 RMS 约 4220／4261，确认真实非静音音频输出。补光后的观察者截图已复核，枪械和死亡姿态清晰；证据保存为 [ObserverReload.png](Task18/ObserverReload.png) 与 [ObserverDeath.png](Task18/ObserverDeath.png)，黑背景来自高空隔离探针，不代表训练图效果。任务 11／15／16／17 回归全部通过；任务 16 旧探针改用 MOVE_Flying 保留朝向复制，并在相机校正后等待 0.45 秒再输入，正式服务器视角校验保持原样。任务 18 已完成，任务 19 的 HUD 接入现已完成，见下方记录。资产路径、生命周期、命令和实际结果见 [任务 18 文档](Task18/CombatFeedback.md)。

## 任务 19：本地消息、CommonUI 与模块化 HUD（完成）

CommonGame UI Policy 为每个 LocalPlayer 建立 Game／Menu／Modal 根层栈；MiniShooterCore 的 AddWidgets Action 按 World／上下文异步注入一个 HUDLayout 和四个 UIExtension 元素，并在 HUD、根布局、世界或功能撤销时清理自己的贡献。ViewModel 从当前 ASC、库存和装备先取快照再发 GameplayMessage 本地通知，处理迟到复制及重生重绑定；命中准星只使用服务器 owner-only 确认。菜单和基础加载／失败界面采用按来源登记的输入门控，停止监听不依赖 Widget GC。

2026-10-01 最终 Editor／Game Development 均以 `-DisableAdaptiveUnity` 统一编译通过；资产 Create／CreateAgain／Verify、补强后的默认与媒体三进程、有效／未知 Experience 的加载双端默认与媒体专项全部 PASS。主机权威刷新、晚创建 HUD、菜单输入恢复、真实根布局释放／重建、重生、真实插件撤销和撤销后重建根布局仍零 HUD／Action／门控均验证；六张战斗 UI 图与四张加载图已复核。任务 10／15／17 回归和任务 18 等待条件括号修复后的重跑均通过。新增 Probe helper 使用任务前缀避免 unity 合并重名，正式射击／伤害校验保持不变。

最终代表截图保存在 `Docs/Task19/Evidence`，实现、日志与命令见 [任务 19 文档](Task19/ModularHUD.md)。训练灰盒仍有光照未重建提示，真实可伤害训练靶、数据 Loadout／共享 ActionSet 和新的打包烟测留给任务 20；比分／时间保持占位。任务 19 单项提交推送由本次收尾执行，版本标识以 Git 为准。

## 插件状态与后续安排

| 插件 | 来源 | 当前状态 | 后续安排 |
| --- | --- | --- | --- |
| ModelingToolsEditorMode | 引擎自带 | 保留模板原有的 Editor-only 启用项 | 可用于灰盒；不是运行时框架依赖 |
| PythonScriptPlugin | 引擎自带 | 仅在任务 02 验证命令中临时启用 | 没写入项目长期插件列表，不需要下载 |
| ControlRig | 引擎自带 | 任务 02 迁入资源的软引用涉及 ControlRig；命令行资源加载已通过 | 实际使用角色动画图时核查启用与运行时配置 |
| AndroidFileServer | 引擎自带 | 显式禁用 | 首版只做 Windows，避免启动时写入无关 Android 文件服务配置 |
| GameplayAbilities（GAS） | 引擎自带 | 任务 09 已接入 PlayerState ASC、AbilitySet 与效果／属性复制；任务 10 的输入处理已通过双进程专项验收 | 后续实现正式战斗能力 |
| GameFeatures、ModularGameplay | 引擎自带 | 任务 06 已用于 `MiniShooterCore` 激活与 AddComponents 注入 | 后续 Action 类型沿用本次的 World 作用域和回收路径 |
| EnhancedInput | 引擎自带 | 任务 03 已启用；任务 10 输入资产、重生及撤销专项验收通过 | 后续配合相机、装备和菜单接入 |
| CommonUI／CommonInput | 引擎自带 | 任务 19 层栈、菜单与输入回收已验证 | CommonInput 是模块；产品前端留到任务 24 |
| ModularGameplayActors、GameplayMessageRouter | 本机 Lyra 的 `Plugins` | 任务 19 已接入 CommonPlayerController 生命周期和本地 HUD 消息 | 消息只做本地通知，不替代网络复制 |
| CommonGame、CommonUser、UIExtension | 本机 Lyra 的 `Plugins` | 任务 19 UI Policy、LocalPlayer 插槽与功能撤销已验证 | CommonUser 仍作依赖，首版不接平台登录 |

这里“不需要安装”指不用额外下载／购买；引擎插件启用、Lyra 项目插件迁入及 Build.cs／`.uproject` 配置已在任务 03 完成。CommonGame 的 CommonUser／OnlineFramework 传递依赖仍保留，即使首版不做平台登录。

## 本次问题与处理

1. **UE 5.8 的 `-NoUBA` 不会移除整个 UBA 执行器。** 本机引擎源码显示它只关闭 detouring，仍然创建 UBA 存储。首次构建尝试写 `C:\ProgramData\Epic\UnrealBuildAccelerator` 时被沙箱拒绝，产生重复错误日志并耗尽 F 盘剩余空间。已停止本次构建进程，清空其生成的两份异常日志，恢复空间；没有删除用户资产和备份。脚本现固定 `-UBARootDir=<项目>\Saved\BuildCache\UBA`，修正后实际编译成功。
2. **环境沙箱限制。** 曾出现辅助进程启动失败；重新在沙箱内运行场景校验时，Zen/DDC 本机缓存服务也因不可写而退出。最终配置已通过获准的本机执行重新验证成功，完整日志包含成功标记；这些限制不表示缺少 UE 插件。
3. **初次 Git 初始化中断且归属沙箱账户。** 将没有提交的临时 Git 元数据保存在 `Backups/Task01_SandboxGit`，以本机用户重新初始化，正常 Git 操作已恢复；没有扩大全局 safe.directory 配置。
4. **路线变化。** 旧启动链现在属于历史资料。已在 `MiniExperienceBootstrap.md` 中标注，并调整路线图：02 先用引擎 WorldSettings，03–05 再从零创建正式 Mini 核心类。

## 恢复点与注意事项

重置前文件位于：`F:\FPS\FPS\Backups\Task01_PreReset_20260927`。

其中保存旧 Source、Config、Content、Binaries、Intermediate、Saved、原 FPS.uproject、.vsconfig 和当时的 Docs；Source 的 SHA256 清单为 `SourceHashes.json`。移动在同一工程目录内完成，没有删除旧内容。约 1.9 GB 的原 Content 完整留在该备份中。本次素材主要从本机 Lyra 和 UE 模板审计后迁入。

Backups 被 Git 忽略，是本机恢复点，**不会随 clone 自动取得**。如要还原旧工程，先关闭 Unreal Editor，另存当前新基线，再从备份恢复对应 Source／Config／Content 和 FPS.uproject；旧生成目录通常不必恢复，可重新编译生成。不要把旧 Source 与新 Source 直接混在一起。

新空项目的恢复点使用 Git 标签 `task01-baseline`。查看记录：

```powershell
git log -1 --oneline
git show task01-baseline --stat
git status --short
```

协作机器应装有 Git LFS，首次取得仓库后执行 `git lfs install`、`git lfs pull`。任务 01 最终检查时仓库已有 `origin=https://github.com/bonjackson/MiniLyra.git` 配置；那次未访问或推送远程，也未核验远程 LFS 服务。任务 02 的版本提交与远程状态以 Git 记录为准。

## 下一次入口

任务 01–19 的实现与验收已完成；本次收尾单独提交推送任务 19，随后完成任务 20 的可玩训练 Experience：真实训练靶、数据 Loadout／共享 ActionSet、训练补给与光照，以及新的打包烟测。任务 16 的服务器回溯／竞技级延迟补偿仍属延期项；任务 06 的旧包不代表当前训练模式已完成打包联机验收。
