# Lyra 中文实战手册

基于本机 UE 5.8.1 与官方 Lyra Starter Game 源码，提供从第一次运行到射击训练场、空白 C++ MMORPG、CommonUI/MVVM 与 UE Program 持久化后端的学习路线。

MMORPG 使用 UE 模板 Manny、可编辑的 Widget Blueprint Designer、事件驱动的 ViewModel，以及英文/简体中文字符串表和完整本地化流水线。教程保留 C++、原生蓝图文本、配置截图和实际运行证据。

在线阅读：[wangshuxian6.github.io/Lyra](https://wangshuxian6.github.io/Lyra/)。已完成 [GitHub Pages 自动部署](https://github.com/WangShuXian6/Lyra/actions/runs/34783791968)及真实线上浏览器验收：中文与 C++ 搜索、深层页面刷新、原图放大、移动端和两个下载包的 SHA-256 共 12 项通过。完整证据见 [发布记录](verification/github-pages-deployment.json)与[交付验收](verification/release-audit.json)。

## 文档站

需要 Node.js 24.13.0、pnpm 10.28.2。所有 npm 包版本和 pnpm lockfile 固定。下列命令从文档仓库执行；本地教学脚本使用 Windows 上的 PowerShell 7，发布文件审计需要 7.4 或更新版本。

```powershell
Set-Location F:/UE/LyraDoc
pnpm install --frozen-lockfile
pnpm dev
```

打开 `http://127.0.0.1:3000`。生产构建与 GitHub Pages 子路径检查：

```powershell
$env:NEXT_PUBLIC_BASE_PATH='/Lyra'
pnpm check
pnpm build
node scripts/check-content.mjs --export
pnpm serve
```

根路径构建时清除 `NEXT_PUBLIC_BASE_PATH`。`out/` 为完整静态产物。搜索索引在构建时生成，在浏览器中搜索；Pages 不运行 Node 服务或游戏后端。

## 配套工程

`examples/LyraTraining` 提供 Game Feature 增量和 Unreal 原生资产复制脚本；`examples/MMORPG` 是独立 C++ 工程。官方基础插件由 `dependencies.json` 列明，从自己获取的 Lyra 安装中复制。

```powershell
./scripts/prepare-labs.ps1
./scripts/set-preview-quality.ps1 -ProjectRoot F:/UE/LyraDocLabs/LyraTraining
./scripts/set-preview-quality.ps1 -ProjectRoot F:/UE/LyraDocLabs/MMORPG
```

执行复制和画质配置前，先保存并关闭目标实验工程的 Editor/PIE。实验工程创建于 `F:/UE/LyraDocLabs`，引擎默认为源码版 `E:/UnrealEngine`。这些命令只复制工程、依赖和教学增量并写入预览配置，**不会自动完成 C++ 构建、训练资产生成、数据库初始化或启动游戏**。下一步按[训练场步骤](content/docs/07-training/feature-and-experience.mdx)或[MMORPG 迁移步骤](content/docs/08-mmorpg/migration.mdx)继续。首次复制后需要完整 Editor 目标构建，不能把已有 DLL 当成与当前引擎匹配的证明。

预览目标为 1920 × 1080、中等画质；配置写入成功还需在实际游戏视口核对尺寸。UE 资产生成和运行需要本机引擎，网站 CI 不需要。可以换盘，但所有路径应同步调整；训练生成脚本仍要求隔离目录结尾为 `LyraDocLabs/LyraTraining`。

后端模块、迁移和接口见 `examples/MMORPG/backend`、`examples/MMORPG/Source/MMO*` 与 `scripts/backend`。凭据由初始化脚本生成并保存在忽略目录，客户端只访问 HTTP 和游戏服务器。

配置、生成、编译、联机与打包的逐步操作从网站两条学习路线进入；本地验证结果保存在 `verification/`。逻辑备份恢复可用 `scripts/backend/Test-BackupRestore.ps1` 在自动创建的独立数据库中复验。

## 真实素材与证据

- `public/blueprints/`：Unreal 原生节点复制文本，保留父类、变量和资产依赖。
- `public/media/`：本机 Unreal 原始图像；蓝图采用插件 1:1 导出，可在网页按原始尺寸查看。
- `source-evidence/`：源码、符号、资产和媒体来源及 SHA-256。
- `verification/`：实际构建和运行记录。测试代码存在不代表对应验收已经通过。
- `IMPLEMENTATION.md`：实施范围与交付状态。

核对验证范围时，训练场 PIE 的输入、联机和生命周期分别见 `training-play.json`、`training-network.json`、`training-lifecycle.json`；它们不能替代 `training-package-runtime.json` 的归档程序检查。MMORPG 的 Editor `-game` 运行与最终 cooked Client/Server 也使用独立报告。报告不存在、标记 pending 或通过前置编译，都不能写成 packaged 运行已通过。

根目录 `.gitattributes` 保留文件的原始换行字节，供来源清单与 CI 按 SHA-256 核对。不要对原生蓝图节点文本、PO 翻译或已取证源文件执行自动换行转换、格式化或编辑器“全部保存为另一编码”；看起来相同的文本也可能产生不同字节。需要更新时先由对应原生导出或本地化流程重建，再更新相关来源清单与实际验证，不能只改哈希掩盖差异。

## 发布

`.github/workflows/pages.yml` 在 PR 中检查根路径和 `/Lyra` 两套静态导出。推送到 `main` 或合并 PR 导致 `main` 更新后自动发布 GitHub Pages；也可手动触发。仓库已启用 GitHub Actions 发布源与 HTTPS；两套 Ubuntu 检查均已通过，包括重新生成示例下载包并逐字节比较。

部署失败时查看相应 workflow 的失败步骤。回退使用新建 revert 提交，保留部署历史。不要把本地 UE 缓存、数据库、密钥、第三方私有库或官方基础资源提交到此仓库。

项目内容来自官方源码分析及本地教学扩展，来源区分见 [NOTICE.md](NOTICE.md)。
