"""Render complete, copyable original tutorial files as MDX; --check verifies drift."""
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
groups = json.loads((ROOT/'curriculum/mmorpg/source-book.json').read_text(encoding='utf-8'))['groups']
records = []
errors = []
stages = json.loads((ROOT/'curriculum/mmorpg/stages.json').read_text(encoding='utf-8'))['stages']
required_sources = {item['source'] for stage in stages for item in stage['files']}
book_sources = {item['source'] for group in groups for item in group['files']}
if missing := required_sources - book_sources:
    raise ValueError('Phase files missing from complete source book: ' + ', '.join(sorted(missing)))
languages = {'.h':'cpp','.cpp':'cpp','.cs':'csharp','.ini':'ini','.json':'json','.uplugin':'json','.uproject':'json','.py':'python','.sql':'sql'}
for group in groups:
    slug = '08-mmorpg/source/' + group['id']
    stage = {'skeleton':'00', 'framework':'01', 'http':'02'}.get(group['id'], '03')
    body = f'''---
title: {group['title']}：完整文件
description: 按目标路径新建或替换文件，复制完整实现，再与逐步操作课程对照。
---

{group['intro']}

[返回对应操作与解释]({group['lesson']}) · [返回制作主线](/docs/08-mmorpg/from-zero)

![完整文件怎样变成可以运行的游戏](/media/guide-diagrams/08-mmorpg-source-workbook.svg)

## 复制之前先确认位置

下列路径均相对你的 **MMORPG 工程根目录**。在 Rider 的文件视图中右键父目录 → New → File，输入完整文件名（包括扩展名）；文件已存在时替换整个文件，不能把第二套类声明追加到末尾。页面给出的是当前教程的完整文件，代码框右上角可以复制，不含省略号占位实现。

先写 `.h` 再写 `.cpp` 便于阅读；**完成当前源码阶段的全部文件后才编译**。`*.generated.h` 由 UnrealHeaderTool 生成，不手工创建。`#include`、模块导出宏和函数签名都要保留。一个文件中出现多个类时，复制整个文件一次即可。

按 [四个源码阶段](/docs/08-mmorpg/from-zero) 使用这些文件。本组从 **{stage} 阶段**开始使用。00–02 阶段保留当阶段的轻量 `.uproject` 和 Build.cs；03 阶段再切换到完整项目依赖。

## 本组文件清单

| 文件 | 行数 | 操作 |
|---|---:|---|
'''
    loaded=[]
    for item in group['files']:
        path=(ROOT/item['source']).resolve()
        if not any(path.is_relative_to((ROOT/folder).resolve()) for folder in ('examples/MMORPG','curriculum/mmorpg')): raise ValueError('Only original example or lesson sources belong in this book')
        raw=path.read_bytes(); code=raw.decode('utf-8-sig').replace('\r\n','\n').rstrip('\n')
        if '```' in code: raise ValueError(f'Fence collision: {path}')
        loaded.append((item,raw,code))
        body += f"| `{item['target']}` | {len(code.splitlines())} | 新建；已有同名文件则整体替换 |\n"
    for number,(item,raw,code) in enumerate(loaded,1):
        lang=languages.get(Path(item['source']).suffix,'text')
        body += f"\n## 文件 {number}：{Path(item['target']).name}\n\n目标路径：`{item['target']}`。原文件位于包内 `{item['source']}`，不是需要修改的引擎文件。\n\n<details>\n<summary>展开完整文件并复制</summary>\n\n```{lang} title=\"{item['target']}\"\n{code}\n```\n\n</details>\n"
        records.append({'chapter':slug,'source':item['source'],'target':item['target'],'sha256':hashlib.sha256(raw).hexdigest(),'lines':len(code.splitlines()),'renderedSHA256':hashlib.sha256(code.encode()).hexdigest()})
    body = body.replace('## 本组文件清单', '## 逐段理解与资产连接\n\n' + group.get('reading', '') + '\n\n## 本组文件清单', 1)
    body += f'\n## 写完后怎样确认\n\n保存全部文件，回到当前阶段执行文件核对和 Editor 构建。文件核对只能发现缺文件、复制不完整和版本差异；`Result: Succeeded` 才说明编译成功。资产、UI 与联机行为继续按 [对应课程]({group["lesson"]}) 验收。\n'
    target=ROOT/'content/docs'/(slug+'.mdx')
    if args.check:
        if not target.exists() or target.read_text(encoding='utf-8')!=body:errors.append(str(target.relative_to(ROOT)))
    else:
        target.parent.mkdir(parents=True,exist_ok=True);target.write_text(body,encoding='utf-8',newline='\n')
evidence={'schemaVersion':1,'scope':'Complete original tutorial files rendered without omitted implementation; external Epic plugins and template assets are obtained locally.','files':records}
manifest=ROOT/'source-evidence/copy-course-source-book.json'
serialized=json.dumps(evidence,ensure_ascii=False,indent=2)+'\n'
if args.check:
    if not manifest.exists() or manifest.read_text(encoding='utf-8')!=serialized:errors.append(str(manifest.relative_to(ROOT)))
else:manifest.write_text(serialized,encoding='utf-8')
print(f'{len(groups)} source chapters, {len(records)} complete files, {sum(r["lines"] for r in records)} lines.')
if errors:raise SystemExit('Source workbook drift: '+', '.join(errors))
