# LyraDocTools 教学 Editor 插件

本插件补足本机 UE 5.8.1 Python 包装层无法完成的两类操作：直接修改 Lyra AbilitySet 内的 `EditDefaultsOnly` 结构数组元素，以及导入原生蓝图节点文本并取得真实编译结果。仅包含原创教学代码，不包含 Epic 资产或第三方截图插件。

源码 API 已按本机 `E:/UnrealEngine` 与 `F:/UE/LyraStarterGame` 核对；完整 `LyraEditor` 首次构建已通过。Python 调用、蓝图导入编译与游戏运行分别保留实验工程证据，不能把构建成功当成全部运行通过。

## 安装位置与调用限制

将整个 `LyraDocTools` 文件夹复制到 `F:/UE/LyraDocLabs/LyraTraining/Plugins/LyraDocTools`。这是普通 `Editor` 插件，默认启用，依赖现有项目模块 `LyraGame` 和引擎的 GameFeatures、GameplayAbilities。不要放进官方分析基准工程。按项目构建流程保留未保存工作、正常关闭所有使用同一引擎且存在 Live Coding 会话的编辑器，再构建完整 `LyraEditor Win64 Development` 目标并重新打开训练场。

所有修改方法同时校验当前工程目录以 `/LyraDocLabs/LyraTraining` 结尾、资产包位于 `/TrainingRange/`、调用发生于游戏线程。`round_trip_blueprint` 进一步要求 `/TrainingRange/Tests/`，拒绝官方 `/Game/`、`/ShooterCore/` 资产以及实际玩法使用的 `/TrainingRange/Game/` 资产。插件不自动保存资产。

启用后的 Python 类名为 `unreal.LyraDocToolsLibrary`，模块只参与 Editor 构建，不进入 Client 或 Server。

## 1. 替换 AbilitySet 中的 Dash

```python
import unreal

lib = unreal.EditorAssetLibrary
abilities = lib.load_asset('/TrainingRange/Game/AbilitySet_TrainingHero')
old_class = lib.load_blueprint_class('/ShooterCore/Game/Dash/GA_Hero_Dash')
new_class = lib.load_blueprint_class('/TrainingRange/Game/GA_TrainingDash')
matched = unreal.LyraDocToolsLibrary.replace_ability_in_set(abilities, old_class, new_class)
if matched != 1:
    raise RuntimeError(f'Expected one Dash grant; got {matched}')
if not lib.save_loaded_asset(abilities):
    raise RuntimeError('AbilitySet save failed')
```

返回值是“当前为 Old，或已经为 New”的项目数量；重跑仍返回 1，且不会重复追加能力。`-1` 表示输入、路径或反射结构校验失败，0 表示没有匹配项。`New` 必须是 `/TrainingRange/` 内的能力类。只替换类指针，保留原 `AbilityLevel`、`InputTag` 和其余数组项。

原生实现用 `FArrayProperty` 找到受保护的 `GrantedGameplayAbilities`，用 `FStructProperty` 取得数组元素结构，再用 `FClassProperty` 读写元素内的 `Ability`。数据仍位于所属资产的数组内，绕过的是 Python 对分离结构值的编辑限制，不需要改 Lyra 类的访问修饰符。调用使用事务及编辑通知，可在编辑器 Undo。

## 2. 配置 Experience 和地图扫描

```python
feature = lib.load_asset('/TrainingRange/TrainingRange')
if not unreal.LyraDocToolsLibrary.configure_training_feature(feature):
    raise RuntimeError('Feature scan configuration failed')
if not lib.save_loaded_asset(feature):
    raise RuntimeError('GameFeatureData save failed')
```

方法使用本机 `FPrimaryAssetTypeInfo` 六参数构造函数写入：

| Primary Asset 类型 | 基类 | 扫描目录 | Blueprint Classes | Cook Rule |
| --- | --- | --- | --- | --- |
| `LyraExperienceDefinition` | `ULyraExperienceDefinition` | `/TrainingRange/Experiences` | true | AlwaysCook |
| `Map` | `UWorld` | `/TrainingRange/Maps` | false | AlwaysCook |

重跑替换这两个类型的规则，保留其他类型与 Game Feature Actions。此调用配置资产，不负责激活 Game Feature；保存后按照训练场章节重新注册或重启编辑器，使 Asset Manager 应用扫描配置。

地图的 `ALyraWorldSettings::DefaultGameplayExperience` 也是 `EditDefaultsOnly`，本机 Python 不允许对地图实例直接 `set_editor_property`。生成脚本改用原生接口：

```python
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
experience = lib.load_blueprint_class('/TrainingRange/Experiences/B_TrainingRange')
if not unreal.LyraDocToolsLibrary.configure_training_world(world, experience):
    raise RuntimeError('Training world configuration failed')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
```

它只接受训练工程下 `/TrainingRange/Maps/` 的编辑器世界和 `/TrainingRange/Experiences/` 的 Experience 类，通过 `FSoftClassProperty` 修改地图自己的设置，支持事务与编辑通知，拒绝运行中的游戏世界。

## 3. 完整蓝图复制文本导入与编译

先复制完整蓝图，保留父类、成员变量、其他函数及局部变量签名，再仅替换其中一张图。不能在空白 Actor 蓝图里粘贴能力节点并宣称相同依赖条件。

```python
import json
import unreal

lib = unreal.EditorAssetLibrary
cases = [
    ('/Game/Characters/Heroes/Abilities/GA_Hero_Jump',
     '/TrainingRange/Tests/GA_JumpClipboardTest', 'EventGraph',
     'F:/UE/LyraDoc/public/blueprints/lyra/jump-event-graph.txt'),
    ('/ShooterCore/Game/Dash/GA_Hero_Dash',
     '/TrainingRange/Tests/GA_DashClipboardTest', 'SelectDirectionalMontage',
     'F:/UE/LyraDoc/public/blueprints/lyra/dash-direction.txt'),
]
reports = []
for source, destination, graph_name, text_path in cases:
    # 初次运行创建完整副本。失败后建议丢弃该测试副本，再从原蓝图复制。
    test_copy = (lib.load_asset(destination) if lib.does_asset_exist(destination)
                 else lib.duplicate_asset(source, destination))
    result = json.loads(unreal.LyraDocToolsLibrary.round_trip_blueprint(
        test_copy, graph_name, text_path))
    reports.append(result)
    if not result['passed']:
        raise RuntimeError(json.dumps(result, ensure_ascii=False))
    # 需要保留测试资产时，只有 passed 后才显式保存。
    if not lib.save_loaded_asset(test_copy):
        raise RuntimeError('Test asset save failed: ' + destination)
print(json.dumps(reports, ensure_ascii=False, indent=2))
```

方法只接受 `EventGraph` 或 `SelectDirectionalMontage`。先检查文本可导入，再清除测试副本图内节点，通过 `FEdGraphUtilities::ImportNodesFromText` 导入。它保留图对象及 `GraphGuid`，保留蓝图成员定义；函数图的输入/返回引脚类型、名称和方向必须与副本原签名一致。原生导入器负责引用修复和 `PostPasteNode`，之后使用 `FKismetEditorUtilities::CompileBlueprint(..., SkipSave, &Results)` 收集编译日志。

结果为 JSON，包含 `passed`、`nodeCount`、`originalNodeCount`、`importedNodeCount`、`signaturePreserved`、`compileStatus`、`compilerErrorCount`、`errors`、`warnings`、`compiledThisCall`。只有节点数量一致、签名一致且编译成功时才通过。完整节点文本必须包含函数入口、返回节点和所需局部变量；只导入截图中的局部选区不会通过完整图核验。

失败会保留测试副本的当前图供查看错误，`SkipSave` 阻止编译器自动保存；可以 Undo 或重新复制测试蓝图。不要对失败副本调用保存，也不要将它替换为实际玩法资产。成功表示该图在完整 Lyra 依赖下可导入并编译，不代表整段技能已经运行验证。

## 4. 导出原生节点文本

```python
source = lib.load_asset('/ShooterCore/Game/Dash/GA_Hero_Dash')
report = json.loads(unreal.LyraDocToolsLibrary.export_blueprint_graph(
    source, 'SelectDirectionalMontage',
    'F:/UE/LyraDocLabs/LyraTraining/Saved/Evidence/dash-direction.txt'))
print(json.dumps(report, ensure_ascii=False, indent=2))
```

导出读取整张图并调用 `FEdGraphUtilities::ExportNodesToText`，以 UTF-8 写入明确的绝对 `.txt` 路径。它不会编译、保存、标脏或调整原蓝图的节点所有权；这里面向 Jump/Dash 使用的常规 K2 图，不是通用嵌套资源复制工具。`compileStatus` 是源蓝图已有状态，`compiledThisCall` 固定 false；`passed` 仅代表文件导出成功。要取得本次粘贴后的编译证明，必须执行上一节测试副本流程。

## 本机 API 依据

### Commandlet 中构建训练导航

`unreal.LyraDocToolsLibrary.build_training_navigation(world)` 仅接受隔离 Training 工程内 `/TrainingRange/Maps/` 的非游戏 World。它等待异步加载和资产编译完成，确认没有剩余资产编译后，仅释放该 World 的 `AsyncLoadLock`，保留并拒绝其他阻止导航构建的锁，然后调用同步的 `UNavigationSystemV1::Build()`。返回 true 后仍需逐个调用原生导航投影核实三个已有拾取点，再保存当前地图；prepare 脚本已经包含这些步骤。

不要在 Python commandlet 中调用 `BUILDPATHS`：本机 UE 5.8.1 的图形编辑器包装 `FEditorBuildUtils::EditorBuild()` 会访问进度窗口，在该环境中产生原生异常。图形编辑器的手动 **Build → Build Paths** 仍可正常使用。导航数据改变后需要重新 Cook；这一 helper 不负责保存、启动 PIE 或宣布完整玩法通过。

- `LyraGame/AbilitySystem/LyraAbilitySet.h`：`GrantedGameplayAbilities`、`FLyraAbilitySet_GameplayAbility::Ability`。
- `CoreUObject/Public/UObject/UnrealType.h`：`FScriptArrayHelper`、`FClassProperty`、对象属性容器读写。
- `Engine/Classes/Engine/AssetManagerTypes.h`：`FPrimaryAssetTypeInfo` 的目录成员为 private，使用原生构造函数。
- `GameFeatures/Public/GameFeatureData.h`：Editor 下可变的 `GetPrimaryAssetTypesToScan()`。
- `UnrealEd/Public/EdGraphUtilities.h`：节点文本导入/导出及可导入检查。
- `UnrealEd/Public/Kismet2/KismetEditorUtilities.h`、`CompilerResultsLog.h`：`CompileBlueprint`、`SkipSave`、编译错误及警告计数。
- `NavigationSystem/Public/NavigationSystem.h`、`Private/NavigationSystem.cpp`：`AsyncLoadLock`、`RemoveNavigationBuildLock` 与内部等待 `EnsureBuildCompletion` 的 `Build()`。
- `Engine/Public/AssetCompilingManager.h`：`FinishAllCompilation()`、`GetNumRemainingAssets()`。

本目录不负责构建或控制编辑器进程；编译与运行结果由实验工程验收记录单独保存。
