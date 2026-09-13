# TrainingRange：内容增量与打包运行检查

此目录包含原创训练场 Game Feature 描述和 `TrainingRangeVerification` C++ 模块。官方地图、角色、枪械和 UI 通过本机 Lyra 复制与资产准备脚本获得；这里不分发官方基础资产。插件保持 `Registered` 初态，由 `B_TrainingRange.GameFeaturesToEnable` 中的 `TrainingRange` 加载激活。

## 复制、构建与保存顺序

1. 首次创建在文档仓库执行 `./scripts/prepare-labs.ps1 -Lab Training`。已有实验工程只需把本目录的 `.uplugin`、`Source` 与 README 复制到 `LyraDocLabs/LyraTraining/Plugins/GameFeatures/TrainingRange`，保留已生成的 `Content`。
2. 在实验工程 `.uproject` 的 Plugins 中确认 `TrainingRange`、`LyraDocTools` 已启用。不要在官方基准项目添加这些教学文件。
3. 保存并正常关闭相关编辑器，构建完整 `LyraEditor Win64 Development`，再用本机原生 Python 运行 `prepare_training.py`，生成并保存地图、Experience、PawnData、AbilitySet 与 Dash。复制地图后必须构建导航，按 P 检查地面绿色区域并保存地图。
4. 正常关闭编辑器，执行 `scripts/package-training.ps1 -Mode Package`。它构建 `LyraGame Win64 Development` 并重新 Cook 当前地图。Development Editor 也编译验收主体，以提前发现 C++ API 错误，但 `StartupModule` 用 `GIsEditor`、`RequiresCookedData` 和显式参数三重检查禁止 Editor/PIE 执行；真正运行仅限 Development Game/Client。Shipping/Test/DebugGame/Server 不包含该模块。

本模块默认不注册 ticker、输入或截图回调。只有实际非 Editor 程序携带 **`-LyraTrainingVerify -LyraTrainingVerifyOutput=<本次独立目录>`** 才执行检查，不调用 `GiveAbility`、不修改背包/弹药/生命值、不改变角色 Transform，也不添加产品界面。

## 启动实际归档程序

从文档仓库，用 PowerShell 7.2 或更新版本执行：

```powershell
./scripts/verify-training-package.ps1 -ShowCommand
./scripts/verify-training-package.ps1
```

启动器读取成功的 `verification/training-package.json`，选择归档中 `Binaries/Win64/LyraGame.exe`，显式传入最终地图 `/TrainingRange/Maps/L_TrainingRange`，使用 D3D12、窗口模式、1920×1080 与 `-ForceRes`。需要测试另一次归档时传 `-PackageReport <该次 report.json>`；不要传 UnrealEditor 或根目录的 bootstrap 可执行文件。游戏窗口出现后保持焦点，等待它输出结果并正常退出。

原生 ticker 在模块启动后最多运行 90 秒；请求截图后另有 10 秒落盘超时。Game Feature 在到达 Registered 前的 Mount 阶段已经加载原生模块，因此计时可能早于 Experience 完成。外层启动器默认最多等待 150 秒，超时只尝试关闭自己创建的进程，正常关闭失败才终止该进程。失败报告保留原因，不能把重新运行成功覆盖成第一次也成功。

## 11 项实际判断

| ID | 原生判断条件 |
| --- | --- |
| packaged-world | RequiresCookedData、Game world、NM_Standalone、精确训练地图包名 |
| experience | Manager 已 Loaded，实际定义类为 B_TrainingRange_C |
| pawn-asc | 本地角色已 Possess，ASC Owner 为同一 PlayerState，Avatar 为当前 Pawn，Hero 的输入绑定已就绪 |
| training-dash-granted | ASC 中 GA_TrainingDash_C 恰好 1 份 |
| hud | 当前玩家、当前世界的 W_ShooterHUDLayout 可见且 Activated，恰好 1 个 |
| display-medium-1080 | 实际 Viewport 为 1920×1080，十个 sg.*Quality 均为 1 |
| movement | 原生 Input.+key W 后，沿控制器水平前向实际移动大于 20 cm；下落高度不计入 |
| dash-input-activation | LeftShift 前 Dash 未激活，输入后采到准确 Spec 活跃或当前 ASC/Avatar 的原生激活回调；两项观测分别记录，撞墙位移不会替代激活判断 |
| fire-ammo | LeftMouseButton 输入后，同一活动武器实例弹匣下降 |
| reload-ammo | R 输入后同一实例弹匣增加、备用减少，增加量等于减少量 |
| native-screenshot | FScreenshotRequest 含 UI、限制游戏视口，回调完成且原始 PNG 已落盘为 1920×1080 |

初始化、世界身份或角色生命周期不满足时会限时失败，不跳过缺少的检查。有 LoadingScreenManager 时，还等待它真正隐藏加载屏幕；本机默认会为纹理流送额外显示 2 秒，Experience Loaded 本身不足以保证游戏画面和输入已经可用。开火记录采到的活跃能力类，但通过条件是实际弹药变化；不能从一个含 Fire 的名称推断伤害或命中。Enhanced Input 全局 Tick 在世界 Tick 之后注入强制键，Controller 要到后续帧消费输入，因此除了持续秒数，各输入阶段还至少留出 3 帧，避免一帧卡顿就提前释放 R。换弹前留出开火释放时间；结束、World Cleanup 与模块关闭都会清理强制键并解绑 ASC/世界委托。

`SetOverallScalabilityLevel(1)`、`ApplyResolutionSettings(true)` 和 `ApplyNonResolutionSettings()` 用于设置本次显示条件；截图使用 UE 5.8.1 六参数 `FScreenshotRequest::RequestScreenshot`，由引擎保存 PNG。回调在 `SaveImageByExtension` 之后发出，但失败时也会发出，因此还核对文件存在、PNG 头、完整 IEND 结尾和尺寸；脚本只读取尺寸和 SHA256，未缩放、裁切或重新编码。

## API 边界与证据

`ULyraQuickBarComponent` 与 `ULyraInventoryItemInstance` 的本机类声明没有导出整个 C++ 类。本模块经 `UFunction`、`FStructOnScope` 调用其公开 `GetActiveSlotItem`、`GetStatTagStackCount`，不访问受保护字段，也不修改官方模块导出宏。ExperienceManager 与 PlayerState 使用已导出的原生 API。

每次运行保留 `runtime.json`、`training-package.png`、`game.log` 与启动器 `report.json`；最新启动器报告另写到 `verification/training-package-runtime.json`。启动器核对实际 PID、可执行文件路径与 SHA、打包报告 SHA、11 项结果、原生退出码和截图 SHA。**实现目前只完成源码/API 与脚本静态检查，完整目标编译及 packaged 运行尚待执行。**既有单人 PIE、联机 PIE、生命周期报告分别有效，不能代替此检查；本模块不验证 packaged 联机、拾取、重生、四方向语义、命中伤害或 Bot 决策。
