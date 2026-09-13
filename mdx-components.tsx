import type { MDXComponents } from 'mdx/types';
import defaultComponents from 'fumadocs-ui/mdx';
import { ImageZoom } from 'fumadocs-ui/components/image-zoom';
import { BlueprintGraph } from '@/components/blueprint-graph';
import { assetUrl } from '@/lib/site';
import { SiteLink as Link } from '@/components/site-link';

type ImportedImage = { src: string; width?: number; height?: number };
function resolveImage(value: unknown): ImportedImage | undefined {
  if (typeof value === 'string') return { src: value };
  if (!value || typeof value !== 'object') return undefined;
  if ('default' in value) return resolveImage(value.default);
  if ('src' in value && typeof value.src === 'string') return value as ImportedImage;
}

export function getMDXComponents(components?: MDXComponents): MDXComponents {
  return {
    ...defaultComponents,
    BlueprintGraph,
    a: ({ href = '', children, ...props }) => {
      if (href.startsWith('/docs') || href === '/') return <Link href={href} {...props}>{children}</Link>;
      return <a href={assetUrl(href)} {...props}>{children}</a>;
    },
    img: ({ src, alt, ...props }) => {
      // Fumadocs MDX can turn local Markdown images into Next StaticImageData imports.
      const image = resolveImage(src);
      if (!image) return <img alt={alt ?? ''} {...props} />;
      return <ImageZoom {...props} src={assetUrl(image.src)} alt={alt ?? ''}
        width={image.width ?? 1600} height={image.height ?? 900}
        rmiz={{ children: null, a11yNameButtonZoom: '放大图片', a11yNameButtonUnzoom: '关闭放大' }} />;
    },
    ...components,
  };
}
export const useMDXComponents = getMDXComponents;
