# 内容来源

本项目是 Lyra 中文教学材料，与 Epic Games 官方产品文档相互独立。

官方 Lyra 源码、蓝图、地图及美术资产来源于用户本机的 Lyra Starter Game。教程保留相关类名、路径和必要的代码说明，截图与原生节点文本标注原始资产。官方基础内容由使用者通过 Epic 获取，仓库中的复制脚本只在本地读取安装目录。

`CommonGame`、`CommonUser`、`ModularGameplayActors`、`UIExtension` 等官方基础插件按清单在本机准备。它们保留原始声明及适用条款。仓库不把这些内容改称自制系统。

本机蓝图截图工具为 Gradess Games 的 Blueprint Screenshot Tool 1.1.7。它与 Fab 链接中的 YeHaike Blueprint Graph Screenshot 为不同产品。UE 5.8.1 兼容性修改记录和最小补丁位于 `scripts/patches/`；不随教程分发该插件完整源代码或预编译二进制。

MMORPG 玩法、UE 后端模块、SQL、站点与教学脚本属于本项目新增内容。PostgreSQL/libpq 与 libsodium 的版本、下载校验及第三方声明见后端依赖文档。客户端构建不需要这些数据库或密码计算库。

真实截图没有使用生成式图像替代。`source-evidence/` 记录所用本地资产、源码及文件哈希；`verification/` 单独记录实际运行结果。
