import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { gzipSync } from 'node:zlib';

export function deterministicGzip(bytes) {
  const result = gzipSync(bytes, { level: 9 });
  if (result[0] !== 0x1f || result[1] !== 0x8b || result[2] !== 8 || result[3] !== 0 || result.readUInt32LE(4) !== 0) {
    throw new Error('Expected gzip without optional fields, header CRC or timestamp');
  }
  // RFC 1952 OS=255 (unknown). zlib otherwise emits 10 on Windows, 3 on Linux.
  // FLG=0 means this header has no CRC to recalculate; payload/trailer stay intact.
  result[9] = 255;
  return result;
}

// POSIX ustar, regular files only. Fixed metadata makes Windows and Linux builds
// publish identical downloads instead of embedding their local user/group IDs.
export async function deterministicTar(directory, names, modifiedAt) {
  const chunks = [];
  const root = path.resolve(directory);
  const write = (header, offset, length, value) => {
    const bytes = Buffer.from(value, 'utf8');
    if (bytes.length > length) throw new Error(`Tar field exceeds ${length} bytes`);
    bytes.copy(header, offset);
  };
  const number = (header, offset, length, value) => {
    if (!Number.isSafeInteger(value) || value < 0) throw new Error('Invalid tar numeric field');
    const octal = value.toString(8);
    if (octal.length >= length) throw new Error('Tar numeric field overflow');
    write(header, offset, length, octal.padStart(length - 1, '0') + '\0');
  };
  for (const name of [...names].sort()) {
    if (name.includes('\0') || name.includes('\\') || path.posix.isAbsolute(name) || name.split('/').some(part => part === '..')) {
      throw new Error(`Unsafe tar member name: ${name}`);
    }
    const absolute = path.resolve(root, name);
    if (!absolute.startsWith(root + path.sep)) throw new Error(`Tar member escaped staging directory: ${name}`);
    let filename = name, prefix = '';
    if (Buffer.byteLength(filename) > 100) {
      const split = [...name.matchAll(/\//g)].map(match => match.index).reverse().find(index =>
        Buffer.byteLength(name.slice(0, index)) <= 155 && Buffer.byteLength(name.slice(index + 1)) <= 100);
      if (split === undefined) throw new Error(`Member path is too long for ustar: ${name}`);
      prefix = name.slice(0, split); filename = name.slice(split + 1);
    }
    const bytes = await readFile(absolute);
    const header = Buffer.alloc(512);
    write(header, 0, 100, filename);
    number(header, 100, 8, 0o644);
    number(header, 108, 8, 0);
    number(header, 116, 8, 0);
    number(header, 124, 12, bytes.length);
    number(header, 136, 12, Math.floor(modifiedAt.getTime() / 1000));
    header.fill(0x20, 148, 156);
    write(header, 156, 1, '0');
    write(header, 257, 6, 'ustar\0');
    write(header, 263, 2, '00');
    write(header, 265, 32, 'root');
    write(header, 297, 32, 'root');
    write(header, 345, 155, prefix);
    const checksum = header.reduce((sum, byte) => sum + byte, 0);
    write(header, 148, 8, checksum.toString(8).padStart(6, '0') + '\0 ');
    chunks.push(header, bytes);
    if (bytes.length % 512) chunks.push(Buffer.alloc(512 - bytes.length % 512));
  }
  chunks.push(Buffer.alloc(1024));
  return Buffer.concat(chunks);
}
