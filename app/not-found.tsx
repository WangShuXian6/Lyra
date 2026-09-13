import { SiteLink as Link } from '@/components/site-link';
export default function NotFound() {
  return <main className="error-page"><span className="eyebrow">404 / PAGE NOT FOUND</span><h1>这条路径还没有通向一篇课程。</h1><p>可以从学习路线重新找到要阅读的章节。</p><Link href="/docs" className="primary-link">返回学习路线 →</Link></main>;
}
