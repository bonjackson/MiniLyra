# Mini Lyra 实施进度

更新：2026-09-27（Asia/Shanghai）

## 当前进度

- [x] 任务 01：范围冻结、空项目基线、本地恢复点、Editor 构建、基础场景加载。
- [ ] 任务 02–30：尚未实施。

用户已确认第三人称、2–4 人竞技场，并明确允许忽略旧实现、从空项目开始。本次据此重置活动源码和配置，保留旧工程文件作为本地备份，不把之前的 MiniExperience 启动壳计作已完成框架。

## 任务 01 的实际产物

| 项目 | 结果 |
| --- | --- |
| 工程 | `F:\FPS\FPS\FPS.uproject`，仍使用原工程名／模块名 `FPS` |
| C++ | 两个 Target、`FPS.Build.cs`、模块头／源文件；没有自定义 Gameplay 类 |
| 模块依赖 | Core、CoreUObject、Engine、InputCore |
| 默认类 | Engine.GameInstance、Engine.GameModeBase、Engine.WorldSettings |
| 默认地图 | 编辑器与游戏均为 `/Engine/Maps/Templates/Template_Default` |
| Content | 当前仅有 `.gitkeep`；自己的地图在任务 02 创建 |
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

## 插件：现在与以后分别需要什么

| 插件 | 来源 | 本期处理 | 后续安排 |
| --- | --- | --- | --- |
| ModelingToolsEditorMode | 引擎自带 | 保留模板原有的 Editor-only 启用项 | 可用于灰盒；不是运行时框架依赖 |
| PythonScriptPlugin | 引擎自带 | 仅在验证命令中临时启用 | 没写入项目长期插件列表，不需要下载 |
| AndroidFileServer | 引擎自带 | 显式禁用 | 首版只做 Windows，避免启动时写入无关 Android 文件服务配置 |
| GameplayAbilities（GAS） | 引擎自带 | 本次不接入 | 任务 03 启用，09 开始实现能力宿主 |
| GameFeatures、ModularGameplay | 引擎自带 | 本次不接入 | 任务 03 启用，06 开始实际装配 |
| EnhancedInput | 引擎自带 | 本次输入使用 Engine 基础类型 | 任务 03／10 接入 |
| CommonUI／CommonInput | 引擎自带 | 本次不接入 UI | 任务 03 核清依赖，19 实现 UI |
| ModularGameplayActors、GameplayMessageRouter | 本机 Lyra 的 `Plugins` | 不复制到空基线 | 任务 03 按依赖迁入 |
| CommonGame、CommonUser、UIExtension | 本机 Lyra 的 `Plugins` | 不复制到空基线 | 任务 03 迁入并核查依赖和插件描述 |

这里“不需要安装”指不用额外下载／购买。到对应任务仍然需要**启用引擎插件、复制 Lyra 的项目插件、配置 Build.cs 和 `.uproject`**，不能跳过这些步骤。CommonGame 的 CommonUser／OnlineFramework 传递依赖必须保留，即使首版不做平台登录。

## 本次问题与处理

1. **UE 5.8 的 `-NoUBA` 不会移除整个 UBA 执行器。** 本机引擎源码显示它只关闭 detouring，仍然创建 UBA 存储。首次构建尝试写 `C:\ProgramData\Epic\UnrealBuildAccelerator` 时被沙箱拒绝，产生重复错误日志并耗尽 F 盘剩余空间。已停止本次构建进程，清空其生成的两份异常日志，恢复空间；没有删除用户资产和备份。脚本现固定 `-UBARootDir=<项目>\Saved\BuildCache\UBA`，修正后实际编译成功。
2. **环境沙箱限制。** 曾出现辅助进程启动失败；重新在沙箱内运行场景校验时，Zen/DDC 本机缓存服务也因不可写而退出。最终配置已通过获准的本机执行重新验证成功，完整日志包含成功标记；这些限制不表示缺少 UE 插件。
3. **初次 Git 初始化中断且归属沙箱账户。** 将没有提交的临时 Git 元数据保存在 `Backups/Task01_SandboxGit`，以本机用户重新初始化，正常 Git 操作已恢复；没有扩大全局 safe.directory 配置。
4. **路线变化。** 旧启动链现在属于历史资料。已在 `MiniExperienceBootstrap.md` 中标注，并调整路线图：02 先用引擎 WorldSettings，03–05 再从零创建正式 Mini 核心类。

## 恢复点与注意事项

重置前文件位于：`F:\FPS\FPS\Backups\Task01_PreReset_20260927`。

其中保存旧 Source、Config、Content、Binaries、Intermediate、Saved、原 FPS.uproject、.vsconfig 和当时的 Docs；Source 的 SHA256 清单为 `SourceHashes.json`。移动在同一工程目录内完成，没有删除旧内容。约 1.9 GB 的原 Content 完整留在该备份中，任务 02 可从这里或本机 Lyra 挑选素材。

Backups 被 Git 忽略，是本机恢复点，**不会随 clone 自动取得**。如要还原旧工程，先关闭 Unreal Editor，另存当前新基线，再从备份恢复对应 Source／Config／Content 和 FPS.uproject；旧生成目录通常不必恢复，可重新编译生成。不要把旧 Source 与新 Source 直接混在一起。

新空项目的恢复点使用 Git 标签 `task01-baseline`。查看记录：

```powershell
git log -1 --oneline
git show task01-baseline --stat
git status --short
```

后续添加 `.uasset`／`.umap` 前，应确保协作机器装有 Git LFS，首次取得仓库后执行 `git lfs install`、`git lfs pull`。最终检查时仓库已有 `origin=https://github.com/bonjackson/MiniLyra.git` 配置；本次保留该配置，没有访问或推送远程，也没有核验远程 LFS 服务。

## 下一次入口

执行任务 02：从备份与本机 Lyra 审计少量角色、武器、动画素材；创建 `/Game/Mini` 和自己的灰盒测试图。先建立可控资源链，不重新引入原版 Lyra 的整套 Gameplay 蓝图，也不提前实现 Experience／GAS。
