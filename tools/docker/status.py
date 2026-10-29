#!/usr/bin/env python3
"""Read live GitHub qualification status; never starts or cancels builds."""
import argparse
from datetime import datetime
import json
import subprocess
import time
from urllib.parse import urlencode
from zoneinfo import ZoneInfo

REPO = 'JMUtechnologies/PS5-ECAM'
groups = [
    ('workstation/native-qualification','Native x86 PS5 qualification'),
    ('workstation/retro-qualification','Added runtime PS5 builds'),
    ('workstation/shelf-qualification','N64 NES Game Boy PS5 builds'),
    ('workstation/combined','Assemble qualified PS5 test bundle'),
]
systems = ['n64','nes','gbc','gba','fceumm','mupen64plus','porpoise']
names = {'n64':'N64','nes':'NES','gbc':'GB/GBC','gba':'GBA','fceumm':'FCEUmm',
         'mupen64plus':'Mupen64Plus','porpoise':'Porpoise','package':'Complete test package'}
def api(path):
    return json.loads(subprocess.check_output(['gh','api','repos/'+REPO+'/'+path],text=True))
def snapshot():
    rows = {}
    urls = []
    for branch, workflow in groups:
        runs = api('actions/runs?'+urlencode({'branch':branch,'per_page':10}))['workflow_runs']
        run = next((r for r in runs if r['name']==workflow),None)
        if not run: continue
        urls.append(run['html_url'])
        for job in api('actions/runs/'+str(run['id'])+'/jobs')['jobs']:
            key = 'base' if branch==groups[0][0] else job['name'].removeprefix('runtime (').removesuffix(')')
            step = next((s['name'] for s in job['steps'] if s['status']=='in_progress'),'')
            rows[key] = {'status':job['status'],'conclusion':job['conclusion'],
                         'step':step,'url':job['html_url'],'head_sha':run['head_sha']}
    return rows, urls
def symbol(row):
    if not row: return '⬜'
    if row['conclusion']=='success': return '🟩'
    if row['conclusion']=='skipped': return '⬜'
    if row['status']=='completed': return '🟥'
    return '🟨'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--watch',action='store_true',help='Refresh every 30 seconds; Ctrl+C stops only this viewer')
args=parser.parse_args()
try:
    while True:
        rows,urls=snapshot()
        if args.watch: print('\033[2J\033[H',end='')
        print('Checked:',datetime.now(ZoneInfo('America/Los_Angeles')).strftime('%Y-%m-%d %I:%M:%S %p %Z'))
        passed=sum(rows.get(s,{}).get('conclusion')=='success' for s in systems)
        print('Added runtime builds:', ''.join(symbol(rows.get(s)) for s in systems),f'{passed}/7 passed')
        print('Build count only; not elapsed-time progress or PS5 testing.')
        for key in ['base',*systems,'package']:
            row=rows.get(key)
            detail=(row['conclusion'] or row['status']) if row else 'not found'
            print(symbol(row), 'ECAM/Eden' if key=='base' else names[key], detail,
                  ('— '+row['step']) if row and row['step'] else '')
        print('PS5 hardware tests: pending; not inferred from build or packaging success.')
        print('\n'.join(urls),flush=True)
        if not args.watch: break
        time.sleep(30)
except KeyboardInterrupt:
    print('\nStatus viewer closed; GitHub jobs are unaffected.')
