# 任务 26：异步故障、网络时序与弱网回归

更新：2026-10-04。前置版本为已推送的任务25 `6140fd3`。本项完成：最终 Editor／Game Development 构建、19 组默认及媒体故障、四角色／权威失败复制、正常与弱网真实 RPC，以及任务05／06／19Loading／23／24回归均通过。最终布局另外重跑超时、坏前端退出和三端 Loaded 后失败媒体场景；版本以本项独立 Git 提交为准。

## 需要成立的行为

保留 Experience、ActionSet、GameFeatureData 与按 World 管理的 Action 框架。必需软类须完成真实加载与类型验证；权威场景对象准备好后才能公布 Loaded。界面依赖 Pawn／HUD 数据的后续工作不能反向阻塞 Pawn 出生，但必需界面挂载失败应进入明确失败状态，Loaded 后也不能忽略它。

失败清理解除本次 Action、异步句柄与观察者；服务器终态须复制给客户端。加载中返回时，取消真实请求并释放本 World 的功能使用者；迟到的完成或失败不能修改新 World。前端本身缺失必需资源时，仍能通过原生错误界面正常退出。所有未解决的启动加载都有有限期限。

故障只在非 Shipping 的显式诊断中注入，一次只消费一个精确 World 的请求快照；不修改正式资产、模板或别的 World。缺包检查走真实加载，取消检查保留真实待完成句柄。测试不直接调用成功／失败处理器来制造结果。

## 故障与回归矩阵

| 类别 | 需要的实际证据 |
| --- | --- |
| 未知 Experience、缺少必需插件 | 双端明确失败，零玩法出生，原生错误可恢复 |
| 已登记 PrimaryAsset 的包不存在 | 真 `LoadPrimaryAsset` 缺包失败，不能用未知 ID 校验代替 |
| 前端／所选插件 HUD／场景 Actor 必需类缺失 | 真实异步加载失败、正确来源与阶段、清理后正常前端 |
| UI／Actor Action 自有异步失败 | 验证中心类屏障后各 Action 的真实失败传播，区分 Loaded 前后 |
| 有效 Tag 对应的层或扩展插槽不存在 | 执行真实注册／挂载，局部贡献清理，明确失败 |
| Primary、插件、中心类、UI、Actor 加载中返回 | 原请求实际 Pending，再生产返回；旧请求取消或迟到激活最终释放，新前端不受污染 |
| 裸启动期限、坏前端退出 | 真实等待期限后失败；退出正常结束，不能循环加载相同坏前端 |
| 旧失败通知 | 捕获真实旧 payload，返回后重放只能被拒绝 |
| 正常角色与既有回归 | Standalone／Listen／Client／Dedicated 的类与对象准备；任务05／06／19Loading／23／24 |
| 网络时序与拒绝恢复 | 正常与弱网下真实客户端开火请求，序号重放、旧装备、非法请求及合法恢复 |

## 弱网口径

目标配置为各网络端出站延迟50毫秒、随机丢包1%，入站额外模拟为0。必须读取实际 NetDriver／Connection 配置与 RTT 样本；传入参数本身不证明生效。主机本地玩家不作远端延迟样本。本机回环只证明独立进程与模拟条件，不能写成跨机器 LAN。

可靠 RPC 的底层重传不等于应用层同序号重放，后者必须显式从客户端重复发送生产请求。只使用生产开火请求中的视角、装备实例与序号；诊断不得新增客户端自报目标、伤害或击杀的授权入口。固定目标用于核对一次接受请求只扣一发弹药、施加一次 GE，拒绝请求不造成伤害，换弹／切枪／重生后可合法恢复。

服务器按当前时刻做射线判定，没有回溯或延迟补偿。弱网移动目标的客户端瞄准与服务器命中可能不一致；保留当前视角、频率、装备、弹药及遮挡检查，不为测试放宽规则。

## 实际结果与证据

### 射击网络专项的实际通路

每轮使用真实 Listen 主机和三个独立 client，第三 remote 在生产 Playing 后才连接。三个 owner-only 诊断桥依次指挥客户端输入／检查点；玩法仍走既有私有 `ServerFire`，没有新增客户端自报目标、伤害或击杀入口。中心生产校验不放宽，仅在非 Shipping 显式开关下保留诊断 friend。

每个 remote 的首发走模拟鼠标 → EnhancedInput → InputTag／GA → 生产开火 RPC；之后显式从 owning client 重发同序号，测试非法视角、连续频率、真实空仓、按 R 完整换弹、按 Q 切枪、旧装备实例／GUID 拒绝，以及各拒绝后的合法恢复。authority 同时核对接受次数、逐阶段弹药、目标血量、实际 `MiniDamageGameplayEffect` 次数和序号。每轮应为 31 个 RPC 验证检查点、19 个实际伤害 GE；第三玩家最后一发导致真实死亡、仅一次计分及正常三秒重生。三 client 的复制比分／HUD／装备来源和本地修改拒绝另外核对。

固定瞄准 fixture 位于地图上方，authority 初始化目标健康、空仓与最后低血量，以便精确比较；实际伤害不直接调用服务器测试伤害接口。首稿仅在 authority 设置 MOVE_None，未停止 owning client 的移动预测，三个 remote 先经历真实跌落死亡。已改诊断双方 MOVE_Flying、保留 Movement Tick 和视角上传；正式移动／跌落规则未变。随后 runner 捕获部分 Q 键尚未释放就提前 ACK，已移除该诊断提前返回。最终 `VerifyTask26Network.ps1 -Profile All -TimeoutSeconds 300` 正常 Exit0，两种 profile 各四个 peer 均正常 Exit0。两个早期修正过程记录分别保存在 `Evidence/Network-InitialFixtureFailure.txt` 与 `Network-BaselineBeforeKeyReleaseFix.txt`，最终通过证据在 `Evidence/Network.txt`。

网络 profile 在真实 Driver 和所有连接完整读回，结束完整归零并再次读回。每 remote 顺序测量 20 个 echo RTT，独立客户端单调钟，无需跨端时钟同步。可靠 RPC 的重传与应用层同序号重放分开记录；旧装备病例是切枪后显式请求过期 identity，不能称为自然同 channel 乱序。本机四个 NullRHI Editor Development 进程，各限制 30 FPS；往返包含帧调度及网络处理，不能把双端各 50ms 的配置直接当成实际 100ms RTT。

| 条件／remote | RTT 中位数 ms | P95 ms | 实际样本数 |
| --- | ---: | ---: | ---: |
| 0ms／0%，ClientA | 32.842 | 33.031 | 20 |
| 0ms／0%，ClientB | 32.917 | 32.999 | 20 |
| 0ms／0%，ClientC 晚加入 | 32.932 | 33.007 | 20 |
| 两端出站各50ms／1%，ClientA | 166.321 | 166.501 | 20 |
| 两端出站各50ms／1%，ClientB | 166.351 | 166.483 | 20 |
| 两端出站各50ms／1%，ClientC 晚加入 | 199.588 | 199.687 | 20 |

每轮三个真实 remote 完整通过首发 GA、重放／非法视角／频率／空仓／旧装备拒绝与合法恢复，严格 31 个服务器 RPC 检查点、19 个实际 GE；三次完整换弹、三次切枪及旧来源能力撤销，新装备序号未被旧请求消耗。第三 remote 真 GE 导致一次死亡及一次击杀，旧轮／同生命／重生后旧死亡通知均拒绝；三 client 当前装备／弹药／比分 HUD 与服务器一致，本地计分修改拒绝。有限包计数只能描述本轮观测，不能声称测出了恰好 1% 实际丢失。

### 本轮补强与已定位问题

Manager 新增 `LoadingActionResources`：收集本次 Experience／ActionSet／所选 GameFeatureData 的必需软类，完成真实异步加载与类型校验；authority 继续等待本 World 的 AddActors 真 Ready 后公布 Loaded。插件 Action 由引擎激活，只观察其失败，不再次调用激活。界面后续挂载依赖本地 Root／HUD，不能反向等待 Pawn；有效 UIExtension 注册句柄还须证明 Widget 实际附着。观察者先订阅再读取缓存失败，Loaded 后持续监听；服务器失败原因通过真实复制传给客户端。

取消先使 generation／状态失效，再清观察者、取消真实句柄和撤销 Action；已完成的中心类加载句柄覆盖 Experience Action 的异步撤销期限。裸启动加载期限为 60 秒，错误 Modal 提供正常退出，坏前端返回不会重复尝试相同错误入口。

角色探针首稿误以为竞技场带 AddActors。实际 Arena 不创建训练对象，因此四角色统一用生产 Practice 装配验证真实 4 个 authority Actor；竞技场另由四人射击／弱网专项覆盖。首轮 Dedicated 启动发现 `MiniTravelSubsystem` 无条件依赖 UIManager，后者在 Dedicated 正确不创建，造成初始化 ensure。该 Subsystem 只承载本地前端／旅行界面，已添加 `ShouldCreateSubsystem` 在 Dedicated 跳过；服务器 Experience／网络功能继续由原有权威对象负责。修复构建后，`VerifyTask26Roles.ps1 -Mode All -TimeoutSeconds 240` 六组／16 个真实进程均正常 Exit0；正常四角色、Listen／Dedicated 各两 client 的中央类失败、Listen Loaded 后 UI self-load 失败全部通过。三端完整 reason 严格相同，客户端不注入；证据在 `Evidence/Roles.txt`。

旧失败测试是捕获真实 payload 后的诊断重放；Loaded 后 UI 失败使用该精确 World 贡献的诊断撤销／重新加载，走真实缺类异步失败。两者均不能写成自然网络乱序或真实网络迟到。AddActors 当前没有对象 Destroyed 自动报告必需失败的契约，本轮不作此承诺。

首轮 Editor／Game Development 构建成功，分别耗时 165.56／139.73 秒。`VerifyTask26Faults.ps1 -Mode All -TimeoutSeconds 240` 和对应 `-WithMedia` 均正常结束 ExitCode=0；各 19 个场景进程都正常 ExitCode=0。`NormalPractice` 另先单独通过，正常条件真实 Loaded、四个 HUD 元素、场景 Actor Ready 和本地输入均成立。

实际矩阵包括未知 ID、缺插件、登记 PrimaryAsset 真缺包、前端／HUD／Actor 必需类、UI／Actor 自有异步失败、缺层／插槽、五种真实 Pending 返回、裸启动 60 秒期限、坏前端退出和旧失败拒绝。失败后正常新前端不受旧工作污染；旧资源在真实 World cleanup、GC 前检查并缓存纯数据。插件加载中返回病例在原引擎状态机恢复后实际激活／撤销各一次，最终使用者释放。

首轮渲染矩阵输出 32 张真实 960×540 截图。已复核正常训练、缺 HUD／插槽、自有 UI 失败、返回前端和坏前端退出的代表图片；坏前端退出走真实 Slate hover／pressed／capture／down／up 后正常结束。超时截图首帧发现自动换行尺寸更新过晚，已将固定580宽面板的说明／地址文字设为508的明确换行宽度。最终 Editor／Game 分别32.12／93.87秒构建通过，超时与坏前端退出媒体各再跑一次通过；三端 Loaded 后 UI 失败媒体也通过。最终说明与按钮无重叠、裁切，生产角色仍存在于 Loaded 后失败场景；本项不承诺自动销毁已出生 Pawn。证据 `Evidence/Faults.txt`／`Faults-WithMedia.txt` 为首轮矩阵，`Builds.txt`／`Media-Final.txt`／`Regressions-Final.txt` 为最终补证，不能把首轮矩阵说成全部在最后一次构建重跑。

任务05重跑、任务06完整回归、任务19Loading双端有效／失败媒体回归、任务23 All 和任务24 All媒体均 ExitCode=0。任务24实际覆盖创建／加入／再开、非法地址与真实连接失败、四人上限、实际关闭主机后超时恢复、正常退出、两种创建驱动失败及后续恢复操作。初轮回归见 `Evidence/Regressions-Initial.txt`，最终补证见 `Regressions-Final.txt`。服务器终态原因可能先于 ID 到达客户端，旧脚本将 `LoadingAssets → Failed` 当作唯一路径；验收允许相同 ID／完整原因的 `Unloaded → Failed`，仍要求恰好一次终态、ID复制、零 Loaded／玩法出生。中央必需类失败在权威 Loaded 前阻断 Pawn；Loaded 后 UI 失败则核对失败传播与回收，不能沿用零 Pawn 断言。

## 复现命令

在工程根目录使用本机 UE5.8：

```powershell
Scripts/BuildProject.ps1 -Target FPSEditor -DisableAdaptiveUnity
Scripts/BuildProject.ps1 -Target FPS -DisableAdaptiveUnity
Scripts/VerifyTask26Faults.ps1 -Mode All -TimeoutSeconds 240
Scripts/VerifyTask26Faults.ps1 -Mode All -WithMedia -TimeoutSeconds 240
Scripts/VerifyTask26Roles.ps1 -Mode All -TimeoutSeconds 240
Scripts/VerifyTask26Roles.ps1 -Mode LateUIFailure -ServerKind Listen -WithMedia -TimeoutSeconds 240
Scripts/VerifyTask26Network.ps1 -Profile All -TimeoutSeconds 300
Scripts/VerifyTask24.ps1 -Mode All -WithMedia -TimeoutSeconds 420
```

裸启动 Deadline 真实等待60秒；异常日志按精确病例与完整行匹配，不忽略任意 Error。角色和网络 runner 只结束自己创建的进程。网络校验当前是 NullRHI 独立进程；媒体故障／角色／菜单回归正常渲染，性能测量另在任务27完成。

## 多人人工验收与已知边界

1. 两台同局域网 Windows 电脑使用同一版本，由前端创建，另一台输入主机IPv4与端口加入；先两人，再让第三／四人 Playing 中加入。确认血量、装备弹药、比分、阶段与 HUD 一致。
2. 在各真实 NetDriver 配置出站50ms／1%丢包并读回；让目标移动、跨掩体，依次开火、装填、切枪、死亡重生。比较服务器接受记录与拥有者弹药／命中确认，拒绝后继续合法开火；不能凭客户端准星判断服务器应命中。
3. 正常返回，再开新局；分别关闭客户端、正常返回主机、实际结束主机进程。确认其余客户端能从相应提示恢复前端并重新创建／加入，Loading不永久停留。
4. 记录两机硬件、版本、地址、网络配置、实测RTT与日志，并保存移动目标录像。上述跨机器和移动目标弱网人工步骤尚待补，当前只有单机回环的自动化实际证据。

未实现 server rewind、移动目标延迟补偿、主机迁移或运行中热切换 Experience。中心类句柄保留覆盖 Experience-owned Action 的异步撤销期限，不承诺覆盖引擎插件全部 pauser。AddActors对象销毁不自动升级为必需加载失败。随机丢包有限样本不证明长期恰好1%；固定目标验证授权、去重与状态恢复，不证明移动瞄准补偿。
