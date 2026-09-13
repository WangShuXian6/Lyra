# 单区域 MMORPG 实验

这个工程从空白 C++ 项目重建 Lyra 的关键模式：Experience/PawnData/AbilitySet、PawnExtension 初始化状态、PlayerState 上的 ASC、Game Feature 注入、CommonUI 层栈、按 LocalPlayer 隔离的 UIExtension，以及真实 Designer/MVVM 界面。角色使用本机 UE 模板 Manny，动画主图通过 Linked Anim Layer 接入移动与空中姿态；官方资源按清单本地复制，仓库只发布原创增量。

## 准备

1. 使用仓库 `scripts/prepare-labs.ps1 -Lab MMORPG` 把本目录同步至 `F:/UE/LyraDocLabs/MMORPG`，并按 `dependencies.json` 从本机官方 Lyra 复制四个插件及 UE 模板 Manny 资源；不要在原 Lyra 工程中添加本模块。手工复制时，必须先将 `E:/UnrealEngine/Templates/TemplateResources/High/Characters/Content/Mannequins` 复制到实验工程 `Content/Characters/Mannequins`，确认网格、BlendSpace 与 Fall Loop 存在，再运行资产生成器。
2. 确认同一源码引擎的所有 Live Coding 会话已结束，构建 `MMORPGEditor Win64 Development`。
3. 用该实验工程运行 `prepare_content.py`，依次生成动画接口/主图/层图、角色与技能蓝图、Experience/PawnData/AbilitySet、7 个 InputAction 与映射、实际 GameMode、七个 Designer 界面及其 MVVM 绑定、UI Policy、GameFeatureData 与健康接口示例。脚本原生编译并验证节点文本往返，报告保存至 `Saved/Evidence`。

   下载增量已包含 `/Game/MMO/Labels/DA_TutorialExamples`，只将 BP_BackendHealth 及其依赖标记为 AlwaysCook。`prepare_content.py` 不创建该标签；完全从空白自建或删除 Content 后重新生成时，还要在实验编辑器 **Output Log → Cmd** 输入 `py "F:/UE/LyraDoc/scripts/mmorpg/prepare-health-cook-label.py"`，检查 `Saved/Evidence/mmorpg-health-cook-label.json` 的 passed/saved。然后保存、正常关闭并重启编辑器，执行 `py "F:/UE/LyraDoc/scripts/mmorpg/validate-health-cook-label.py"`，确认当前扫描规则检查通过再 Cook；不要把 Tutorial/Tests 副本加入包。

4. 构建 Backend Program，按后端文档初始化 PostgreSQL 并启动服务。
5. 运行 `pwsh -File F:/UE/LyraDoc/scripts/mmorpg/Start-Lab.ps1`。它从本机加密配置读取游戏服务器身份，等待服务器就绪，再打开两个 1920×1080、中等画质客户端。客户端环境明确移除数据库凭据和服务器密钥。
6. 若服务器已运行，使用 `-UseRunningServer`；先检查单客户端可以传入 `-ClientCount 1`。这是 Editor 二进制的开发验证路径；发布路径使用打包后的 Client/Server。进程清单与原始日志在实验工程 `Saved/Logs/Interactive`，不应发布包含一次性票据的原始连接日志。

登录界面访问 `http://127.0.0.1:8088`。创建两套账号与角色，分别进入区域。键鼠：WASD 移动、右键旋转、Tab 选目标、1 普攻、2 奥术箭、I 背包、P 设置。手柄：左/右摇杆移动/视角、Y 选目标、A 普攻、X 技能、View 背包、Menu 设置；菜单中 A 确认、B 返回，键盘 Escape 返回。奥术箭消耗 20 法力并进入 3 秒 GAS 冷却。目标死亡奖励 25 经验和一瓶恢复 40 法力的药剂，5 秒后复活。

存活目标受到攻击后用 GAS 效果反击 25 Health。玩家生命归零时，Pawn-scoped HealthComponent 禁用移动并复制死亡状态；GameMode 三秒后通过同一 Experience/PawnData 重生，PlayerState/ASC 和两项能力保留。死亡期间正常离服会保存 Health 0，下次入服明确恢复到 Health 100 的检查点，法力、经验和背包保留。

设置页支持简体中文/英语切换并保存文化设置。打包前运行 `scripts/mmorpg/Build-Localization.ps1`，必须生成 en/zh-Hans 的 locres；源文和审核译文不一致时脚本会失败，不能跳过校验发布缺失翻译的界面。

首版 Windows 客户端要求 DirectX 12 / Shader Model 6。`Config/Windows/WindowsEngine.ini` 明确只 Cook `PCD3D_SM6`；中等画质仍由 `DefaultGameUserSettings.ini` 与运行时设置控制。

项目 LocalizationPaths 放在 `DefaultGame.ini` 的 `[Internationalization]`。正常启动脚本为每个客户端保留独立用户 INI，并用 `-MultiprocessSaveConfig` 允许多进程模式落盘；`-ForceRes` 保证预览维持 1920×1080。程序化验收入口：`Invoke-Smoke.ps1 -RestartBackendBetweenRounds`（八项）、`Invoke-Smoke.ps1 -DeathLogoutOnly`（独立死亡退出六项）、`Invoke-RenderedShowcase.ps1`（22张原生视口图、中英切换与新进程语言恢复）。键鼠/手柄焦点导航单独由原生输入记录验证。

## 数据与运行边界

- 账号令牌只保存在客户端 GameInstance；入服票据随连接发送，游戏服务器用自己的后端凭据兑换票据。
- 服务器同时等到 Experience Ready 和存档加载完成，再按 PawnData 创建实际 BP_MMOCharacter；客户端输入在 PawnExtension 就绪后才能处理。客户端不连接 PostgreSQL。
- 背包和经验仅向拥有者复制；目标、移动及外观在各客户端可见。
- 每 15 秒保存；普通离服提交最终快照，成功后释放会话。关闭服务器前先让两个客户端退出，并确认日志中的保存结果。进程被强杀时最多损失最近一个保存周期；不能把 `EndPlay` 中发起的异步 HTTP 当作可靠关停保证。
- 区域使用简单几何体和静态训练目标，角色使用 UE 模板 Manny；能力由服务器执行。这是网络与架构教学切片，不以该切片推断 MMO 在线容量。

## 已执行的首版验收

2026-09-13，Editor、Client、Server 与 UnrealPak/Bootstrap 目标均通过原生构建；WindowsClient/WindowsServer 实际 Cook、Pak/IoStore、Stage 完成。最终验收使用 `Saved/StagedBuilds/WindowsClient/MMORPG/Binaries/Win64/MMORPGClient.exe` 和 WindowsServer 对应的 `MMORPGServer.exe`，两个脚本都支持 `-ClientExecutable`、`-ServerExecutable` 参数。

| 实际范围 | 报告（仓库 verification 目录） |
| --- | --- |
| 8/8：战斗、后端重启与存档恢复、死亡重生、UI 返回登录 | `mmorpg-packaged-smoke.json` |
| 6/6：死亡等待中保存/离服、全新进程检查点恢复 | `mmorpg-packaged-death-logout.json` |
| 22 张原生 1920×1080 Medium 图、中英切换、新进程语言恢复、真实 Linked Layer 移动采样 | `mmorpg-packaged-rendered-showcase.json` |
| 实际包内翻译/字体/健康蓝图存在、测试副本缺席 | `mmorpg-staged-presentation.json` |
| 实际 ASC 两份带标签 Spec、InputID=-1、服务器按标签激活、重生不重复授予 | `mmorpg-tagged-ability-input-runtime.json` |

这些是程序化游戏与界面业务验证，能力限于 ServerOnly 瞬发。原生 Slate 注入键鼠/手柄事件的 17 项导航记录在 `mmorpg-native-ui-input.json`，不等于实体设备兼容性测试；也不能从两客户端结果推断大规模在线容量。之前失败和 Editor 开发记录保留其原始范围，最终产物与源代码通过报告中的 SHA-256 关联。

