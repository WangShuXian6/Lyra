'use client';

import { useState, type PointerEvent } from 'react';
import { Download, Expand, Move, Scan } from 'lucide-react';
import { assetUrl } from '@/lib/site';

type Props = { src: string; alt: string; caption?: string; download?: string };

export function BlueprintGraph({ src, alt, caption, download }: Props) {
  const [fit, setFit] = useState(true);
  const [drag, setDrag] = useState<{ x: number; y: number; left: number; top: number }>();
  function begin(event: PointerEvent<HTMLDivElement>) {
    if (fit || event.button !== 0) return;
    const el = event.currentTarget;
    el.setPointerCapture(event.pointerId);
    setDrag({ x: event.clientX, y: event.clientY, left: el.scrollLeft, top: el.scrollTop });
  }
  function move(event: PointerEvent<HTMLDivElement>) {
    if (!drag) return;
    event.currentTarget.scrollLeft = drag.left + drag.x - event.clientX;
    event.currentTarget.scrollTop = drag.top + drag.y - event.clientY;
  }
  return <figure className="blueprint-figure not-prose">
    <div className="blueprint-toolbar">
      <span><Move size={14} /> 蓝图图表</span>
      <div>
        <button type="button" onClick={() => setFit(!fit)} aria-pressed={!fit}>
          {fit ? <Expand size={14} /> : <Scan size={14} />}{fit ? '1:1 查看' : '适应宽度'}
        </button>
        <a href={assetUrl(src)} target="_blank" rel="noreferrer">打开原图</a>
        {download && <a href={assetUrl(download)} download><Download size={14} /> 节点文本</a>}
      </div>
    </div>
    <div className={`blueprint-canvas ${fit ? 'fit' : 'native'}`} tabIndex={0}
      aria-label={`${alt}。${fit ? '当前为全图预览' : '当前为原始尺寸，可拖动或滚动查看'}`}
      onPointerDown={begin} onPointerMove={move} onPointerUp={() => setDrag(undefined)}
      onPointerCancel={() => setDrag(undefined)}>
      {/* Native image dimensions are intentional: a large graph must remain legible at 1:1. */}
      {/* eslint-disable-next-line @next/next/no-img-element */}
      <img src={assetUrl(src)} alt={alt} loading="lazy" draggable={false} />
    </div>
    {caption && <figcaption>{caption}</figcaption>}
  </figure>;
}
