# 任务27：四人性能与资源基线

更新：2026-10-04。前置提交为任务26 `e63ac1e`。本项建立真实渲染、网络与普通旅行基线，并加入首版默认画质和60 FPS上限。四个同机 Editor 窗口未达到严格1080p／60目标；测量捕获通过不代表帧率目标通过。最终 Game 构建状态见 Evidence/Acceptance.txt，打包 Game 成本将在任务28补测。

## 条件与测量口径

Windows11 26200，i7-13650HX（14核／20线程），15.7 GiB可用物理内存；真实各端 RHI 为 D3D12、RTX4070 Laptop GPU、驱动592.82。四个 UnrealEditor Development `-game` 独立进程共用CPU／GPU／RAM，本机回环 Listen＋3 Client。一个窗口活动，其余后台；允许重叠，未最小化。电池读回为外接电源状态，未修改系统电源方案。硬件枚举与实际adapter分别记录在各轮 Hardware.json／RENDER marker。

每端实际viewport1920×1080、ScreenPercentage100、VSync0、DynamicRes0、BackgroundIdle0；不使用NullRHI、固定时间步或benchmark。首轮窗口被UE自动收缩，实际条件门拒绝；runner补 `-ForceRes` 后才通过viewport读回及PNG尺寸校验。Current保留本机既有画质（GI0、Reflection0、Shadow1、AA3，其余3）；Performance明确GI0、Reflection0、Shadow1、AA2，其余保持，所有值逐端／逐World读回。二者不是全Epic与全Low的比较。

Wall FrameMS是唯一GFrameCounter对应的实际单调钟间隔；Game／Render／RHI为引擎busy时间。GPU使用引擎GPU history独立游标，每个保留样本仅读一次，正式窗口任意Disjoint、无效／零值或样本不足都保留partial CSV并失败。CPU与异步GPU分别分析，接口没有GPU origin frame，不能逐行配对。普通窗口暖机10秒，首尾各1秒不计；Four采30秒、Practice15秒、Restart每World10秒。Combat暖机2秒后采真实Playing，阶段切换停止读取，不声称能剔除预知的末尾1秒。

严格目标：平均FPS≥60且wall P95≤16.666667ms。Near60仅为平均≥59且P95≤18ms的容差观察；预算超时帧比例另列，不把Near60改写为严格通过。

## 四窗口结果

下表每端均有60以上CPU／30以上真实GPU样本，GPUDisjoint=0。

| 条件 | Server FPS／P95 ms | ClientA | ClientB | ClientC |
| --- | --- | --- | --- | --- |
| Current，cap60 | 31.68／34.91 | 59.99／17.14 | 31.48／34.65 | 31.66／34.35 |
| Current，cap0 | 2.57／481.12 | 166.92／7.50 | 2.57／482.90 | 2.60／485.34 |
| Performance，cap60 | 38.69／29.90 | 38.97／29.21 | 38.79／29.75 | 59.17／17.24 |

Current cap60中ClientA是活动窗口；Performance中ClientC活动。活动窗口跟随前台发生变化。取消上限时一个活动窗口跑到约167 FPS，另外三个严重变慢；保持cap60及AA2后后台约39 FPS。该观测支持限制无谓渲染和收敛画质，不能从单次进程调度比较推导精确因果比例。

Current cap60的Game P95为3.10–3.94ms，后台Render约34.4–34.9ms、GPU约31.9ms；Performance后台Game约3.54–4.19ms，Render约29.2–29.9ms、GPU约27.0–27.4ms。主要慢项在渲染侧并与窗口活动状态、共享GPU／内存压力相关，当前证据没有细分到某个GPU pass或驱动策略。运行期间系统剩余物理／提交空间很低；不据此宣称生产Game在四台电脑也有同一瓶颈，不停止用户其他应用来改变条件。

独立Practice约60.00 FPS，P95 16.83ms，Game／Render／GPU P95分别2.34／3.10／5.06ms，Near60通过、严格目标未通过。真实地图、角色、双武器HUD与训练靶代表图已复核。

## 战斗活动与网络

两组Combat各四端正常捕获，复用任务23的真实两局：20次战斗GE死亡、4次环境GE死亡、生产保护／三秒重生、两次达分结算和第三局Playing，三remote完整owner ACK。Task27只被动采样；不能称为持续武器Fire／Reload输入性能，后者的功能由任务17／26、最终包整局由任务29验证。

| Combat条件 | Server FPS／P95 ms | ClientA | ClientB | ClientC |
| --- | --- | --- | --- | --- |
| Current | 32.87／34.40 | 52.02／28.35 | 32.91／34.11 | 33.04／34.10 |
| Performance | 37.91／30.69 | 59.98／17.15 | 38.05／30.33 | 38.15／29.98 |

每端约110秒真实Playing采样，保留CPU／GPU／network CSV。Performance主机约8.25 KB/s入、5.80 KB/s出；各client约1.87–1.91 KB/s入、2.30–3.62 KB/s出。统计为实际NetDriver累计bytes／packets差分，主机是三个连接总量；OutReliableBunches不是RPC次数。窗口帧率／移动上传与战斗活动影响流量，不能把这些数字当所有玩法的带宽上界。Practice无NetDriver，空network CSV明确表示Standalone。

## 五次真实再开局

初始World＋5次生产RestartArena，四端各6个新World；未重启进程、未强制GC。每次新PlayerState／ASC与旧World身份不同。角色数量、注册组件、私有库存和所有本地绑定严格等于基线：

| 端 | Actors／Components | PS／Pawn／Controller | 私有库存总条目 | HUD／元素／插槽 | VM／消息／Hero绑定 | AbilitySpecs |
| --- | --- | --- | ---: | --- | --- | ---: |
| Server | 69／125 | 4／4／4 | 8 | 1／4／4 | 16／6／16 | 4 |
| 每个Client | 56／112 | 4／4／1 | 2 | 1／4／4 | 16／6／16 | 4 |

旧WorldWeakAlive每个稳定快照均0；仅证明weak有效性结束，不能证明全部allocator内存已归还。所有World／PS／ASC观察是weak或纯身份值，没有强保留旧世界或global delegate。

| 端 | 6轮全局对象范围 | Committed，cycle2→6净增 MiB |
| --- | ---: | ---: |
| Server | 45110–45255 | 44.34 |
| ClientA | 44969–45006 | 20.04 |
| ClientB | 44969–45006 | 25.91 |
| ClientC | 44969–45006 | 29.15 |

第一轮缓存热身单列，cycle2–6无玩法对象／绑定持续累积，但Committed仍有约0.6–1.4%增长，作为后续调查边界保留，不能声称已证明长期无内存增长。WorkingSet受系统分页影响明显，不能把下降当修复证据。DiagnosticArrayStringBytes单列，仅含指定数组／字符串，不含全部容器／allocator开销，不从Committed扣出所谓净游戏内存。任务28在Game包复测趋势，必要时再细分渲染资源和allocator；本项没有逐Action贡献内存测量。

## 实际修改与复现

新增显式非Shipping的Task27测量Subsystem和runner，RHI／RenderCore是引擎模块，无新UE插件。正常游戏不创建该探针。Controller只在Task27模式与独立IsolateInput双开关下忽略外部设备事件，保留模拟输入通路，不借Task25开关启动别的probe。

首版 DefaultGameUserSettings 配置100%渲染比例、60 FPS上限、GI／Reflection0、Shadow1、AA2，其余3；只作为新用户默认值，既有Saved设置仍有优先级。以实际readback判断设置，不能只凭ini字面值宣称生效。该档仍未实现单机四窗口严格60，后续包内复测与跨机验证继续保留。

```powershell
Scripts/BuildProject.ps1 -Target FPSEditor -DisableAdaptiveUnity
Scripts/BuildProject.ps1 -Target FPS -DisableAdaptiveUnity
Scripts/VerifyTask27.ps1 -Mode Four -Profile Current -WithMedia -SampleSeconds 30 -TimeoutSeconds 600
Scripts/VerifyTask27.ps1 -Mode Four -Profile Current -MaxFPS 0 -SampleSeconds 30 -TimeoutSeconds 600
Scripts/VerifyTask27.ps1 -Mode Four -Profile Performance -WithMedia -SampleSeconds 30 -TimeoutSeconds 600
Scripts/VerifyTask27.ps1 -Mode Practice -Profile Current -WithMedia -SampleSeconds 15 -TimeoutSeconds 180
Scripts/VerifyTask27.ps1 -Mode RestartTrend -Profile Performance -Restarts 5 -RestartSampleSeconds 10 -TimeoutSeconds 900
Scripts/VerifyTask27.ps1 -Mode Combat -Profile Current -WarmupSeconds 2 -TimeoutSeconds 600
Scripts/VerifyTask27.ps1 -Mode Combat -Profile Performance -WarmupSeconds 2 -TimeoutSeconds 600
```

结果先写入独立GUID的Saved/Task27Results，再复制至Evidence下对应名称；Run.json记录实际参数与RunId，Hardware.json记录条件，Summary由runner从原始CSV重新计算。runner只结束自身创建的PID；采样完成后停止进程不作为正常Quit验收。Current／Performance两种四窗口图和Practice代表图按真实PNG复核，所有媒体端均检查1920×1080尺寸。本项不承诺GPU跨端时钟对齐、移动目标弱网性能、四台硬件60FPS或打包程序已经通过。
