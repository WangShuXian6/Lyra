import { readdir, readFile, stat } from 'node:fs/promises';
import path from 'node:path';
import { verifyMedia } from './check-media.mjs';

const root = process.cwd();
const errors = [];
async function files(dir, extension) {
  const result = [];
  for (const item of await readdir(dir, { withFileTypes: true })) {
    const name = path.join(dir, item.name);
    if (item.isDirectory()) result.push(...await files(name, extension));
    else if (name.endsWith(extension)) result.push(name);
  }
  return result;
}
const documents = await files(path.join(root, 'content/docs'), '.mdx');
const media = await verifyMedia(root, documents);
errors.push(...media.errors);
for (const file of documents) {
  const mdx = await readFile(file, 'utf8');
  if (!/^---\r?\n[\s\S]*?\btitle:\s*\S[\s\S]*?\r?\n---/.test(mdx)) errors.push(`${file}: missing frontmatter title`);
  for (const match of mdx.matchAll(/(?:src|download)=["'](\/(?:media|blueprints|downloads)\/[^"']+)["']/g)) {
    try { await stat(path.join(root, 'public', decodeURIComponent(match[1]))); }
    catch { errors.push(`${file}: missing ${match[1]}`); }
  }
  for (const match of mdx.matchAll(/!\[[^\]]*\]\((\/[^\s)]+)(?:\s+"[^"]*")?\)/g)) {
    try { await stat(path.join(root, 'public', decodeURIComponent(match[1]))); }
    catch { errors.push(`${file}: missing Markdown image ${match[1]}`); }
  }
}
for (const file of await files(path.join(root, 'public/blueprints'), '.txt')) {
  const nodes = await readFile(file, 'utf8');
  const begins = [...nodes.matchAll(/^Begin Object\b/gm)].length;
  const ends = [...nodes.matchAll(/^End Object\b/gm)].length;
  const nativeGraphNode = /^Begin Object Class=\/Script\/(?:BlueprintGraph\.K2Node_|AnimGraph\.AnimGraphNode_)/m.test(nodes);
  if (!begins || begins !== ends || !nativeGraphNode) errors.push(`${file}: invalid native Blueprint clipboard text`);
}
if (process.argv.includes('--export')) {
  const base = process.env.NEXT_PUBLIC_BASE_PATH || '';
  const output = path.join(root, 'out');
  const pages = await files(output, '.html');
  const cache = new Map();
  const decode = (s) => s.replaceAll('&amp;', '&').replaceAll('&quot;', '"').replaceAll('&#x27;', "'");
  for (const file of pages) {
    const html = (await readFile(file, 'utf8')).replace(/(<script\b[^>]*>)[\s\S]*?(<\/script>)/gi, '$1$2');
    cache.set(file, html);
    if (/\bsrc="(?:\[object Object\]|undefined)"/.test(html)) errors.push(`${file}: invalid rendered image src`);
  }
  for (const [file, html] of cache) {
    const relative = path.relative(output, file).replaceAll('\\', '/').replace(/index\.html$/, '');
    const pageURL = new URL(`${base}/${relative}`, 'https://local.invalid');
    for (const tag of html.matchAll(/<(?:a|img|link|script)\b[^>]*>/g)) {
      // Resource hints name an origin, not an exported page under basePath.
      if (/\brel="(?:preconnect|dns-prefetch)"/.test(tag[0])) continue;
      const m = tag[0].match(/\b(?:href|src)="([^"]+)"/);
      if (!m) continue;
      const raw = decode(m[1]);
      if (/^(?:data:|mailto:|tel:|javascript:)/.test(raw)) continue;
      const url = new URL(raw, pageURL);
      if (url.origin !== pageURL.origin) continue;
      if (base && !url.pathname.startsWith(`${base}/`) && url.pathname !== base) {
        errors.push(`${relative}: link escapes basePath: ${raw}`); continue;
      }
      const pathname = decodeURIComponent(url.pathname.slice(base.length));
      let target = path.join(output, pathname);
      try {
        if ((await stat(target)).isDirectory()) target = path.join(target, 'index.html');
        await stat(target);
        if (url.hash && target.endsWith('.html')) {
          const targetHTML = cache.get(target) ?? await readFile(target, 'utf8');
          const anchor = decodeURIComponent(url.hash.slice(1));
          if (![...targetHTML.matchAll(/\bid="([^"]+)"/g)].some(x => decode(x[1]) === anchor)) errors.push(`${relative}: missing anchor ${raw}`);
        }
      } catch { errors.push(`${relative}: missing exported target ${raw}`); }
    }
  }
  const search = JSON.parse(await readFile(path.join(output, 'api/search'), 'utf8').catch(() => readFile(path.join(output, 'api/search/index.json'), 'utf8')).catch(() => readFile(path.join(output, 'api/search/index.html'), 'utf8')));
  if (!JSON.stringify(search).includes('Lyra')) errors.push('Search index does not contain Lyra');
  console.log(`Checked ${pages.length} exported HTML pages, local targets, anchors and search index (${base || '/'}).`);
}
if (errors.length) { console.error([...new Set(errors)].join('\n')); process.exitCode = 1; }
else console.log(`Content checks passed: ${documents.length} MDX documents, native Blueprint downloads, and ${media.publicFiles} public files with SHA-256/native dimensions.`);
