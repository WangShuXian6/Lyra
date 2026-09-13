'use client';

import { RootProvider } from 'fumadocs-ui/provider/next';
import type { ReactNode } from 'react';
import { assetUrl } from '@/lib/site';
import { SiteLink } from '@/components/site-link';
import searchConfig from '@/lib/search-config.json';

export function Providers({ children }: { children: ReactNode }) {
  return <RootProvider
    components={{ Link: SiteLink }}
    theme={{ defaultTheme: 'dark', enableSystem: true }}
    search={{ options: { type: 'static', api: assetUrl(searchConfig.indexPath) } }}
    i18n={{ locale: 'zh-CN', translations: {
      // Fumadocs 16.15 / @fuma-translate 1.0 keys include the component's notes.
      'Search(search trigger)': '搜索课程与源码',
      'Search(search dialog)': '搜索课程与源码',
      'Open Search(search trigger)(aria-label)': '打开搜索',
      'Close Search(search dialog)(aria-label)': '关闭搜索',
      'No results found(search dialog)': '没有找到结果',
      'On this page(table of contents)': '本页内容',
      'Table of Contents(inline table of contents)': '本页目录',
      'No Headings(table of contents)': '本页暂无标题',
      'Last updated on(page footer)': '更新于',
      'Next Page(pagination)': '下一篇', 'Previous Page(pagination)': '上一篇',
      'Copy Text(code block)(aria-label)': '复制代码',
      'Copied Text(code block)(aria-label)': '已复制代码',
      'Copy Anchor Link(heading anchor)(aria-label)': '复制章节链接',
      'Edit on GitHub(edit page)': '在 GitHub 编辑',
      'Toggle Theme(theme switcher)(aria-label)': '切换主题',
      'Light(theme switcher)(aria-label)': '浅色', 'Dark(theme switcher)(aria-label)': '深色',
      'System(theme switcher)(aria-label)': '跟随系统',
      'Open Sidebar(sidebar)(aria-label)': '打开目录',
      'Close Sidebar(sidebar)(aria-label)': '关闭目录',
      'Collapse Sidebar(sidebar)(aria-label)': '折叠目录',
    } }}
  >{children}</RootProvider>;
}
