#!/usr/bin/env python3
"""AI-assisted, read-only PS5 FTP inventory. Never uploads, renames or deletes."""
import ftplib
import json
from datetime import datetime, timezone
from pathlib import Path

root = Path(__file__).resolve().parents[2]
report = {'host': '192.168.1.19', 'port': 2121,
          'time': datetime.now(timezone.utc).isoformat(),
          'mode': 'read-only', 'directories': {}}
try:
    with ftplib.FTP() as ftp:
        ftp.connect(report['host'], report['port'], timeout=15)
        ftp.login()
        for path in ('/data/homebrew', '/data/homebrew/PPSA99360',
                     '/data/ps5cemu', '/data/eden', '/mnt/usb0', '/mnt/usb1'):
            try:
                report['directories'][path] = ftp.nlst(path)
            except ftplib.all_errors as e:
                report['directories'][path] = {'error': str(e)}
except (OSError, ftplib.Error) as e:
    raise SystemExit('FTP preflight could not connect: ' + str(e))
out = root / 'build/ps5-test-preflight.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
print('Saved:', out)
print('No console files were changed. No hardware tests have been run.')
