#!/usr/bin/env python3
"""Wait for the selected qualification runs, failing closed on any failure."""
import argparse
import json
import subprocess
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('runs',nargs='+',type=int)
parser.add_argument('--timeout-seconds',type=int,default=7200)
args=parser.parse_args()
deadline=time.monotonic()+args.timeout_seconds
pending=set(args.runs)
while pending:
    for run in sorted(pending):
        result=None
        for attempt in range(3):
            response=subprocess.run(['gh','api',f'repos/JMUtechnologies/PS5-ECAM/actions/runs/{run}'],
                                    capture_output=True,text=True)
            if response.returncode==0:
                result=json.loads(response.stdout);break
            if attempt<2: time.sleep(5)
        if result is None: raise SystemExit(f'Cannot read qualification run {run}; stopping')
        print(f'{run}: {result["name"]} — {result["status"]} {result["conclusion"] or ""}',flush=True)
        if result['status']=='completed':
            if result['conclusion']!='success':
                raise SystemExit(f'Required build {run} did not pass; no package assembled')
            pending.remove(run)
    if not pending: break
    if time.monotonic()>=deadline: raise SystemExit('Qualification wait timed out; no package assembled')
    time.sleep(30)
print('All selected qualification runs passed; artifact provenance checks are next',flush=True)
