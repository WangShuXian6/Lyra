# BlueprintScreenshotTool 1.1.7 → UE 5.8.1

适用于 Gradess Games 插件的本机源码版本。先检查差异，再在自己的插件副本中应用 `blueprint-screenshot-ue58.patch`。补丁调整两个 Slate 视图位置变量的精度类型，并将插件兼容版本声明为 `5.8.0`。本机实际运行引擎为 5.8.1，但插件检查使用的 `FEngineVersion::CompatibleWith()` 返回 `5.8.0-0+UE5`；声明为 5.8.1 会在无人值守运行时被跳过。

保存并正常关闭使用同一源引擎的编辑器与 Live Coding 会话，然后构建完整 Editor Target：

```powershell
& E:/UnrealEngine/Engine/Build/BatchFiles/Build.bat `
  LyraEditor Win64 Development `
  F:/UE/LyraStarterGame/LyraStarterGame.uproject `
  -WaitMutex -NoHotReloadFromIDE -EnablePlugin=BlueprintScreenshotTool
```

只修改 EngineVersion 不能转换旧 DLL 的 ABI。只使用 `-Module=BlueprintScreenshotTool` 可以编译源代码，但不会完成整个目标的模块清单更新。本机之后执行完整目标，由 UBT 写入匹配引擎的 `.modules` BuildId；不要手工伪造 BuildId。

运行时可传入 `-EnablePlugins=BlueprintScreenshotTool -LiveCoding=false`。注意运行时参数为复数 `EnablePlugins`，UBT 编译参数为单数 `EnablePlugin`。

本机验证：UE 5.8.1 构建成功、四处 Slate 弃用警告消除；重启后工具栏按钮出现，设置 ZoomAmount=1，选择图内节点后 Ctrl+F7 成功导出完整 PNG。详细尺寸及哈希见 `verification/blueprint-screenshot.json`。
