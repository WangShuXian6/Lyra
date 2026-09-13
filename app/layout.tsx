import type { Metadata } from 'next';
import { Providers } from '@/components/providers';
import { basePath, siteOrigin } from '@/lib/site';
import './global.css';

export const metadata: Metadata = {
  metadataBase: new URL(`${siteOrigin}${basePath}/`),
  title: { default: 'Lyra 学习手册 · 从源码到自己的游戏', template: '%s · Lyra 学习手册' },
  description: '基于 UE 5.8.1 实际源码的中文 Lyra 教程，涵盖 Game Features、GAS、CommonUI、MVVM 与 MMORPG 后端实战。',
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return <html lang="zh-CN" suppressHydrationWarning><body><Providers>{children}</Providers></body></html>;
}
