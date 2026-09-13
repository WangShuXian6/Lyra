export const basePath = process.env.NEXT_PUBLIC_BASE_PATH ?? '';
export const siteOrigin = 'https://wangshuxian6.github.io';
export const repository = 'https://github.com/WangShuXian6/Lyra';

/** For raw img, downloads and fetch; Next Link prefixes internal routes itself. */
export function assetUrl(path: string): string {
  if (!path.startsWith('/') || path.startsWith('//')) return path;
  if (basePath && (path === basePath || path.startsWith(`${basePath}/`))) return path;
  return `${basePath}${path}`;
}
