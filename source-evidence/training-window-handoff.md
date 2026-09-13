# Training 最终编辑器窗口交接

交接日期：2026-09-13。此文件是操作交接，各项通过范围以引用的原生报告为准。最终归档复测 PID 22876 已正常退出；20:51:53 UTC 复核无 Editor、LyraGame、Live Coding 或 UBT 进程，TCP8000 无监听，UE/UBT 窗口现已释放。

## 当前工程与窗口

- 训练副本：`F:/UE/LyraDocLabs/LyraTraining/LyraStarterGame.uproject`。
- 源引擎：`E:/UnrealEngine`，UE 5.8.1。
- 最终 Training Editor PID **19024** 已于 **17:11:34 UTC 正常关闭，exitCode=0，TCP8000=0**，见 `verification/training-final-editor-closure.json`。已停止 PIE、保存当前地图，然后发送正常主窗口关闭请求。
- 早先 PID 2324 在 13:50 UTC 重复准备脚本时触发 World GC 检查；该历史失败仍保留于 `verification/training-prepare-repeat.json`，不被本次通过覆盖。
- 不复用 PID、窗口索引、Slate ref 或 MCP session。`.local/config-mcp-context.json` 已标 `closed`，适配器拒绝继续调用该连接。
- 本轮窗口已交还根任务和 MMO 构建队列。后续 UE/UBT 由根任务协调，不并行操作共享引擎 DLL 或 8000 端口。

## 已有证据

- 19/19 配置视图，26 张原生 PNG：`source-evidence/configuration-training.json`。`verification/training-configuration-files.json` 核对原图、公用图及 manifest 的 SHA-256 和 PNG IHDR 尺寸，26/26 一致；所有新增图已插入对应章节。
- 单人生命周期 4/4：`verification/training-lifecycle.json`，换弹 11/48 → 12/47、真实导航移动与 Rifle 重叠拾取、原生自毁重生后同 PS/ASC、新 Avatar、Dash/HUD 唯一。
- 三图原生复制文本导入编译 3/3：`verification/training-blueprint-roundtrip.json`。Jump 20、Dash 26、W_OverallUILayout EventGraph 10 节点，无编译错误或警告，完整测试副本均已保存。
- 导航构建和地图保存：`verification/training-navigation.json`。13:42:27 UTC `BUILDPATHS`，0.02 秒，Map Check 0 errors/0 warnings；角色和拾取点可投影到 Z=60，13:45:51 UTC 保存后完成真实路径拾取。没有新增 Spawner 或 NavMeshBoundsVolume。
- `verification/training-cook.json` 是 **12:34 UTC、导航更新之前**的成功结果。保持历史记录，最终 Package 必须重新 Cook。
- 17:00 UTC 新 Cmd 进程与同进程重复准备 **2/2**；17:05 UTC 根布局 **1648×1264、1:1 全 10 节点图**；17:08 UTC 最终导航地图单人首图实际 **1920×1080、Medium 十项=1**。对应 `training-prepare-rerun.json`、`root-layout-blueprint.json`、`training-final-preview-state.json`。

## 先验证完整准备脚本

源码位置 `examples/LyraTraining/prepare_training.py`。已修复“当前已打开 L_TrainingRange 时又调用 LoadLevel”的重复卸载；脚本检查完整 World 对象路径，同地图跳过重载。导航步骤现在调用 `LyraDocToolsLibrary.build_training_navigation`、检查三个现有拾取点投影并保存。

复验脚本为 `scripts/verify-training-prepare.py`，于 **17:00:07–17:00:38 UTC 原生 2/2 通过**，PID 2000，进程退出码 0。它先核对 repo/lab 的准备源码 SHA 相同，再在全新 commandlet 进程里运行两次。这里的 fresh 指全新原生进程读取已生成的现有资产，不是删除资产后首次创建；保留已有地图、测试蓝图与历史初次创建记录。第二轮 `sameNativeWorldObject=true`、标牌仍为 TextRenderActor_0、三点投影均 Z=60；原生汇总 0 errors/297 条既有翻译冲突警告。

本轮第一次 Cmd（PID 12964）在调用 `BUILDPATHS` 时还暴露了新的 UI 空指针崩溃，见 `training-prepare-navigation-failure.json/.log`。现行 helper 先 `FlushAsyncLoading` 与 `FinishAllCompilation`，确认资产编译已清零后，仅移除该 World 的 AsyncLoadLock，拒绝其他锁，再调用会等待 `EnsureBuildCompletion` 的原生导航 Build；不进入图形进度窗口。该修正的完整 LyraEditor 目标 4 actions、16.89 秒成功，见 `training-navigation-helper-build.json`。手工图形 Editor 的 Build Paths 仍有效。

根任务明确交还原生窗口、完成 TrainingEditor 增量编译后，从 `F:/UE/LyraDoc` 的 PowerShell 7 执行：

```powershell
& E:/UnrealEngine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe `
  F:/UE/LyraDocLabs/LyraTraining/LyraStarterGame.uproject `
  -run=pythonscript `
  '-Script=F:/UE/LyraDoc/scripts/verify-training-prepare.py' `
  '-EnablePlugins=LyraDocTools,PythonScriptPlugin,EditorScriptingUtilities,TrainingRange' `
  '-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=1,[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame=1,[DevOptions.Shaders]:bForceUseSCWMemoryPressureLimits=False,[SystemSettings]:r.ForceAllCoresForShaderCompiling=0' `
  '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False' `
  -unattended -NullRHI -LiveCoding=false -Multiprocess -corelimit=3
```

该命令不执行 UBT。`-Multiprocess` 避免启动时自动进入 SDK 验证与额外 UBT；MCP 自动启动被当前进程的 ini 覆盖关闭。脚本默认输出独立的 `verification/training-prepare-rerun.json`，不会覆盖 `verification/training-prepare-repeat.json` 的 GC 失败事实。引擎实际参数解析见 `PythonScriptCommandlet.cpp:20`，MCP 配置类见 `ModelContextProtocolSettings.h:17`。

验收须同时检查原生退出码为 0、报告 `passed=true`、两次 `runs[].passed=true`、第二次 `sameNativeWorldObject=true`。每次记录新写入的 `Saved/training-assets.json`、三个原有拾取点的导航投影、唯一标牌、Experience/PawnData/AbilitySet 引用及保存后的地图 SHA。报告运行前及阶段开始时先落盘；若再发生原生崩溃，不能把残留的 `status=running` 当成通过。

两次 prepare 使用各自独立的 Python globals 字典，并仅释放该次脚本的临时包装引用。第一次成功后，验证器**特意保留目标 Training World 的 Python 包装对象**，再在同进程执行第二次，以覆盖原先触发 GC 失败的条件；第二次必须保持同一个原生 World 和标牌对象。不可清空引擎所有对象或编辑 `.umap` 二进制来规避 GC。脚本自身跳过已打开目标地图，才是此次重载问题的修复。

## 根布局真正 1:1 导出已完成

资产 `/Game/UI/W_OverallUILayout`，Widget Blueprint，Graph 为 `EventGraph`。Designer 四个层栈要保留；原件只读。已有 `public/blueprints/lyra/root-layout-registration.txt` 为真实 10 节点原生文本，且已导入完整 WidgetBlueprint 副本编译通过。

本轮 17:05:51 UTC 的原生插件输出为 `W_OverallUILayout_EventGraph_00001.png`，**1648×1264、314070 字节、Zoom 1:1**；已逐像素不变复制到 `public/media/lyra/root-layout-registration.png` 并插入 06 章。四个 Register Layer、四个容器变量、事件与 Comment 共十节点全部选中，橙色描边经过实际观察；CDO 的 ZoomAmount=1、MaxScreenshotSize=15360 已原生读取。以下保留复现步骤和失败原因。

失败图片保存在 Training 的 `Saved/Screenshots/WindowsEditor/W_OverallUILayout_EventGraph_00000.png`，为 **1210×743、Zoom -3**，不能发布为 1:1。来源和 SHA 见 `source-evidence/root-layout-blueprint.json`。

本机 Gradess Games Blueprint Screenshot Tool 1.1.7 的 `CaptureGraphEditor`（Private/BlueprintScreenshotToolHandler.cpp:127–153）只有 `SelectedNodes.Num()>0` 时才使用 `Settings.ZoomAmount`；无选择时使用当前 `CachedZoomAmount`。上次 Ctrl+A 没有落到实际 Graph 的选择集，仅选中资产标签页不够。

1. 核对插件已载入，工具栏 **Take Screenshot** 可见。原生读取 `BlueprintScreenshotToolSettings` CDO 的 `zoom_amount=1`、`max_screenshot_size=15360`；新进程要重新读取，不能沿用上个 PID 的验证。
2. 打开 W_OverallUILayout，切 **Graph**，在 My Blueprint 打开 **EventGraph**。先单击实际节点主体或图画布，不能只点 Graph 标签页、My Blueprint 列表、面包屑或 Details。
3. 按 **Ctrl+A**，立即检查节点是否出现全部选择描边。若没有描边，先修正实际焦点，不继续导出。原生 Slate Snapshot 可定位 SGraphPanel/SGraphEditor 周边控件，必要时在已核验 PID 的真实窗口点击可见节点。
4. 按 **Ctrl+F7**。等待新文件，不以原文件已存在判成功；记录新写入时间、输出路径和 CDO 设置。
5. 直接查看原始 PNG，确认显示 **Zoom 1:1**、四个 RegisterLayer 与全部输入变量/连线完整且字体正常。预期尺寸应由真实输出决定，不能先填写假尺寸；不得裁剪、重采样或用图像工具放大。
6. 正确原图发布到 `public/media/lyra/root-layout-registration.png`，更新 `source-evidence/root-layout-blueprint.json` 和配置计划中的 `graphDetailFollowUp`，在 `06-ui/common-game.mdx` 增加全局 `<BlueprintGraph>` 组件，`download` 指向既有 txt；保留现有 Designer/Graph 配置上下文图。

## 原生 MCP 适配器及 PNG 保存

工具顺序始终是 list → describe → call，调用串行。启动时先以 TCP8000 所属 PID 加完整 exe/uproject 命令行核实身份，再初始化 MCP；操作地图前再通过原生 Python 核对 World 和工程路径。

本地轻量适配器位于 `.local/config-mcp.py`，初始化器 `.local/initialize-config-mcp.py <PID>`。适配器每次调用前重新读取 TCP8000 的所属 PID、进程完整命令行和引擎 exe，任一身份不匹配就不发送 HTTP 请求；已关闭的旧 session 同样拒绝复用。根任务另有独立 `mmo-config-mcp.py` 等文件与 context，不互相覆盖。示例（PowerShell）：

```powershell
python .local/config-mcp.py list
python .local/config-mcp.py describe SlateInspectorToolset.SlateInspectorToolset
'{"action":"list"}' | python .local/config-mcp.py call SlateInspectorToolset.SlateInspectorToolset Windows
'{"ref":"本次实际窗口ref"}' | python .local/config-mcp.py call SlateInspectorToolset.SlateInspectorToolset Screenshot
```

JSON 通过 stdin 传入，避免 PowerShell 参数转义丢失引号。Screenshot 返回的原始 base64 直接解码为 `.local/config-mcp-image.png`，并记录 `.local/config-mcp-image-metadata.json`。`.local/save-config-capture.py` 接收 slug/ref/title/assetPath/chapter/fields/caption 的 JSON：先检查 PID、session、project、ref，再将原字节复制到 Training Saved/Screenshots 和 public，记录尺寸与 SHA。

配置图展开数组请点左侧小箭头，右侧 **+** 会修改元素数量。GameFeature 的 Registered 状态只需展开查看，不应误触状态切换或添加元素。Details 标签截断时优先真实拖宽面板或改变原窗口尺寸，再原生截图，不裁剪图片。PIE 输入验证执行 Output Log 命令后须在 3 秒内切回 Standalone 预览窗口，鼠标开火依赖预览实际焦点；窗口列表可能出现短暂空项，应按标题包含 NetMode Standalone 动态寻找。

## 训练场首屏已按真实 1080 重采

旧首图的原生窗口为 1926×1120、实际视口 1920×1082。本轮已经替换为新原图：**17:08:35 UTC，窗口 PNG 1926×1118、真实视口 1920×1080**，十个质量组均为 1。原始文件保存在 Training Saved/Screenshots/WindowsEditor/Training_Final_FirstPreview_20260913_1708.png；旧首图 metadata 保留在 training-references 的 previousFirstPreview。

复现时使用已经保存导航的 L_TrainingRange：开启单人 PIE，将十个 `sg.*Quality` 组核对为 **1（Medium）**，用当前本地 PlayerController 原生 `GetViewportSize` 读取并确认 **1920×1080**，再采集原生 Slate 窗口或视口 PNG。窗口边框可保留，PNG 外部尺寸不必等于视口尺寸；不得重采样。本轮图源地图 SHA 为 `8334a40e8118d1ef4d7a71152e7d5eb034ebb503e856f66a32ba491a223fb07f`；结束 PIE 后再次原生 Save 的最终文件 SHA 为 `59126aa6cd69f7da51b2a15ffe311012f7458f555d2ca5652238985fe3cb51c3`，193375 字节。保留各自真实快照，不将后一次保存的哈希重标到旧图。

本机上次浮动预览将 `LevelEditorPlaySettings.NewWindowWidth=1920`、`NewWindowHeight=1078` 才得到原生视口 1920×1080；这只是已观察到的本地窗口偏移，不能取代此次读取。Medium 可通过 `GameUserSettings.get_game_user_settings().set_overall_scalability_level(1)` 和 `apply_non_resolution_settings()` 应用，再以 `SystemLibrary.get_console_variable_int_value()` 核对十项质量值；不要仅根据菜单文字推断实际值。Python 的真实包装名是 `AbilitySystemLibrary`、`WidgetLibrary`。执行 Output Log 后切回预览并确认鼠标焦点，再取最终画面。

图形 Editor 启动也显式带 `-LiveCoding=false -Multiprocess -corelimit=3` 及上面的同一 Shader INI，本轮日志实际确认 `Using 2 local workers for shader compilation`。不要只给 Cook 限制 worker，而让图形首次启动使用全部核心。诊断脚本持有的 PIE World、Controller、Pawn 包装引用在停止 PIE 前逐一释放，不清空引擎全部 Python 对象。

两张 network 图的视口本来已是真实 1920×1080，但来自导航构建之前的地图；可保留为当时输入/复制验收范围，若重新联机采图需更新实际新证据，不能改旧截图的 sourceMapSha256。

## 结束与后续发布检查

图形窗口已保存训练地图并正常关闭。其后按独占队列完成最终 Package 与两次归档运行，详见下段；旧 Cook、两次真实 prepare 失败和成功复验均保留，不把历史结果改标为本轮结果。

最终构建已完成：`verification/training-package-editor-refresh.json` 核对 Engine BuildId `edea13a7-c914-4603-96c4-02e0279a216e` 与四份 Training 模块 manifest 及所需引擎工具模块一致。AllToolsets 本身是无 Modules 的聚合插件，不要求虚构独立 `.modules`；首次检查器误判及实际成功构建已保留在 `training-package-editor-refresh-first-check.json/.log`，没有手改 GUID。

`./scripts/package-training.ps1 -Mode Package` 于 **19:34:46–20:39:46 UTC** 完成，最终源地图 `59126a…` 重新 Cook；LyraGame 1021 actions / 3678.09 秒，Cook 96.34 秒 / 4087 processed / 0 error、299 warning / 2 SCW，Stage 90.84 秒、Archive 27.91 秒，UAT exit0。证据 `training-package.json` 原字节不再改写，后续运行报告记录它的 SHA。

同一归档内 exe `Artifacts/LyraTraining/20260913-193445-abe6db/Archive/Windows/LyraStarterGame/Binaries/Win64/LyraGame.exe`，SHA `c69ca4a481af2da0ed0e6e861e253c68c80adc5253f5879c3368c3bbfe889970`，两次 `verify-training-package.ps1` 均 11/11、正常 exit0：首轮 PID6700 于20:41:23Z结束，复测 PID22876 于20:45:51Z结束。没有禁用音频或改运行参数来消除首轮问题。

首轮 `training-package-runtime-first-run.json` 保留实际 AudioMixer 队列 Error/释放阶段 Stall，复测 `training-package-runtime.json` 无 Error/Stall 但有1次 Underrun Display。原生源码诊断与两次日志 SHA 位于 `training-package-log-analysis.json`。首轮8秒日志间隔包含同步抓栈开销，根因仍未唯一证明，不宣称音频性能通过。

公开主图 `public/media/training/packaged-preview.png` 是**首轮**原始 FScreenshotRequest PNG，1920×1080，SHA `7f352785906d4a002f5bf34bbbbdd6e90459c7aa749c09326dde9a1460b6e3ab`，绑定首轮报告和最终地图，保留黄色 MoviePlayer 窗口交接警告、Warmup 等待玩家与12/47 HUD。复测原图12/46保存在独立原生目录，未冒充主图来源。已有 first-preview 和网络历史图均未覆盖。

为本轮后续构建，Training 主 `Intermediate` 于 17:17 UTC 经全部 593 文件 SHA/长度/修改时间核对后迁为 junction：逻辑路径仍 `F:/UE/LyraDocLabs/LyraTraining/Intermediate`，物理路径为 `E:/LyraDocBuildCache/LyraTraining/Intermediate`。旧 F 目录保留为 `Intermediate.hdd-backup-20260913-171737`，没有删除；Engine、插件与 MMO 缓存均未移动。证据为 `verification/training-build-cache-ssd.json`，这只是缓存迁移通过，不是构建或性能提升的实测报告。
