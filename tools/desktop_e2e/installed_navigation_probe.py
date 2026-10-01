#!/usr/bin/env python3
"""Bounded, isolated installed-navigation diagnosis, not product acceptance."""
import argparse
import datetime
import functools
import hashlib
import http.server
import json
import os
import pathlib
import plistlib
import re
import signal
import subprocess
import tempfile
import threading
import time
import urllib.request


def idle_seconds():
    raw = subprocess.check_output(['ioreg', '-c', 'IOHIDSystem'], text=True)
    return int(re.search(r'"HIDIdleTime"\s*=\s*(\d+)', raw).group(1)) // 10**9


def app_running(app):
    commands = subprocess.check_output(['ps', '-axww', '-o', 'comm='], text=True)
    return str(app / 'Contents') + '/' in commands


def stop_owned(process):
    if process is None or process.poll() is not None:
        return
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
            return
        except subprocess.TimeoutExpired:
            pass
    raise RuntimeError('owned process could not be reaped')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=pathlib.Path, required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--lock-directory', type=pathlib.Path, required=True)
    parser.add_argument('--port', type=int, default=9413)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    state = {'runnerPid': os.getpid(), 'phase': 'preflight', 'source': args.source,
             'startedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'acceptance': False}
    def save():
        (args.output / 'state.json').write_text(json.dumps(state, indent=2) + '\n')
    save()
    info = plistlib.loads((args.app / 'Contents/Info.plist').read_bytes())
    if info.get('AhoiSourceCommit') != args.source:
        raise RuntimeError('installed source differs from expected candidate')
    state['installedInfoSha256'] = hashlib.sha256(
        (args.app / 'Contents/Info.plist').read_bytes()).hexdigest()
    locks = [args.lock_directory / name for name in ('build.lock', 'e2e.lock', 'h3.lock')]
    state.update(idleSeconds=idle_seconds(), appRunning=app_running(args.app))
    if state['idleSeconds'] < 90 or state['appRunning'] or any(p.exists() for p in locks):
        state['phase'] = 'deferred-owner-active'
        save()
        return 2
    # Refuse a pre-existing DevTools listener; never attach to another browser.
    busy = subprocess.run(['lsof', '-nP', f'-iTCP:{args.port}', '-sTCP:LISTEN'],
                          capture_output=True).returncode == 0
    if busy:
        state['phase'] = 'deferred-port-busy'
        save()
        return 2
    lock = locks[1]
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.write(descriptor, f'codex navigation diagnostic pid={os.getpid()}\n'.encode())
    os.close(descriptor)
    identity = lock.stat().st_ino
    browser = driver = server = None
    server_thread = None
    try:
        profile = pathlib.Path(tempfile.mkdtemp(prefix='ahoi-navigation-profile.', dir='/private/tmp'))
        site = profile / 'fixture'
        site.mkdir()
        for page in ('A', 'B'):
            (site / f'{page.lower()}.html').write_text(f'<title>Pane{page}</title><h1>Pane {page}</h1>')
        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self, format, *values):
                with (args.output / 'site.log').open('a') as log:
                    log.write(format % values + '\n')
        server = http.server.ThreadingHTTPServer(
            ('127.0.0.1', 0), functools.partial(Handler, directory=str(site)))
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        url = f'http://127.0.0.1:{server.server_port}'
        with urllib.request.urlopen(url + '/a.html', timeout=5) as response:
            body = response.read().decode()
            state['httpReadiness'] = {'status': response.status, 'body': body}
            if response.status != 200 or '<title>PaneA</title>' not in body:
                raise RuntimeError('fixture did not serve the expected page')
        state.update(profile=str(profile), fixtureOrigin=url, phase='launching')
        save()
        # Recheck immediately before the only app action.
        if idle_seconds() < 90 or app_running(args.app):
            state['phase'] = 'deferred-owner-active'
            save()
            return 2
        with (args.output / 'browser.log').open('w') as log:
            browser = subprocess.Popen([
                str(args.app / 'Contents/MacOS/AhoiBrowser'), f'--user-data-dir={profile}',
                '--no-first-run', '--no-default-browser-check',
                f'--remote-debugging-port={args.port}', url + '/a.html'],
                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            state.update(browserPid=browser.pid, phase='observing')
            save()
            deadline = time.monotonic() + 30
            while True:
                if browser.poll() is not None:
                    raise RuntimeError('owned browser exited during startup')
                if idle_seconds() < 2:
                    raise RuntimeError('owner input returned; cancelling diagnostic')
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{args.port}/json', timeout=1) as response:
                        targets = json.load(response)
                    pages = [t for t in targets if t.get('type') == 'page']
                    if pages:
                        break
                except (OSError, ValueError):
                    pass
                if time.monotonic() > deadline:
                    raise RuntimeError('owned DevTools startup deadline')
                time.sleep(0.5)
            time.sleep(4)
            with urllib.request.urlopen(f'http://127.0.0.1:{args.port}/json', timeout=2) as response:
                targets = json.load(response)
            (args.output / 'startup-targets.json').write_text(json.dumps(targets, indent=2) + '\n')
            page = next(t for t in targets if t.get('type') == 'page')
            helper = pathlib.Path(__file__).with_name('navigation-probe.mjs')
            state['driverSha256'] = hashlib.sha256(helper.read_bytes()).hexdigest()
            driver = subprocess.Popen(['node', str(helper), str(args.port), page['id'],
                                       url + '/b.html', str(args.output / 'protocol.json')],
                                      stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            state['driverPid'] = driver.pid
            save()
            deadline = time.monotonic() + 45
            while driver.poll() is None:
                if idle_seconds() < 2:
                    raise RuntimeError('owner input returned; cancelling diagnostic')
                if time.monotonic() > deadline:
                    raise RuntimeError('owned protocol driver deadline')
                time.sleep(0.5)
            state['driverExitCode'] = driver.returncode
            try:
                state['browserExitCode'] = browser.wait(timeout=15)
            except subprocess.TimeoutExpired:
                state['shutdownDeadline'] = True
            state['phase'] = 'complete'
    except Exception as error:
        state.update(phase='failed', failure=str(error))
    finally:
        stop_owned(driver)
        stop_owned(browser)
        if server:
            server.shutdown()
            server.server_close()
        if server_thread:
            server_thread.join(timeout=5)
        state['cleanupComplete'] = True
        if lock.exists() and lock.stat().st_ino == identity:
            lock.unlink()
        save()
    return 0 if state['phase'] == 'complete' and state.get('driverExitCode') == 0 else 4


if __name__ == '__main__':
    raise SystemExit(main())
