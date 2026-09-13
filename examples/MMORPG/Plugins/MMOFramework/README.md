# MMOFramework：把 Lyra 的装配方式带入空白工程

这是本教程原创的运行时插件。它依赖 UE 的 GAS、GameFeatures、ModularGameplay 与 EnhancedInput，不依赖 LyraGame、ShooterCore 或后端数据库。当前代码以本机 UE 5.8.1 头文件为基准；编译和主工程联调结果必须由实际验证记录补充，源码落地不代表运行通过。

## 结构与职责

`MMOExperienceDefinition` 选择 PawnData、要激活的 Game Features 与共享动作；`MMOPawnData` 选择角色类、技能集、输入配置、输入映射和动画类；`MMOAbilitySet` 描述服务器应授予的技能、效果及属性。`MMOInputConfig` 把语义标签映射到 InputAction；物理按键与轴向变换由 InputMappingContext 管理。它们都是 **Data Asset 实例**，不是本课程前半部分的 Lyra Experience 蓝图生成类，因此 Asset Manager 的 Has Blueprint Classes 应关闭。

`MMOExperienceManagerComponent` 放在 GameState。服务器只复制选择的 Primary Asset ID，各客户端独立异步加载自己的数据和 Game Feature，执行本世界动作后才广播 Ready。`MMOPawnExtensionComponent` 放在 Pawn，使用真正的 `IGameFrameworkInitStateInterface` 协调配置、Controller、输入和 ASC。它不负责账号认证，也不额外授予一遍 PlayerState 已拥有的默认技能。

详细函数签名、类路径与主工程接法见同目录 `api-contract.json`。

源码对照基准（2026-09-13）：Lyra 的 `Source/LyraGame/GameModes/LyraExperienceManagerComponent.cpp`、`Character/LyraPawnExtensionComponent.cpp`、`AbilitySystem/LyraAbilitySet.cpp`；原生接口来自 UE 的 `GameFrameworkInitStateInterface.h`、`GameFeaturesSubsystem.h`、`AssetManager.h` 与 `AbilitySystemComponent.h`。本插件使用自己的类、数据类型和依赖模块，并增加严格失败路径与每世界动作实例，不能把它描述为官方 Lyra 模块的原样复制。

## 从零安装与构建

1. 把 `MMOFramework` 整个文件夹放在新工程 `Plugins/MMOFramework`，确认 `.uplugin` 直接位于该目录。
2. 在 `.uproject` 的 Plugins 中启用 `MMOFramework`。主游戏模块的 Build.cs 添加 `MMOFramework` 依赖。
3. 保存未保存工作并正常关闭使用同一引擎的相关 Live Coding 编辑器。构建完整 `MMORPGEditor Win64 Development` 目标，不以拷贝源文件替代 UHT/UBT。
4. 重新打开 **MMORPG 实验工程**，确认 Plugins 中已启用本插件；内容浏览器的 Miscellaneous → Data Asset 类型选择器应能搜索 `MMOExperienceDefinition`、`MMOPawnData`、`MMOAbilitySet`、`MMOInputConfig` 和 `MMOExperienceActionSet`。

工程主类需要按后面的接入步骤安装组件。仅启用插件不会替你改变 GameMode 的玩家生成条件。

## 创建三个必要的数据资产

在 Content Browser 展开 **Content**（包根 `/Game`），新建 `MMO` 文件夹，再建立 `Experiences`、`Pawns`、`Abilities` 子文件夹。以下各次创建都从目标文件夹空白处右键 **Miscellaneous → Data Asset** 进入类型选择器。若只看到普通 Blueprint Class 创建菜单，返回选择 Data Asset。

### 1. DA_MMOAbilities

在 `MMO/Abilities` 选择类型 **MMOAbilitySet**，命名 **DA_MMOAbilities**，双击打开，直接查看 Details，不用寻找 Class Defaults。

展开 **Granted Abilities**，用加号创建两行：

| 行 | Ability | Level | Input ID | Input Tag |
| --- | --- | --- | --- | --- |
| 0 | `/Script/MMORPG.MMOAttackAbility` | 1 | -1 | `InputTag.MMO.Attack` |
| 1 | `GA_ArcaneBolt`（`MMOSpellAbility` 的教学蓝图子类） | 1 | -1 | `InputTag.MMO.Spell` |

两行 **Input ID=-1**（`INDEX_NONE`），主工程不再用数字槽选择能力。`GrantToAbilitySystem` 把有效 **Input Tag** 写入 `FGameplayAbilitySpec.GetDynamicSpecSourceTags()`；运行时按这个精确标签查找已经授予的能力。Input Tag 必须在标签选择器中选择真实注册项，分别与 `DA_MMOInputConfig` 的 Attack、Spell 行一致。给字段填一个名字不会自动创建 InputAction 或绑定输入。

**Granted Effects** 和 **Granted Attributes** 先留空。当前 PlayerState 已构造 `UMMOAttributes`；重复配置相同属性类会产生两份属性实例。新增属性集时再在此列表选择自己真实存在的类，并通过授予句柄清理。Instant GameplayEffect 的瞬时修改不能通过 RemoveActiveGameplayEffect 撤销，不要把它当作卸装可逆效果。

单击 **Save**，关闭后重开，确认两行类、Input ID=-1 和不同的 Input Tag 保留。数据资产没有 Blueprint Compile 按钮。若自己新建了能力蓝图，应先在能力蓝图 Compile → 检查 Compiler Results → Save，再将其类填入此处。

### 2. DA_MMOPlayer

在 `MMO/Pawns` 创建 **MMOPawnData**，命名 **DA_MMOPlayer**。Details 中：

1. **Pawn Class** 选择主工程 `MMOCharacter`，或已经编译成功的教学角色蓝图类；必须与实际角色初始化组件兼容。
2. 展开 **Ability Sets**，添加一行并选择 `DA_MMOAbilities`。
3. **Input Config** 选择下面创建的 `DA_MMOInputConfig`，**Input Mapping Context** 选择 `IMC_MMOPlayer`。这两个字段只选择并预加载资产，角色的输入组件仍需按标签查找并绑定动作，在本地玩家子系统添加映射，在销毁或切换时移除映射。
4. **Anim Instance Class** 选择与角色 Skeleton 兼容且实际编译通过的 AnimBlueprint 生成类；未完成动画资产时留空，不填想象中的资产路径。角色整合代码负责应用该类。
5. Save，关闭重开，沿 Ability Sets 的放大镜定位 `DA_MMOAbilities`，确认引用确实落在自己工程。

### 3. DA_MMOExperience

在 `MMO/Experiences` 创建 **MMOExperienceDefinition**，命名 **DA_MMOExperience**。Details 中：

1. **Default Pawn Data** 选择 `DA_MMOPlayer`。
2. **Game Features To Enable** 添加字符串 **MMOCore**，拼写必须与实际 `.uplugin` 名相同。MMOCore 的 `.uplugin` 初始状态设为 **Registered**，由 Experience 在运行时显式激活；不要同时保留旧 GameInstance 的自动激活代码。未知名称会记录失败，不能广播 Ready。
3. **Action Sets** 与 **Actions** 可以先为空，前提是 MMOCore 的 GameFeatureData 已提供当前玩法动作。这不表示把主 GameMode 中的所有玩法继续硬编码进去。
4. Save，关闭重开，依次追踪 Experience → PawnData → AbilitySet，确认三条配置链接。

## 需要复用动作时创建 ActionSet

在 `MMO/Experiences` 创建 **MMOExperienceActionSet**，命名 `LAS_MMOComponents`。展开 Actions，选择本机可用的 **Add Components** 动作，在 Component List 的每行明确设置 Actor Class、Component Class、Client Component、Server Component。组件类必须是已经编译或正确加载的真实类。UI 动作应来自新工程实际迁移/实现的模块，不把 LyraGame 的 Add Widgets 类名当成插件自带类型。

Save 后，把此资产加入 `DA_MMOExperience.ActionSets` 再 Save。运行时管理器会复制动作对象给当前世界，传入该世界的 Context Handle，避免多个 PIE 世界共享动作的可变状态；退出按相反顺序停用动作，并等待异步解除句柄完成后释放依赖。

## 创建能在编辑器审查的输入配置

在 `/Game/MMO/Input` 新建输入资产。内容浏览器空白处右键 **Input → Input Action**，依次建立 `IA_Move`、`IA_Look`、`IA_Target`、`IA_Attack`、`IA_Spell`、`IA_Inventory`、`IA_Settings`；Move 和 Look 的 **Value Type** 设为 **Axis2D**，其他五个按钮动作设为 **Digital (bool)**。逐个 Save。用 **Input → Input Mapping Context** 创建 `IMC_MMOPlayer`；在本机 5.8.1 展开 **Default Key Mappings → Mappings**，选择上述真实动作，在动作下添加物理键。

下面 17 行对应主工程原创 Editor 生成器 `MMODocExperienceTools.cpp::ConfigureInputs` 的当前配置；每行的 Modifier 属于该物理键。点击键左侧的小箭头，展开 Modifiers，用加号创建需要的项，选择对应类并展开其字段。Save 后关闭重开确认。

| InputAction | 物理键 | 按键行 Modifiers |
| --- | --- | --- |
| IA_Move | D | 空，正 X |
| IA_Move | A | Negate，X 启用 |
| IA_Move | W | Swizzle Input Axis Values，Order=YXZ |
| IA_Move | S | 先 Negate，再 Swizzle Input Axis Values，Order=YXZ |
| IA_Move | Gamepad Left Thumbstick 2D Axis | 空 |
| IA_Look | Mouse XY 2D Axis | 空 |
| IA_Look | Gamepad Right Thumbstick 2D Axis | 空 |
| IA_Target | Tab | 空 |
| IA_Target | Gamepad Face Button Top | 空 |
| IA_Attack | 1 | 空 |
| IA_Attack | Gamepad Face Button Bottom | 空 |
| IA_Spell | 2 | 空 |
| IA_Spell | Gamepad Face Button Left | 空 |
| IA_Inventory | I | 空 |
| IA_Inventory | Gamepad Special Left | 空 |
| IA_Settings | P | 空 |
| IA_Settings | Gamepad Special Right | 空 |

键盘单键首先产生 X 值，因此 W/S 需要换轴；S 先取负，再换到 Y。手柄二维轴已经有 X/Y，不应再次照搬 W 的 Swizzle。当前生成器没有为这些按钮额外添加 Trigger；角色对 Target/Attack/Spell/Inventory/Settings 绑定 `Started`，对 Move/Look 绑定 `Triggered`。Settings 的打开键是 **P**；CommonUI 的返回操作由 UI 路由管理，不能假定打开菜单与返回使用相同输入。代码配置事实与实际键鼠、手柄验收分别记录，不能由表格推断运行已通过。

在同一文件夹右键 **Miscellaneous → Data Asset**，选择 **MMOInputConfig**，命名 **DA_MMOInputConfig**。展开 **Native Input Actions** 并添加七行：

| Input Action | Input Tag |
| --- | --- |
| IA_Move | InputTag.MMO.Move |
| IA_Look | InputTag.MMO.Look |
| IA_Target | InputTag.MMO.Target |
| IA_Attack | InputTag.MMO.Attack |
| IA_Spell | InputTag.MMO.Spell |
| IA_Inventory | InputTag.MMO.Inventory |
| IA_Settings | InputTag.MMO.Settings |

标签必须先由主工程的 Native Gameplay Tags 或 Gameplay Tags 配置注册，选择器才能选择。避免重复标签；`FindInputActionForTag` 按精确标签查找首个匹配项，不使用父标签匹配，也不临时同步加载资产。Save 后关闭重开，分别用放大镜定位七个 InputAction，确认没有引用另一个实验工程的资产。

最后打开 `DA_MMOPlayer`，选择上述 Input Config 与 Input Mapping Context 并 Save。Experience 管理器在客户端先异步加载配置，再加载其中所有软引用的动作，完成后才能 Ready；专用服务器跳过输入资产。主角色需要在配置加载与输入组件都可用时调用绑定，PawnData 复制比 SetupPlayerInputComponent 晚到时也要重新尝试，不能在第一次空指针返回后永久遗漏绑定。实际键位响应仍需运行验证。

## 在 Asset Manager 注册与 Cook

打开 **Edit → Project Settings → Game → Asset Manager**，展开 Primary Asset Types to Scan。创建以下条目，均 **Has Blueprint Classes=false、Is Editor Only=false、Rules.Cook Rule=Always Cook**：

| Primary Asset Type | Asset Base Class | Directories |
| --- | --- | --- |
| MMOExperience | MMOExperienceDefinition | `/Game/MMO/Experiences` |
| MMOPawnData | MMOPawnData | `/Game/MMO/Pawns` |
| MMOAbilitySet | MMOAbilitySet | `/Game/MMO/Abilities` |
| MMOInputConfig | MMOInputConfig | `/Game/MMO/Input` |
| MMOActionSet | MMOExperienceActionSet | `/Game/MMO/Experiences` |

同一个目录允许不同基类分别扫描。检查 Config/DefaultGame.ini 实际差异，然后重开设置确认。本次选择 ID 是 **`MMOExperience:DA_MMOExperience`**；它不是 `.uasset` 磁盘文件名，也不是 `LyraExperienceDefinition:B_...`。

## 接入 GameState、GameMode 与 PlayerState

主 GameState 派生 `AModularGameStateBase`，构造 `UMMOExperienceManagerComponent` 默认子组件；所有客户端必须有同一个组件类，不能只在服务器临时 NewObject 一个无法对应的组件。主 GameMode 在世界启动时选择上述 ID，并用 `CallOrRegister_OnReady` 注册 UObject/Weak 回调。

玩家生成必须同时满足 **Experience Ready** 和 **后端角色身份/存档已确认**。两条异步流程都调用同一个 `TryStartPlayer` 判断；不要因为其中一条先结束就立即 RestartPlayer。Experience Ready 后，GetPawnData 返回已加载的数据，GameMode 从 `PawnClass.Get()` 选择角色类，无需在登录回调里同步阻塞加载。

PlayerState 用 `CreateDefaultSubobject<UMMOAbilitySystemComponent>` 创建 ASC，随后初始化 ASC Owner（`ASC->InitAbilityActorInfo(this, GetPawn())`，无 Pawn 时 Avatar 可为空），再在服务器按 PawnData 的 AbilitySets 调用 `GrantToAbilitySystem`。每份技能集保存一个 `FMMOGrantedAbilityHandles`；同一份句柄不能被重复授予覆盖。PlayerState 拥有的默认技能跟随玩家生命周期，单次 Pawn 重生不应再增加一份。卸装或替换数据时用原句柄 `TakeFromAbilitySystem` 精确回收。

## 从语义标签到已授予的能力

本轮标签输入实现已于 **2026-09-13 18:42:13 UTC** 随完整 `MMORPGEditor Win64 Development` 目标编译、链接通过，记录为 `verification/mmorpg-tagged-editor-build.json`。**18:46:48 UTC** 原生编辑器保存了既有 `DA_MMOAbilities` 的两行 Input Tag 与 Input ID，且验证其他字段不变，见 `verification/mmorpg-tagged-ability-input-assets.json`。该资产操作没有执行玩法。随后原生 Client/Server 增量构建成功；最终 cooked 双客户端加独立服务器于 **19:26:04 UTC** 完成 **8/8**，每项均记录 `tagDrivenInput=true` 与正确的 `UMMOAbilitySystemComponent` 授予快照，并包含后端重启恢复，见 `verification/mmorpg-packaged-smoke.json`。**19:26:58 UTC** 的死亡等待中退出与恢复 **6/6**、最终渲染验证另见 `mmorpg-packaged-death-logout.json` 与 `mmorpg-packaged-rendered-showcase.json`。这些是修正后真实归档程序的结果，不沿用修正前或 Editor 的通过记录。

汇总 `verification/mmorpg-tagged-ability-input-runtime.json` 保留了各份证据 SHA 与验收范围。上述自动流程从角色技能入口进入，验证 RPC、已授予 Spec 和服务器 ASC 激活；这一轮没有回放物理键鼠或手柄，记录为 `physicalInputTested=false`，不能把标签派发通过当成所有设备按键已验证。

输入链是 **物理键 → InputAction → `DA_MMOInputConfig` 的 InputTag → 角色 RPC → ASC 中已授予的 AbilitySpec → GameplayAbility**。例如 `2` 或手柄左侧面键触发 `IA_Spell`，角色发送 `InputTag.MMO.Spell`；服务器按该标签从 PlayerState 的 ASC 找到由 AbilitySet 授予的 `GA_ArcaneBolt`，再调用 GAS 的 `TryActivateAbility`。技能类、等级和输入标签来自资产，RPC 不接收技能类或数字槽。

`MMOAbilitySystemComponent.h` 提供两个原生 C++ 方法：

| 方法 | 行为 |
| --- | --- |
| `GetGrantedAbilitiesForInputTag(const FGameplayTag&)` | 返回所有精确匹配的 `FMMOGrantedAbilityInput` 值快照，其中包含 `Handle`、`InputID`、`AbilityClassPath`，供诊断；不返回可在数组变动后失效的 Spec 指针 |
| `TryActivateAbilitiesByInputTag(const FGameplayTag&)` | 仅服务器权威可调用；锁内收集所有精确匹配的 Spec 句柄，结束遍历并释放锁后逐个尝试激活；至少一个成功时返回 true |

通用 ASC 允许一个输入标签对应多份已授予能力，不把“恰好一份”或某个 Input ID 写成框架规则。当前教程的两项能力各自只有一份，并把 Input ID 设为 -1；这是本工程的资产与运行验收约束。授权仍属于主玩法：`AMMOCharacter::ServerActivate_Implementation` 只接受当前 Attack/Spell 标签，并检查后端会话、角色初始化、存活状态以及当前 PlayerState/ASC/Avatar 归属；ASC 的查找方法不能替代这些检查。

当前 Attack 与 Spell 是 **ServerOnly 瞬发技能**。本次采用 Lyra 的标签输入装配方式，但没有加入 Lyra 的 Held/Released 队列、每帧 `ProcessAbilityInput` 或本地预测策略。扩展蓄力、持续施法或预测技能时，需要单独设计按下/释放传播、取消与预测窗口；不能由当前两个按键成功推断这些功能已经实现。

在运行前检查两处资产：`DA_MMOInputConfig` 决定动作对应的语义标签，`DA_MMOAbilities` 决定哪个已授予技能响应该标签。两处拼写不一致或 AbilitySet 未授予时，不能找到可激活的 Spec；应追踪 Ready、默认授予句柄和标签，不能为“修好输入”在 Pawn 上再授予第二份能力。

## 接入角色与初始化链

`AMMOCharacter` 构造 `UMMOPawnExtensionComponent` 默认子组件。服务器将选定 PawnData 交给 `SetPawnData`，组件复制此引用。接入顺序与事件如下：

| 主类事件 | 组件调用 |
| --- | --- |
| BeginPlay，Super 之前 | 订阅 `OnDataAvailable`，回调同一个幂等 `TryInitializeInput` |
| PossessedBy | Super 后 `InitializeAbilitySystem(PS->ASC, PS)`，再 `HandleControllerChanged()` |
| OnRep_PlayerState | Super 后按当前 PS 重新绑定 ASC，再 `HandlePlayerStateReplicated()` |
| SetupPlayerInputComponent | 完成实际 Enhanced Input 绑定后 `SetupPlayerInputComponent()` |
| UnPossessed、EndPlay | `UninitializeAbilitySystem()`；同时保留主类自己的输入上下文和委托清理 |

状态通过真正的 ModularGameplay 管理器注册，顺序为 Spawned → DataAvailable → DataInitialized → GameplayReady。本地玩家必须完成输入准备，服务器/本地 Pawn 必须有 Controller，ASC Avatar 必须指向当前 Pawn。远端模拟 Pawn 不要求具备本地输入组件。

`OnDataAvailable` 在 `HandleChangeInitState` 进入 DataAvailable 时广播。回调完成真实输入绑定后调用组件的 `SetupPlayerInputComponent` 设置输入已准备标记，外层初始化链随后继续推进。回调时状态管理器尚未记录这个新状态，因此回调不要再次要求 `HasReachedInitState(DataAvailable)` 才处理输入；可以直接检查 Experience Ready、当前 PawnData 与 InputComponent。`OnGameplayReady` 则在整条链推进完成、管理器已经记录状态后广播。

当复制的 Pawn 比 GameState/Experience 先到时，管理器 Ready 会检查当前世界已有的 PawnExtension；Pawn 后到时由 BeginPlay、OnRep 和输入事件继续推进。没有固定 Delay 或每帧轮询。一个旧 Pawn 销毁时仅能解除仍以自身为 Avatar 的 ASC，不能清空已绑定到新 Pawn 的 Avatar。

## 复现验证与范围

首先编译 Editor，再创建保存上述真实资产并截取每种 Data Asset 的 Details。运行时记录 SelectedExperienceId、LoadState、PawnData 路径、InitState、ASC Owner/Avatar 与默认技能数量。测试正常进入、资源延迟、错误 GF 名、后端拒绝、两个客户端、重生和正常退出。本轮已有实际 Editor/Client/Server 构建、归档双客户端、重生、退出恢复和渲染记录，以上文列出的报告为准。资源延迟、错误 GF 名等反例仍应按此清单单独验证，不能从正常路径通过推断全部失败路径已覆盖。
