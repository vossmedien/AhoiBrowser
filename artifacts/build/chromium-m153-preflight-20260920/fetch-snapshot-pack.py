#!/usr/bin/env python3
"""One supervised standard Git snapshot fetch; no checkout or ref update."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from chromium_checkout_state import checkout_snapshot, changed_guard_fields, git_environment

checkout = ROOT / '.work/chromium/src'
evidence = Path(__file__).resolve().parent
target = '792bf6722e73a45aa9e47c163b9901bdc17f3230'
assert json.loads((ROOT / 'config/chromium.json').read_text())['commit'] == target
assert target in (checkout / '.git/shallow').read_text().splitlines()
environment = git_environment()
before = checkout_snapshot(checkout, environment)
config_hash = hashlib.sha256((checkout / '.git/config').read_bytes()).hexdigest()
assert before['statusSha256'] == hashlib.sha256(b'').hexdigest()
command = [
    'git', '-c', 'http.version=HTTP/1.1', '-c', 'http.maxRequests=1',
    '-c', 'fetch.parallel=1', '-c', 'maintenance.auto=false', '-c', 'gc.auto=0',
    '-c', 'pack.threads=2', '-c', 'fetch.unpackLimit=1',
    'fetch', '--refetch', '--no-filter', '--depth=1', '--no-tags',
    '--no-write-fetch-head', '--no-write-commit-graph', '--no-auto-maintenance',
    '--no-recurse-submodules', '--refmap=', '--progress', 'origin', target,
]
started = time.time()
reason = None
with (evidence / 'snapshot-pack.log').open('wb') as log:
    process = subprocess.Popen(command, cwd=checkout, env=environment,
                               stdout=log, stderr=subprocess.STDOUT,
                               start_new_session=True)
    print(f'Exact snapshot pack PID {process.pid}; 1800s/32GiB bounds.', flush=True)
    try:
        while process.poll() is None:
            space = os.statvfs(checkout)
            if space.f_bavail * space.f_frsize < 32 * 1024**3:
                reason = 'disk_floor'
            if time.time() - started > 1800:
                reason = 'time_bound'
            if reason:
                os.killpg(process.pid, signal.SIGTERM)
                break
            time.sleep(5)
    except KeyboardInterrupt:
        reason = 'interrupted'
        os.killpg(process.pid, signal.SIGTERM)
    try:
        result = process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        result = process.wait()
after = checkout_snapshot(checkout, environment)
changed = changed_guard_fields(before, after)
config_unchanged = config_hash == hashlib.sha256((checkout / '.git/config').read_bytes()).hexdigest()
report = dict(command=command, target=target, exitCode=result, stopReason=reason,
              elapsedSeconds=time.time()-started, before=before, after=after,
              changedFields=changed, configUnchanged=config_unchanged,
              acceptance='fetch only; complete target inventory still required')
(evidence / 'snapshot-pack-result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({key: report[key] for key in ('exitCode', 'stopReason', 'elapsedSeconds', 'changedFields', 'configUnchanged')}), flush=True)
sys.exit(3 if changed or not config_unchanged else (0 if result == 0 else 1))
