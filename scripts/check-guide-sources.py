"""Verify tutorial excerpts against the actual local UE/source files, without running UE.

Run from the docs repository: python scripts/check-guide-sources.py
Override --lyra-root/--engine-root when the verified source tree lives elsewhere.
Source hashes are intentionally strict: a different revision needs a fresh review.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import re
import textwrap

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--lyra-root', type=Path, default=Path('F:/UE/LyraStarterGame'))
parser.add_argument('--engine-root', type=Path, default=Path('E:/UnrealEngine'))
args = parser.parse_args()

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

errors = []
checked = []
roots = {
    'F:/UE/LyraDoc': ROOT,
    'F:/UE/LyraStarterGame': args.lyra_root,
    'E:/UnrealEngine': args.engine_root,
}
for record in read_json(ROOT/'source-evidence/full-guide-code.json')['excerpts']:
    try:
        source_root = roots[record['sourceRoot']].resolve()
        source = (source_root / record['file']).resolve()
        assert source.is_relative_to(source_root), 'Source escaped its declared root'
        raw = source.read_bytes()
        assert digest(raw) == record['sourceSHA256'], 'Source revision changed'
        lines = raw.decode('utf-8-sig').splitlines()
        assert 1 <= record['startLine'] <= record['endLine'] <= len(lines), 'Excerpt line range is outside source file'
        code = textwrap.dedent('\n'.join(lines[record['startLine']-1:record['endLine']]))
        assert code == record['code'], 'Excerpt no longer matches its line range'
        assert digest(code.encode()) == record['excerptSHA256'], 'Excerpt hash changed'
        chapter = ROOT/'content/docs'/(record['chapter']+'.mdx')
        body = chapter.read_text(encoding='utf-8-sig')
        blocks = re.findall(r'^```[^\n]*\n(.*?)\n```', body, re.M | re.S)
        assert code in blocks, 'Exact source excerpt is absent from fenced code blocks'
        checked.append({k: record[k] for k in ('chapter','file','startLine','endLine','sourceSHA256','excerptSHA256')})
    except (AssertionError, KeyError, OSError) as exc:
        errors.append(f'{record["chapter"]}: {record["file"]}: {exc}')

coverage = read_json(ROOT/'source-evidence/full-guide-coverage.json')
tracked = {c['path'] for c in coverage['chapters']}
actual = {p.relative_to(ROOT).as_posix() for p in (ROOT/'content/docs').rglob('*.mdx')}
if tracked != actual:
    errors.append(f'Coverage mismatch: missing {actual-tracked}; stale {tracked-actual}')
chapters = []
for c in coverage['chapters']:
    p = ROOT/c['path']; raw=p.read_bytes(); body=raw.decode('utf-8-sig')
    images = re.findall(r'!\[[^\]]*\]\((/media/[^)\s]+)',body)
    images += re.findall(r'<BlueprintGraph\s+src="(/media/[^"]+)"',body)
    if not images:
        errors.append(f'{c["path"]}: no illustration')
    if not c.get('newPage') and '{/* full-guide-begin */}' not in body:
        errors.append(f'{c["path"]}: whole-guide teaching revision absent')
    chapters.append({'path':c['path'],'sha256':digest(raw),'characters':len(body),
                     'illustrations':len(images),'codeBlocks':len(re.findall(r'^```',body,re.M))//2,
                     'newSourceExcerpts':sum(r['chapter']==p.relative_to(ROOT/'content/docs').with_suffix('').as_posix() for r in checked)})

atlas=(ROOT/'content/docs/02-architecture/plugin-atlas.mdx').read_text(encoding='utf-8')
entries=re.findall(r'^\| `(Plugins/[^`]+)` \| `([^`]+)`',atlas,re.M)
for plugin, entry in entries:
    if not (args.lyra_root/plugin/entry).exists():
        errors.append(f'Plugin atlas entry missing: {plugin}/{entry}')
if len(entries)!=22:
    errors.append(f'Expected 22 local plugin source entries, found {len(entries)}')

report={'checkedAt':datetime.datetime.now().astimezone().isoformat(),'passed':not errors,
        'scope':'Exact new source excerpts, all chapter revision coverage and actual local plugin entry paths. Read-only source verification; does not rerun gameplay/backend tests.',
        'excerpts':checked,'chapters':chapters,'pluginSourceEntries':len(entries),'errors':errors}
(ROOT/'verification/full-guide-source-review.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'Checked {len(checked)} exact source excerpts, {len(chapters)} illustrated chapters and {len(entries)} plugin entry paths.')
for error in errors: print(error)
raise SystemExit(bool(errors))
