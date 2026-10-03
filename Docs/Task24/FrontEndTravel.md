# 任务 24：前端与直接 IP 联机

更新：2026-10-03。前端、普通旅行及四人批准完成；最终七场景图形验收、实际退出核验与既有 UI 最终回归全部通过。提交／推送版本以 Git 记录为准。

## 产品入口

程序默认进入 `L_MiniFrontEnd`。菜单提供训练、创建竞技场、输入 IPv4 地址加入与退出。无需账号登录。训练和竞技中的菜单提供返回主菜单和退出，Listen 主机额外提供重新开始竞技场。

竞技仍为 2–4 人 FFA：至少两人开始比赛，服务器最多接纳四人。默认端口为 7777，地址示例为 `192.168.1.20:7777`。这套入口没有房间搜索、邀请、认证或主机迁移。

## 装配与旅行职责

| 对象 | 职责 |
| --- | --- |
| `DA_MiniFrontEndExperience` | 显式 `bIsFrontEnd`，无 PawnData、战斗插件或 ActionSet；唯一 AddWidgets 将原生前端压入 Menu |
| `MiniGameMode` | Experience Loaded 后按前端标志禁止战斗 Pawn；普通旅行重建比赛对象 |
| `MiniTravelSubsystem` | GameInstance 持久状态，封装训练／创建／加入／返回／再开局／退出；保存失败原因 |
| `MiniFrontEndWidget` | 真实按钮及地址输入；激活订阅旅行快照，停用释放委托 |
| `MiniConnectionStatusWidget` | Modal 显示忙碌／失败和恢复操作；拥有独立输入 gate |
| `MiniGameSession` | PreLogin／Login 共同批准入口中执行固定四人上限，死亡和暂时无 Pawn 的玩家仍占位 |
| `MiniPlayerController` | 观察新 World；可靠主机返回 RPC 保存文字原因后走引擎返回流程 |

首次创建使用 Arena 的 `OpenLevel(..., "listen")`；加入使用本地 `ClientTravel`；主机再开局使用普通 `ServerTravel`，`bUseSeamlessTravel=false`。PlayerState、ASC、Pawn 和比赛统计在新 World 重建。

主机返回使用引擎 `GameSession::ReturnToMainMenuHost()`，先通知远端，再走默认 `?closed`／前端返回链。网络失败回调不直接 Browse 或销毁正在 tick 的 NetDriver。错误状态保存在 GameInstance，直到前端读取并由用户关闭或发起新请求。

旅行请求按当前 World、请求 generation 与实际 NetDriver 归属过滤；重复请求在 busy 状态拒绝，75 秒 watchdog 为没有收到完成／失败回调的请求提供恢复入口。退出对局时先完成真实前端旅行，再请求正常进程退出；已在前端时直接退出。

用户弹窗显示可采取行动的中文原因：地址格式、满员、超时、断开、版本不一致或场景载入失败。底层资产 ID／地图路径／驱动错误保留在 `MiniTravel *_FAILURE_DETAIL` 日志中，避免将内部引用字符串作为产品提示。

Root 的 Canvas 层次为 Game=0、Loading=50、Menu=100、Modal=200。加载失败时 Loading 与可操作 Modal 分别拥有一个来源 gate；离开后两者均释放。

前端拥有正常的 PlayerState 和默认 ASC／HealthSet 对象，但没有战斗 Avatar、PawnData 或能力授予。前端菜单与连接 Modal 在激活时读取快照并订阅旅行状态，在停用／销毁时解绑。

普通旅行会销毁旧焦点目标。UI Policy 仅在主玩家的 Slate 焦点为空时恢复该玩家的视口焦点，后续输入模式及按钮焦点由 CommonUI 管理。地址输入框保留提交时的焦点路径，防止 Enter 回调创建错误 Modal 后又被输入框清除焦点；Modal 明确作为独立输入根。

主机驱动创建失败和端口绑定失败均保存 `MINI_HOST_FAILED` 与中文建议。引擎已处理的 ListenFailure 沿其返回流程恢复；null Driver 的 CreateFailure 通过 `SetClientTravel("?closed")` 排队到下一次引擎旅行处理，回调内不执行 Browse。GameNetDriver 失败只接受当前 World 中仍注册的实际驱动，旧世界回调不能覆盖新菜单。

## 作者与独立校验

`Scripts/Task24Assets.ps1` 的 Create、CreateAgain、Verify 在三个独立 Editor 进程执行。作者桥仅允许固定前端 Experience，唯一 Action 名为 `MiniFrontEnd_AddWidgets`。验证阶段不保存或修复资产。脚本比较既有训练／竞技地图、Experience、ActionSet 与 PawnData 的文件哈希。

默认地图／EditorStartupMap／项目 Experience 回退均指向前端；MapsToCook 明确列出 FrontEnd、Practice、Arena。最终 Cook 白名单和新包属于任务 28，旧任务 20 包不代表当前竞技版本。

## 验收记录

所有任务 24 专项进程从默认入口启动，不传地图或 Experience。持久 GameInstance 探针使用 Slate 的鼠标事件、焦点、字符输入与 Enter／Escape，记录实际 `UI_CLICK`／`ADDRESS_TYPED`；按钮必须实际 hover、按下并拥有鼠标捕获，错误关闭还要求产品错误状态确实清除。诊断 Actor 只传送检查点，不替代产品按钮。`VerifyTask24.ps1` 的场景为：

| 模式 | 独立进程与目的 |
| --- | --- |
| Flow | 主机／客户端，创建、加入、离开再加、普通再开局、主机结束及两端训练返回 |
| Failures | 无效地址、无人监听导致真实 PendingNetDriver 失败，然后加入正常主机 |
| Capacity | 主机＋三客户端占满，第五人拒绝，一人从菜单离开后第五人重新加入，其他两人身份不变 |
| HostLoss | 已进入竞技的主机／客户端；脚本实际终止其跟踪的主机进程，客户端通过真实驱动失败恢复 |
| Quit | 客户端／主机从游戏菜单退出，确认先完成真实返回前端旅行再正常退出进程；另一个默认前端进程验证直接退出按钮 |
| HostFailureListen | 独占同一 UDP 端口，实际点击创建触发引擎 ListenFailure；提示恢复后释放端口，再经界面成功创建与返回 |
| HostFailureCreate | UE Development 的驱动覆盖参数指定不存在的主类及 fallback，实际点击创建触发 null Driver 的 CreateFailure；恢复菜单后实际进入训练并返回 |

Failures／HostLoss 测试命令行请求五秒连接超时，其他场景请求三十秒以涵盖普通 ServerTravel 的约四秒离开等待；本次 Editor 的真实 Pending timeout 日志为二十秒，已建立连接的 HostLoss 实际 timeout 为六十秒。测试覆盖不改生产配置。容量信号使用每次独立的 GUID 目录，避免旧信号造成假通过。`-WithMedia` 每次先删除目标旧截图，再请求真实渲染，实际文件保存后才进入检查点。

独立进程无法同时拥有操作系统前台窗口；Slate 正常会阻止后台程序建立鼠标捕获。本探针仅在每个真实鼠标事件序列内暂用 `SetHandleDeviceInputWhenApplicationNotActive(true)`，之后恢复原值，并记录 `AppActive`／`ProbeBackgroundCapture`。这属于 opt-in 诊断设置，不进入产品 Widget，也不直接调用 OnClicked、按钮处理器或旅行 API 代替该界面验收。此前仅以 Down／Up=Handled 判定的记录不作点击成功证据。

```powershell
.\Scripts\BuildProject.ps1 -Target FPSEditor -DisableAdaptiveUnity
.\Scripts\BuildProject.ps1 -Target FPS -DisableAdaptiveUnity
.\Scripts\Task24Assets.ps1
.\Scripts\VerifyTask24.ps1 -Mode All -WithMedia -TimeoutSeconds 420
.\Scripts\VerifyTask24.ps1 -Mode HostFailures -WithMedia
.\Scripts\VerifyTask19.ps1 -WithMedia
.\Scripts\VerifyTask19Loading.ps1 -WithMedia
```

| 场景 | 当前结果 |
| --- | --- |
| FPSEditor／FPS Development 构建 | 最终 Editor／Game 均以 `-DisableAdaptiveUnity` 通过，进程 ExitCode=0 |
| 资产 Create／CreateAgain／独立 Verify | 三独立 Editor 均通过，0 errors／0 warnings，八项受保护资产哈希不变 |
| 两进程默认前端、真实按钮创建／地址加入 | 最终 All 的 Flow 通过；均从默认前端启动，真实 Slate 按钮创建及输入地址加入 |
| 客户端离开、再次加入、主机结束原因 | 最终 Flow 通过；真实 Logout、普通再开局重建 World／PS／ASC 与清分、主机结束明确原因 |
| 无效地址、无人监听失败、恢复连接 | Failures 通过；真实 Enter 关闭、Pending gate=2、实际取消／重试／驱动超时、成功加入及返回 |
| 主机突发断开 | HostLoss 通过；真正终止跟踪主机，客户端由真实 NetDriver failure 恢复前端并关闭提示 |
| 客户端／主机／前端退出按钮与实际进程结束 | 最终 Quit 通过；客户端和主机均实际返回前端后退出，默认前端直接退出，三个进程均正常 ExitCode=0，主机确认客户端真实 Logout |
| 四人成功、第五人拒绝、释放位置后再加入 | Capacity 通过；四人实际批准，第五人收到 MINI_SERVER_FULL；一人真实菜单离开后再次批准，其余两人身份保持 |
| 训练入口与返回、普通竞技再开局 | Flow 通过；两端实际进入三靶训练，真实 GE，返回前端且旧监听归零 |
| 主机创建／绑定失败 | 两个 HostFailure 均通过；明确中文错误、恢复菜单及下一次真实产品操作 |
| 既有加载 UI、HUD 与普通旅行回归 | Task04 新默认扫描、Task21／23 独立资产验证和 Task20 训练核心／三次普通旅行／七个新 gameplay World 通过；最后一次焦点修复后的 Task19 媒体三端 HUD／菜单／根布局／真实 Core 撤销回归通过，Task19Loading 媒体正常／失败双端全部通过，两个 runner 均 ExitCode=0，最终失败弹窗与 HUD 截图复核通过 |
| 实际渲染与文字／焦点检查 | 最终 All 带渲染执行七场景，脚本 ExitCode=0；960×540 精选截图已复核，中文可读且无裁切、错误弹窗不暴露内部驱动串，地址 Enter 关闭提示及菜单真实点击通过 |

最终七场景依次为 Flow、Failures、Capacity、HostLoss、Quit、HostFailureListen、HostFailureCreate，均 PASS。其中 Quit 的成功要求进程实际正常结束；其余场景通过检查点后由 runner 清理仍存活的测试进程，不能把清理退出写成产品退出证明。

收尾检查：全部 PowerShell 脚本解析、四个修改 Python 脚本的语法编译与 `git diff --check` 均通过。任务 24 已满足本项验收；独立提交与远端版本以 Git 记录为准。

## 保存的证据

精简关键日志见 [Verification.txt](Evidence/Verification.txt)，保留当前最终 `Task24-WithMedia-*.log` 的真实 marker 与时间戳，不保存命令行和整份引擎日志。完整本地日志仍位于 `Saved/Logs`。

| 画面 | 精选证据 |
| --- | --- |
| 默认主菜单与客户端游戏菜单 | [MainMenu.png](Evidence/MainMenu.png)、[GameMenu.png](Evidence/GameMenu.png) |
| 非法 IPv4 地址与实际连接中 | [InvalidAddress.png](Evidence/InvalidAddress.png)、[Connecting.png](Evidence/Connecting.png) |
| 第五人满员提示 | [ServerFull.png](Evidence/ServerFull.png) |
| 主机主动结束与实际主机丢失 | [HostLeft.png](Evidence/HostLeft.png)、[HostLoss.png](Evidence/HostLoss.png) |
| null Driver 创建失败 | [HostFailure.png](Evidence/HostFailure.png) |
| Experience 加载失败的前端恢复 | [ExperienceFailure.png](Evidence/ExperienceFailure.png)、[ExperienceFailure.txt](Evidence/ExperienceFailure.txt) |

## 当前范围

本机独立进程验收和跨机器局域网验收分别记录。不会将本机回环测试写成跨机器结果。最终打包后两局验收留在任务 29；连续重生／往返与迟到工作回收继续在任务 25 收紧。
