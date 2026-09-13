import { DocsLayout } from 'fumadocs-ui/layouts/docs';
import { source } from '@/lib/source';
import { repository } from '@/lib/site';

export default function Layout({ children }: { children: React.ReactNode }) {
  return <DocsLayout tree={source.pageTree} nav={{ title: <span className="brand">L<span className="brand-mark">/</span> Lyra 学习手册</span> }}
    githubUrl={repository}
    sidebar={{ defaultOpenLevel: 0 }} tabs={false}>
    {children}
  </DocsLayout>;
}
