import { createServer } from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import path from 'node:path';
const directory = path.resolve('out');
const base = process.env.NEXT_PUBLIC_BASE_PATH || '';
const port = Number(process.env.PORT || 3000);
const types = { '.html':'text/html; charset=utf-8', '.js':'text/javascript', '.css':'text/css', '.json':'application/json', '.gz':'application/gzip', '.png':'image/png', '.jpg':'image/jpeg', '.svg':'image/svg+xml', '.woff2':'font/woff2', '.txt':'text/plain; charset=utf-8' };
createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    if (url.pathname === base && base) { res.writeHead(308, { Location: `${base}/` }); res.end(); return; }
    if (base && !url.pathname.startsWith(`${base}/`)) throw new Error('Outside base path');
    const pathname = decodeURIComponent(url.pathname.slice(base.length));
    let file = path.resolve(directory, `.${pathname}`);
    if (!file.startsWith(`${directory}${path.sep}`) && file !== directory) throw new Error('Outside export');
    const entry = await stat(file);
    if (entry.isDirectory()) {
      if (!url.pathname.endsWith('/')) {
        res.writeHead(308, { Location: `${url.pathname}/${url.search}` }); res.end(); return;
      }
      file = path.join(file, 'index.html');
    } else if (pathname.endsWith('/')) {
      // path.resolve removes a trailing slash even for a file. Static hosts reject it.
      throw new Error('A file URL cannot have a directory slash');
    }
    const data = await readFile(file);
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/json', 'Cache-Control':'no-cache' });
    res.end(data);
  } catch {
    res.writeHead(404, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(await readFile(path.join(directory, '404.html')).catch(() => 'Not found'));
  }
}).listen(port, '127.0.0.1', () => console.log(`Static export: http://127.0.0.1:${port}${base}/`));
