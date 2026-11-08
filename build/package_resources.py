from pathlib import Path
import argparse
import hashlib
import json
import shutil

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
source=args.source.resolve()
output=args.output.resolve()
if source==output or source in output.parents and '902temp' not in output.parts and 'obj' not in output.parts:
    raise SystemExit('Package output must be outside the source tree')
allowed={'.json','.png','.ico','.ttf','.otf'}
paths=list((source/'content').rglob('*'))
paths+=list((source/'src/systems/world/model').rglob('*.png'))
records=[]
seen=set()
for path in sorted(paths):
    if not path.is_file() or path.suffix.lower() not in allowed:
        continue
    logical=path.relative_to(source).as_posix()
    if logical.casefold() in seen:
        raise SystemExit('Case-colliding asset: '+logical)
    seen.add(logical.casefold())
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    target=output/logical
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=digest:
        shutil.copyfile(path,target)
    records.append({'path':logical,'bytes':path.stat().st_size,'sha256':digest})
output.mkdir(parents=True,exist_ok=True)
manifest={'version':1,'builtInLocales':['zh-CN','en'],'files':records}
payload=json.dumps(manifest,ensure_ascii=False,indent=2)+'\n'
(output/'resources.json').write_text(payload,encoding='utf-8')
expected={entry['path'] for entry in records}|{'resources.json'}
stale=[p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file() and p.relative_to(output).as_posix() not in expected]
if stale:
    raise SystemExit('Stale package files; use a fresh output directory: '+', '.join(stale[:10]))
print(f'Resources: {len(records)} files, {sum(r["bytes"] for r in records)} bytes')
