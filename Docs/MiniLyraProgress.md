# Mini Lyra 实施进度

更新：2026-09-28（Asia/Shanghai）

## 当前进度

- [x] 任务 01：范围冻结、空项目基线、本地恢复点、Editor 构建、基础场景加载。
- [x] 任务 02：资产审计、49 包迁入 `/Game/Mini`、灰盒训练图及命令行加载验证。
- [x] 任务 03：基础插件、模块、Tag 与日志完成；Editor／Game 编译和运行探针通过。
- [x] 任务 04：三类原生数据资产、AssetManager 与地图 Experience 入口完成；Editor／Game 构建及正反例探针通过。
- [x] 任务 05：GameMode／GameState／Experience 异步状态机完成；Editor／Game 构建及双进程正反例探针通过。
- [x] 任务 06：GameFeature 装配与回收、双端探针、连续三次 PIE、Cook 和独立打包单机烟测通过。
- [x] 任务 07：Modular 角色骨架、PawnData 预注入及 Experience 出生门控完成；双端、晚加入和失败时零出生探针通过。
- [ ] 任务 08–30：尚未实施。

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

`BP_MiniCharacter` 继承原生角色，挂 Manny Simple 网格；训练 PawnData 的 PawnClass 已改指该蓝图。`MiniShooterCore` 的 GameFeatureData 新增 Character 目标的 AddComponents Action，用 `UMiniCharacterFeatureMarkerComponent` 观察晚生成角色在各 World 的组件注入。Editor／Game target 构建通过；`Scripts/Task07CreateAssets.ps1` 重复执行后，`Scripts/VerifyTask07Assets.ps1` 在全新进程中核实蓝图父类、最新编译状态、Manny Mesh、PawnData 蓝图类引用及任务 06／07 两个 Action 各一份。新增防护后 `Scripts/VerifyTask07.ps1` 再次通过：独立监听服务器和 ClientA 两人开局后再启动 ClientB，三个进程最终均记录 `PlayerStates=3 Characters=3 ValidCharacters=3 LocalPawn=1 CharacterMarkers=3`。服务端三次 `COMMITTED` 均晚于 Experience `Loaded`；无效 Experience 的服务器与客户端都进入 `Failed`，且没有角色或角色 marker。`VerifyTask06.ps1` 含任务 05 双进程、缺失插件负例和三轮 World 周期的回归也通过。详见 [任务 07 出生门控说明](Task07/SpawnGate.md)。本次尚未加入 PawnExtension／Hero、GAS、输入与相机。

代码审查新增 PawnData 的类约束：`ValidatePawnData` 要求 PawnClass 继承 `AMiniCharacter`，防止误填普通 `APawn` 后 Experience 到达 `Loaded` 却无法生成角色。任务 04 资产脚本现在只为**新建**的 PawnData 使用原生 MiniCharacter 占位，不改动已有资产；`MiniGameInstance` 的负例探针新增普通 `APawn` 被拒绝的检查。补丁后的 Editor／Game 重编译及任务 04 运行／资产验证均通过；旧探针脚本已修复读取尚未写完的空日志时的竞争。本次任务 04／05／06／07 探针运行在未 Cook 的编辑器游戏进程并使用 NullRHI，不能据此认定打包后联机或图形呈现已经验收。

## 插件状态与后续安排

| 插件 | 来源 | 当前状态 | 后续安排 |
| --- | --- | --- | --- |
| ModelingToolsEditorMode | 引擎自带 | 保留模板原有的 Editor-only 启用项 | 可用于灰盒；不是运行时框架依赖 |
| PythonScriptPlugin | 引擎自带 | 仅在任务 02 验证命令中临时启用 | 没写入项目长期插件列表，不需要下载 |
| ControlRig | 引擎自带 | 任务 02 迁入资源的软引用涉及 ControlRig；命令行资源加载已通过 | 实际使用角色动画图时核查启用与运行时配置 |
| AndroidFileServer | 引擎自带 | 显式禁用 | 首版只做 Windows，避免启动时写入无关 Android 文件服务配置 |
| GameplayAbilities（GAS） | 引擎自带 | 任务 03 已启用 | 任务 09 开始实现能力宿主 |
| GameFeatures、ModularGameplay | 引擎自带 | 任务 06 已用于 `MiniShooterCore` 激活与 AddComponents 注入 | 后续 Action 类型沿用本次的 World 作用域和回收路径 |
| EnhancedInput | 引擎自带 | 任务 03 已启用 | 任务 10 接入 InputTag 输入 |
| CommonUI／CommonInput | 引擎自带 | 任务 03 已核清并启用 CommonUI | 任务 19 实现 UI；CommonInput 是模块，不是独立插件 |
| ModularGameplayActors、GameplayMessageRouter | 本机 Lyra 的 `Plugins` | 任务 03 已迁入 | 按需调用运行时模块 |
| CommonGame、CommonUser、UIExtension | 本机 Lyra 的 `Plugins` | 任务 03 已迁入并核查依赖 | 会话与 HUD 在后续任务实现 |

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

任务 07 已通过补充 PawnClass 防护后的构建、资产全新进程复核、双端与晚加入探针；任务 04 及任务 05／06 回归也通过。下一步是任务 08 的 PawnExtension／Hero 四态协作。任务 06 的连续三次 PIE 与 Pak 打包程序单机烟测已通过；打包后联机、IoStore staging 及同进程不同 Experience 的多个 World 并存仍需后续验收。UE 内置 GameFeatureData AddComponents 随插件在进程级激活，当前 lease 防止过早卸载，但不阻止它注入到未请求插件的并存 World；详见任务 06 文档。
