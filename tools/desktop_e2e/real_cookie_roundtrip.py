#!/usr/bin/env python3
"""Real installed cookie encryption/restart check using synthetic data only."""
import argparse
import datetime
import hashlib
import http.server
import json
import os
import pathlib
import plistlib
import sqlite3
import subprocess
import tempfile
import threading
import time
import urllib.request

from installed_navigation_probe import idle_seconds, app_running, stop_owned


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=pathlib.Path, required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--lock-directory', type=pathlib.Path, required=True)
    parser.add_argument('--port', type=int, default=9421)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    state = dict(runnerPid=os.getpid(), phase='preflight', source=args.source,
                 mockKeychain=False, headless=False, pass_=False, checks={},
                 startedAt=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                 harnessSha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest())
    def save():
        (args.output / 'state.json').write_text(json.dumps(state, indent=2) + '\n')
    save()
    assert plistlib.loads((args.app / 'Contents/Info.plist').read_bytes())['AhoiSourceCommit'] == args.source
    locks = [args.lock_directory / n for n in ('build.lock', 'e2e.lock', 'h3.lock')]
    state.update(idleSeconds=idle_seconds(), appRunning=app_running(args.app))
    if state['idleSeconds'] < 90 or state['appRunning'] or any(p.exists() for p in locks):
        state['phase'] = 'deferred-owner-active'
        save()
        return 2
    if subprocess.run(['lsof', '-nP', f'-iTCP:{args.port}', '-sTCP:LISTEN'],
                      capture_output=True).returncode == 0:
        state['phase'] = 'deferred-port-busy'
        save()
        return 2
    lock = locks[1]
    fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.write(fd, f'codex real synthetic cookie pid={os.getpid()}\n'.encode())
    os.close(fd)
    inode = lock.stat().st_ino
    browser = server = None
    cookie_name = 'ahoi_roundtrip_fixture'
    cookie_value = 'synthetic-persistent-value'
    requests = []
    try:
        profile = pathlib.Path(tempfile.mkdtemp(prefix='ahoi-cookie-profile.', dir='/private/tmp'))
        state['profile'] = str(profile)
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                matched = f'{cookie_name}={cookie_value}' in self.headers.get('Cookie', '').split('; ')
                requests.append(dict(path=self.path, syntheticCookieMatched=matched))
                (args.output / 'requests.json').write_text(json.dumps(requests, indent=2) + '\n')
                title = 'CookieSet' if self.path == '/set' else ('CookieRestored' if matched else 'CookieMissing')
                body = f'<title>{title}</title><h1>{title}</h1>'.encode()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                if self.path == '/set':
                    self.send_header('Set-Cookie', f'{cookie_name}={cookie_value}; Max-Age=3600; Path=/; HttpOnly; SameSite=Lax')
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *_):
                pass
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        origin = f'http://127.0.0.1:{server.server_port}'
        state['fixtureOrigin'] = origin
        with urllib.request.urlopen(origin + '/ready', timeout=5) as response:
            assert response.status == 200
        cdp = pathlib.Path(__file__).with_name('cdp.mjs')
        state['cdpSha256'] = hashlib.sha256(cdp.read_bytes()).hexdigest()
        def check_owner():
            if idle_seconds() < 2 or app_running(args.app, exclude_pid=browser.pid):
                raise RuntimeError('owner input/app returned; cancel synthetic cookie run')
        def command(target, method, params):
            check_owner()
            result = subprocess.run(['node', str(cdp), str(args.port), target, method,
                                     json.dumps(params)], capture_output=True, text=True, timeout=12)
            value = json.loads(result.stdout)
            if result.returncode or 'error' in value:
                raise RuntimeError(f'owned protocol command failed: {method}')
            return value
        def launch(path, title, phase):
            nonlocal browser
            state['phase'] = phase
            save()
            # Both modes are real/windowed; no mock or sandbox exception.
            with (args.output / f'{phase}-browser.log').open('w') as log:
                browser = subprocess.Popen([
                    str(args.app / 'Contents/MacOS/AhoiBrowser'), f'--user-data-dir={profile}',
                    '--no-first-run', '--no-default-browser-check',
                    f'--remote-debugging-port={args.port}', origin + path],
                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            state.setdefault('browserPids', []).append(browser.pid)
            save()
            deadline = time.monotonic() + 35
            while time.monotonic() < deadline:
                check_owner()
                if browser.poll() is not None:
                    raise RuntimeError('owned browser exited before cookie document')
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{args.port}/json', timeout=1) as response:
                        targets = json.load(response)
                    target = next((t for t in targets if t.get('type') == 'page' and
                                   t.get('url') == origin + path), None)
                except (OSError, ValueError):
                    target = None
                if target:
                    value = command(target['id'], 'Runtime.evaluate', {
                        'expression': 'JSON.stringify({href:location.href,title:document.title,ready:document.readyState})',
                        'returnByValue': True})
                    text = value.get('result', {}).get('value')
                    if text:
                        doc = json.loads(text)
                        if doc.get('title') == title and doc.get('ready') == 'complete':
                            state.setdefault('documents', []).append(doc)
                            save()
                            return target['id']
                time.sleep(0.5)
            raise RuntimeError('real cookie fixture document did not commit')
        def close(target, phase):
            command(target, 'Browser.close', {})
            code = browser.wait(timeout=20)
            state.setdefault('browserExitCodes', []).append(code)
            state['checks'][phase] = code == 0
            save()
            if code != 0:
                raise RuntimeError('owned native browser close failed')
        target = launch('/set', 'CookieSet', 'first-launch')
        # Force the cookie through the actual native cookie manager/server path.
        command(target, 'Page.navigate', {'url': origin + '/read'})
        deadline = time.monotonic() + 10
        while not any(x['path'] == '/read' and x['syntheticCookieMatched'] for x in requests):
            check_owner()
            if time.monotonic() > deadline:
                raise RuntimeError('server did not receive synthetic cookie before close')
            time.sleep(0.5)
        state['checks']['serverCookieBeforeClose'] = True
        close(target, 'firstNativeClose')
        # Read only the owned fixture's database after native shutdown.
        database = next((p for p in (profile / 'Default/Network/Cookies', profile / 'Default/Cookies') if p.is_file()), None)
        if database is None:
            raise RuntimeError('owned native cookie database absent')
        with sqlite3.connect(database.as_uri() + '?mode=ro', uri=True) as connection:
            row = connection.execute('SELECT value, encrypted_value, expires_utc FROM cookies WHERE name=?', (cookie_name,)).fetchone()
        if row is None:
            raise RuntimeError('synthetic cookie was not persisted')
        plain, encrypted, expiry = row
        state['database'] = dict(path=str(database), syntheticRowPresent=True,
                                 clearValueEmpty=plain == '', encryptedBytes=len(encrypted),
                                 encryptedPrefix=bytes(encrypted[:3]).hex(), persistentExpiry=expiry > 0)
        state['checks']['encryptedAtRest'] = plain == '' and len(encrypted) > 3 and expiry > 0
        if not state['checks']['encryptedAtRest']:
            raise RuntimeError('synthetic cookie encryption/persistence not proved')
        before = len(requests)
        target = launch('/read', 'CookieRestored', 'second-launch')
        state['checks']['serverCookieAfterRestart'] = any(
            x['path'] == '/read' and x['syntheticCookieMatched'] for x in requests[before:])
        close(target, 'secondNativeClose')
        state.update(phase='complete', pass_=all(state['checks'].values()))
    except Exception as error:
        state.update(phase='failed', failure=str(error))
    finally:
        stop_owned(browser)
        if server:
            server.shutdown()
            server.server_close()
        state['cleanupComplete'] = True
        save()
        if lock.exists() and lock.stat().st_ino == inode:
            lock.unlink()
    return 0 if state['phase'] == 'complete' and state['pass_'] else 4


if __name__ == '__main__':
    raise SystemExit(main())
