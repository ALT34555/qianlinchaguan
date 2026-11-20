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
if source == output or (source in output.parents and output.relative_to(source).parts[0] != 'obj'):
    raise SystemExit('Package output must be outside the source tree')
allowed={'.json','.plm','.blm','.png','.ico','.ttf','.otf'}
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
manifest={'version':1,'builtInLocales':['zh-CN','en','zh-Hant'],'files':records}
payload=json.dumps(manifest,ensure_ascii=False,indent=2)+'\n'
manifest_file=output/'resources.json'
previous=json.loads(manifest_file.read_bytes()) if manifest_file.exists() else {}
managed={entry['path']:entry['sha256'] for entry in previous.get('files',[])}
expected={entry['path'] for entry in records}|{'resources.json'}
stale=[]
retired=[]
for item in output.rglob('*'):
    logical=item.relative_to(output).as_posix()
    if not item.is_file() or logical in expected:
        continue
    if item.resolve().is_relative_to(output) and logical in managed and hashlib.sha256(item.read_bytes()).hexdigest()==managed[logical]:
        retired.append(item)
    else:
        stale.append(logical)
if stale:
    raise SystemExit('Stale package files; use a fresh output directory: '+', '.join(stale[:10]))
for item in retired:
    item.unlink()
manifest_file.write_text(payload,encoding='utf-8')
print(f'Resources: {len(records)} files, {sum(r["bytes"] for r in records)} bytes')
