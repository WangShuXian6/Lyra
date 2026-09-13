import { createHash } from 'node:crypto';
import { readFile, readdir } from 'node:fs/promises';
import path from 'node:path';

// These records describe the committed public files. Source-asset hashes may
// intentionally refer to the revision at capture time, so they are not silently
// replaced with hashes from a later recompilation. CI needs no local UE install.
export async function verifyMedia(root, documents) {
  const errors = [];
  const registered = new Set();
  let checked = 0;
  const publicRoot = path.resolve(root, 'public');
  const manifests = await readdir(path.join(root, 'source-evidence'));
  for (const name of manifests.filter(name => name.endsWith('.json'))) {
    const manifest = JSON.parse((await readFile(path.join(root, 'source-evidence', name), 'utf8')).replace(/^\uFEFF/, ''));
    for (const entry of [...(manifest.media ?? []), ...(manifest.captures ?? [])]) {
      if (!entry.publicPath) continue;
      const url = entry.publicPath.replace(/^public\//, '/');
      if (!/^\/(?:media|blueprints)\//.test(url)) continue;
      const file = path.resolve(publicRoot, '.' + decodeURIComponent(url));
      if (!file.startsWith(publicRoot + path.sep)) {
        errors.push(`${name}: public file escapes public/: ${entry.publicPath}`);
        continue;
      }
      try {
        const bytes = await readFile(file);
        const actualHash = createHash('sha256').update(bytes).digest('hex');
        if (typeof entry.sha256 !== 'string' || entry.sha256.toLowerCase() !== actualHash)
          errors.push(`${name}: SHA-256 mismatch for ${url}`);
        if (entry.bytes !== undefined && entry.bytes !== bytes.length)
          errors.push(`${name}: byte count mismatch for ${url}`);
        if (url.endsWith('.png')) {
          if (bytes.length < 24 || bytes.subarray(0, 8).toString('hex') !== '89504e470d0a1a0a') {
            errors.push(`${name}: invalid PNG header for ${url}`);
          } else if (entry.width !== bytes.readUInt32BE(16) || entry.height !== bytes.readUInt32BE(20)) {
            errors.push(`${name}: native image dimensions mismatch for ${url}`);
          }
        }
        registered.add(url);
        checked++;
      } catch (error) {
        errors.push(`${name}: cannot read ${url}: ${error.message}`);
      }
    }
  }
  for (const document of documents) {
    const mdx = await readFile(document, 'utf8');
    const refs = [
      ...mdx.matchAll(/(?:src|download)=["'](\/(?:media|blueprints)\/[^"']+)["']/g),
      ...mdx.matchAll(/\[[^\]]*\]\((\/(?:media|blueprints)\/[^\s)]+)(?:\s+"[^"]*")?\)/g),
    ];
    for (const match of refs) {
      if (!registered.has(match[1])) errors.push(`${path.relative(root, document)}: no public file provenance for ${match[1]}`);
    }
  }
  return { errors, checked, publicFiles: registered.size };
}
