#!/usr/bin/env python3
"""Collect successful, source-compatible Actions artifacts for test assembly."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from urllib.parse import urlencode

ROOT=Path(__file__).resolve().parents[2]
REPO='JMUtechnologies/PS5-ECAM'
BASELINE='1bf39e0fd9b163b16e4049cb88145d780388bb91'
parser=argparse.ArgumentParser(description=__doc__)
for name in ('base','retro','shelf'): parser.add_argument('--'+name,type=int,required=True)
args=parser.parse_args()
def api(path):
    return json.loads(subprocess.check_output(['gh','api','repos/'+REPO+'/'+path],text=True))
def copy(source,target):
    if target.exists(): raise SystemExit('Refusing to replace existing staging: '+str(target))
    target.parent.mkdir(parents=True,exist_ok=True)
    if source.is_dir(): shutil.copytree(source,target)
    else: shutil.copy2(source,target)
groups=[('base',args.base,'workstation/native-qualification','Native x86 PS5 qualification',None),
        ('retro',args.retro,'workstation/retro-qualification','Added runtime PS5 builds',['porpoise','mupen64plus','fceumm']),
        ('shelf',args.shelf,'workstation/shelf-qualification','N64 NES Game Boy PS5 builds',['n64','nes','gbc','gba'])]
verified=[]
for label,run_id,branch,workflow,kinds in groups:
    run=api('actions/runs/'+str(run_id))
    if run['head_branch']!=branch or run['name']!=workflow or run['conclusion']!='success':
        raise SystemExit(f'{label}: expected successful {workflow} on {branch}')
    comparison=api('compare/'+BASELINE+'...'+run['head_sha'])
    if comparison['status'] not in ('ahead','identical'):
        raise SystemExit(f'{label}: build is not descended from the integration baseline')
    files=comparison.get('files',[])
    if len(files)>=300 or any(not f['filename'].startswith(('tools/docker/','.github/workflows/')) for f in files):
        raise SystemExit(f'{label}: production source changed since the qualified baseline')
    jobs=api('actions/runs/'+str(run_id)+'/jobs?'+urlencode({'per_page':100}))['jobs']
    if not jobs or any(j['conclusion']!='success' for j in jobs):
        raise SystemExit(f'{label}: not every build job passed')
    artifact_names=([f'ecam-native-qualification-{run["head_sha"]}'] if kinds is None
                    else [f'ecam-{kind}-native-{run["head_sha"]}' for kind in kinds])
    verified.append((label,run_id,run,artifact_names,kinds))
records=[]
for label,run_id,run,artifact_names,kinds in verified:
    for name in artifact_names:
        destination=ROOT/'build/assembly-inputs'/name
        if destination.exists(): raise SystemExit('Download staging already exists: '+str(destination))
        subprocess.run(['gh','run','download',str(run_id),'--repo',REPO,'--name',name,
                        '--dir',str(destination)],check=True)
        if label=='base':
            copy(destination/'cemu/ps5cemu.elf',ROOT/'build/cemu/ps5cemu.elf')
            copy(destination/'eden/app',ROOT/'build/eden/app')
        else:
            kind=next(k for k in kinds if name==f'ecam-{k}-native-{run["head_sha"]}')
            copy(destination/'retro'/kind,ROOT/'build/retro'/kind)
    records.append({'group':label,'run_id':run_id,'head_sha':run['head_sha'],'url':run['html_url']})
out=ROOT/'build/qualification';out.mkdir(parents=True,exist_ok=True)
(out/'build-runs.json').write_text(json.dumps({'baseline':BASELINE,'runs':records,'ps5_hardware_tested':False},indent=2)+'\n')
print('Collected only successful builds with unchanged integration source')
