# MMODocTools：原创 Designer、动画与玩法数据的原生生成

原创 Editor 插件，供 `F:/UE/LyraDocLabs/MMORPG` 实验工程使用。依赖客户端 HTTP 桥 `MMOBackendClient`、原创 `MMOFramework`、CommonUI、MVVM 与 UE 编辑器模块；不依赖 LyraGame、数据库、服务器服务密钥或 MMOPersistence。客户端和服务器目标不包含这个 Editor 模块。

在 MMORPG `.uproject` 的 Plugins 中启用：

```json
{"Name":"MMODocTools","Enabled":true,"TargetAllowList":["Editor"]}
```

复制插件后先按工程约定构建完整 `MMORPGEditor Win64 Development`；不能只复制源码便调用 Python 反射类。此目录不负责执行 UBT、关闭编辑器或修改 `.uproject`。

## 完整工程生成入口

同步工程根目录的 `prepare_content.py`、`ui-designer-contract.json`、`ui-designer-strings.json` 和本插件，再从独立实验工程运行 `prepare_content.py`。脚本依次执行：

1. `MMOAnimationTools.PrepareCharacterAnimation` 创建原创动画接口、动画层与角色 AnimGraph，编译并验证原生文本往返；官方 Manny、骨架、BlendSpace 和动作先由本地复制脚本准备。
2. `MMOAnimationTools.ConfigureCharacter` 设置 BP_MMOCharacter 的继承 Mesh 默认值，编译后核对实际 SkeletalMesh/AnimClass 留存。
3. `MMODocExperienceTools.ConfigureExperienceAssets` 配置 Experience → PawnData → AbilitySet 的真实 DataAsset 连接。
4. `ConfigureDesignerStrings` 创建 `/Game/Localization/ST_MMO` 的 MMOUI 命名空间和 45 条英文源文案；随后调用者保存资产。
5. `ConfigureDesignerWidget` 为契约中的七个 Widget Blueprint 创建实际 Designer 层级，再添加 Manual PlayerVM 的真实 MVVM 绑定并编译。HUD 含五条绑定，嵌套 ManaStatus 含两条，Inventory 含一条；控件名称必须符合 BindWidget，Canvas 数量为零。
6. 配置 BP_MMOUIPolicy 使用 WBP_MMOLayout，保存 GameFeatureData，再生成下面的后端健康节点图。

这些 `ConstructWidget` 调用仅在编辑器编写资产时运行。打包游戏加载已保存的 WBP；其 C++ 父类提供业务、CommonUI 层栈及 ViewModel 生命周期，Designer 控制布局和样式。重跑生成器会重建指定教学资产的控件树和绑定，应先保存自行修改的副本。

`Saved/Evidence/ui-designer-assets.json` 汇总每个 WBP 的实际控件、绑定数量、编译错误与保存结果。单个报告为 `WBP_<名称>-designer.json`；动画、角色与 Experience 有各自的独立报告。生成成功不代表游戏内输入、语言切换、动画或存档已经验证；这些行为由运行报告分别证明。中英文资源还需执行 `scripts/mmorpg/Build-Localization.ps1`。

## Python 调用

```python
import json
import unreal

path = '/Game/Tutorial/BP_BackendHealth'
library = unreal.EditorAssetLibrary
asset = library.load_asset(path)
if not asset:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.Actor)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'BP_BackendHealth', '/Game/Tutorial', unreal.Blueprint, factory)
result = json.loads(unreal.MMODocToolsLibrary.configure_backend_tutorial(asset))
if not result['passed']:
    raise RuntimeError(json.dumps(result, ensure_ascii=False))
if not library.save_loaded_asset(asset):
    raise RuntimeError('Source tutorial Blueprint save failed')
test_asset = library.load_asset(result['testAsset'])
if not test_asset or not library.save_loaded_asset(test_asset):
    raise RuntimeError('Round-trip test Blueprint save failed')
print(json.dumps(result, ensure_ascii=False, indent=2))
```

生成方法严格校验实验工程路径及资产包 `/Game/Tutorial/BP_BackendHealth`，要求其父类为 Actor。该资产专门用于本教程，重复调用会重建它的 EventGraph；不在里面保留自行添加的玩法逻辑。测试副本采用 `/Game/Tutorial/Tests/` 下的唯一名称，不覆盖已有验证资产。

## 生成的四个真实节点

1. `Event BeginPlay`：Actor 的 `ReceiveBeginPlay` 覆盖事件。
2. `Request MMO`：`UK2Node_AsyncAction::InitializeProxyFromFunction` 绑定 `UMMORequestAction::RequestMMO`；Verb=`GET`、Route=`/health`、JsonBody=`{}`。
3. `Print String`：执行输入连接异步节点的 **Completed** 输出，`InString` 连接回调的 `Json` 输出。不会把发出请求的 `then` 当作 HTTP 已完成。
4. `Self`：分别连接两个节点的 WorldContextObject，引擎可能将这些上下文引脚隐藏显示。

所有连接由 `UEdGraphSchema_K2::TryCreateConnection` 创建并检查返回值。原生编译器使用 `SkipSave` 编译，编译成功后从实际图导出全部原生节点文本至 `Saved/Evidence/backend-health.txt`。随后复制完整蓝图，清除测试图节点，从写出的文件导入并再次编译，核对节点数量，输出 `Saved/Evidence/backend-health-roundtrip.json`。

JSON 包含源图和重导副本两次编译的状态、错误、警告、节点数、实际测试资产路径及导出文件路径。插件不自动保存资产，调用方应在 `passed=true` 后显式保存。失败的测试副本只留在内存中供排障；可以 Undo 或重新生成。

`httpExecuted=false` 表示这里验证的是节点构造、复制文本和编译。将该 Actor 放入运行中的实验地图、启动后端并 PIE，观察 Print String 输出，才完成 HTTP 行为验证。`Print String` 是开发调试节点，不用于 Shipping 游戏的正式错误界面。

## API 与验证范围

源码依据本机 UE 5.8.1 的 `K2Node_AsyncAction.h`、`K2Node_BaseAsyncTask.cpp`、`EdGraphSchema_K2.h`、`EdGraphUtilities.h`、`KismetEditorUtilities.h`、`IAssetTools.h`、`WidgetBlueprint.h`、`MVVMBlueprintView.h` 与动画图编辑器 API。`verification/backend-health-roundtrip.json` 记录健康图的四节点真实导出、导入与编译；新增 Designer 和动画的结果必须检查各自报告，不能沿用健康图的通过状态。
