import { SiteLink as Link } from '@/components/site-link';
import { ArrowDown, ArrowUpRight, BookOpen, Braces, GitBranch, Layers3, Network, Route } from 'lucide-react';
import { source } from '@/lib/source';
import { repository } from '@/lib/site';

const topics = [
  ['01', '启动与架构', '跟踪一次 Experience 加载，理解代码、配置与资产如何协作。', '/docs/02-architecture', Layers3],
  ['02', '角色、输入与 GAS', '从按下一个键，到激活技能、产生效果，再到网络同步。', '/docs/04-gas', Braces],
  ['03', 'CommonUI 与 MVVM', '把界面层级、输入焦点、玩法状态和数据显示连接起来。', '/docs/06-ui', Network],
  ['04', '自己的游戏与后端', '用训练场和全新 MMORPG 工程验证每一个设计选择。', '/docs/08-mmorpg', GitBranch],
] as const;

export default function Home() {
  return <main className="home-page">
    <header className="home-nav"><Link href="/" className="brand">L<span className="brand-mark">/</span> Lyra 学习手册</Link><nav><Link href="/docs">课程目录</Link><a href={repository}>GitHub <ArrowUpRight size={14} /></a></nav></header>
    <section className="hero">
      <div className="hero-copy"><div className="eyebrow"><span className="live-dot" /> UNREAL ENGINE 5.8.1 · 中文开发手册</div>
        <h1>读懂 Lyra。<br /><span>做出自己的游戏。</span></h1>
        <p className="hero-description">从一次角色生成、一张蓝图、一段 C++ 出发，<br className="desktop-break" />认识 Lyra 的设计，并把它用到全新的 MMORPG 项目中。</p>
        <div className="hero-actions"><Link href="/docs" className="primary-link">开始学习 <ArrowUpRight size={18} /></Link><Link href="/docs/08-mmorpg" className="secondary-link">从空白项目开始 <span>→</span></Link></div>
        <div className="hero-notes"><span><BookOpen size={15} /> {source.getPages().length} 篇课程与指南</span><span><Braces size={15} /> C++ · 蓝图 · 实际工程</span></div>
      </div>
      <div className="architecture-plate" aria-label="Lyra 核心关系图">
        <div className="plate-heading"><span>一次玩法的诞生</span><span>FIG. 01</span></div>
        <div className="plate-node experience"><small>定义要玩什么</small><strong>Experience</strong><code>ULyraExperienceDefinition</code></div>
        <div className="plate-connector"><span>加载与组合</span><ArrowDown size={20} /></div>
        <div className="plate-pair"><div className="plate-node"><small>启用玩法功能</small><strong>Game Features</strong></div><div className="plate-node"><small>配置玩家角色</small><strong>PawnData</strong></div></div>
        <div className="plate-connector"><span>初始化完成</span><ArrowDown size={20} /></div>
        <div className="plate-trio"><span>Input</span><span>Abilities</span><span>UI</span></div>
        <div className="plate-footer"><span className="live-dot" /> Gameplay Ready <code>从源码找到答案 ↗</code></div>
      </div>
    </section>
    <section className="learning-routes"><div className="section-heading"><span className="eyebrow">CHOOSE YOUR PATH</span><h2>两条路线，同一套理解。</h2><p>先认识系统，再用可运行的工程检验理解。</p></div>
      <div className="route-grid"><Link href="/docs/01-start" className="route-card"><div className="route-label"><span>路线 A</span><Route size={21} /></div><h3>从 Lyra 开始</h3><p>运行官方示例 → 阅读核心系统 → 扩展自己的射击训练场</p><span className="route-bottom">适合第一次接触 Lyra <ArrowUpRight size={19} /></span></Link>
      <Link href="/docs/08-mmorpg" className="route-card"><div className="route-label"><span>路线 B</span><GitBranch size={21} /></div><h3>从自己的项目开始</h3><p>空白 UE 工程 → MMORPG 玩法与 UI → 独立后端与角色存档</p><span className="route-bottom">学习迁移、取舍与重新设计 <ArrowUpRight size={19} /></span></Link></div>
    </section>
    <section className="topic-section"><div className="section-heading"><span className="eyebrow">READ / TRACE / BUILD</span><h2>把系统连起来看。</h2></div><div className="topic-list">{topics.map(([n,title,description,url,Icon])=><Link href={url} className="topic-row" key={n}><span className="topic-number">{n}</span><Icon size={23}/><div><h3>{title}</h3><p>{description}</p></div><ArrowUpRight size={21}/></Link>)}</div></section>
    <section className="evidence-strip"><div><span className="eyebrow">LEARN WITH EVIDENCE</span><h2>源码、节点、结果，互相对得上。</h2></div><p>每个关键流程附源码与资产定位。蓝图支持完整原图与 1:1 查看，节点文本可以复制到 UE。验证状态与版本依据随课程记录。</p><Link href="/docs/downloads">查看配套资料 <ArrowUpRight size={17}/></Link></section>
    <footer className="home-footer"><span>Lyra 学习手册 <span className="muted">/ 非 Epic 官方教程</span></span><span>以实际工程为依据 · UE 5.8.1</span><a href="https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-sample-game-in-unreal-engine">Epic Lyra 文档 ↗</a></footer>
  </main>;
}
