#!/usr/bin/env python3
"""Replay a retained synthetic session COPY with native restore tracing."""
import argparse
import datetime
import hashlib
import http.server
import functools
import json
import os
import pathlib
import plistlib
import re
import shutil
import subprocess
import tempfile
import threading
import time
import urllib.request

from installed_navigation_probe import idle_seconds, app_running, stop_owned


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retained-profile', type=pathlib.Path, required=True)
    parser.add_argument('--session-file', required=True)
    parser.add_argument('--app', type=pathlib.Path, required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--lock-directory', type=pathlib.Path, required=True)
    parser.add_argument('--explicit-restore', action='store_true')
    parser.add_argument('--trusted-pref-probe', action='store_true',
                        help='observe/update the copied profile through native settings API')
    parser.add_argument('--trusted-pref-receipt', type=pathlib.Path,
                        help='use the own synthetic native-setter profile preference pair')
    parser.add_argument('--restore-lifecycle-receipt', type=pathlib.Path,
                        help='verify just the original three restore assertions from retained evidence')
    args = parser.parse_args()
    # Never accept a normal browser/user profile as the replay source.
    retained = args.retained_profile.resolve()
    assert retained.parent == pathlib.Path('/private/tmp') and retained.name.startswith('ahoi-split-profile.')
    original = retained / 'Default/Sessions' / args.session_file
    assert original.is_file() and args.session_file.startswith('Session_')
    args.output.mkdir(parents=True, exist_ok=False)
    state = dict(runnerPid=os.getpid(), phase='preflight', acceptance=False,
                 source=args.source, explicitRestore=args.explicit_restore,
                 trustedPrefProbe=args.trusted_pref_probe,
                 harnessSha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
                 cdpSha256=hashlib.sha256(pathlib.Path(__file__).with_name('cdp.mjs').read_bytes()).hexdigest(),
                 retainedSessionSha256=hashlib.sha256(original.read_bytes()).hexdigest())
    def save():
        (args.output / 'state.json').write_text(json.dumps(state, indent=2) + '\n')
    save()
    assert plistlib.loads((args.app / 'Contents/Info.plist').read_bytes())['AhoiSourceCommit'] == args.source
    restore_expectations = None
    if args.restore_lifecycle_receipt:
        assert args.trusted_pref_receipt and not args.trusted_pref_probe
        prior = json.loads(args.restore_lifecycle_receipt.read_text())
        assert prior['candidate'] == args.source
        assert any(r['name'] == args.session_file and r['sha256'] == state['retainedSessionSha256']
                   for r in prior['sessionEvidence'])
        root = pathlib.Path(__file__).resolve().parents[2]
        raw = (root / prior['rawDirectory']).resolve()
        assert raw.is_relative_to(root / 'artifacts/computer-use/m154')
        steps = raw / 'steps.txt'
        baseline = raw / 'snap-1-base.txt'
        for file in [steps, baseline]:
            assert hashlib.sha256(file.read_bytes()).hexdigest() == prior['rawSha256'][file.name]
        files = re.search(r'^before quit: (.+)$', steps.read_text(), re.MULTILINE).group(1).split()
        base = next(json.loads(line.split('|', 2)[2]) for line in baseline.read_text().splitlines()
                    if json.loads(line.split('|', 2)[2])['t'] == 'PaneA')
        restore_expectations = dict(files=files, width=base['w'], height=base['h'])
        assert 'g.html' in files and 'h.html' in files and 'solo.html' in files
        ax_source = pathlib.Path(__file__).with_name('axtool.swift')
        ax = pathlib.Path('/private/tmp/ahoi-axtool')
        assert ax.is_file() and ax.stat().st_mtime >= ax_source.stat().st_mtime
        state.update(restoreExpectations=restore_expectations,
                     restoreReceiptSha256=hashlib.sha256(args.restore_lifecycle_receipt.read_bytes()).hexdigest(),
                     axSourceSha256=hashlib.sha256(ax_source.read_bytes()).hexdigest(),
                     axBinarySha256=hashlib.sha256(ax.read_bytes()).hexdigest(),
                     sidebarHelperSha256=hashlib.sha256(pathlib.Path(__file__).with_name('split_sidebar_group.py').read_bytes()).hexdigest())
        save()
    locks = [args.lock_directory / n for n in ('build.lock', 'e2e.lock', 'h3.lock')]
    state.update(idleSeconds=idle_seconds(), appRunning=app_running(args.app))
    if state['idleSeconds'] < 90 or state['appRunning'] or any(p.exists() for p in locks):
        state['phase'] = 'deferred-owner-active'
        save()
        return 2
    for port in [9431, 8827]:
        if subprocess.run(['lsof', '-nP', f'-iTCP:{port}', '-sTCP:LISTEN'], capture_output=True).returncode == 0:
            state['phase'] = 'deferred-port-busy'
            save()
            return 2
    lock = locks[1]
    fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.write(fd, f'codex synthetic restore replay pid={os.getpid()}\n'.encode())
    os.close(fd)
    inode = lock.stat().st_ino
    browser = server = None
    try:
        profile = pathlib.Path(tempfile.mkdtemp(prefix='ahoi-restore-replay.', dir='/private/tmp'))
        shutil.copytree(retained, profile, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('Singleton*', 'LOCK', 'lockfile'))
        # Select the retained pre-relaunch session in the COPY only. Preserve
        # all original records; the later NTP file is not a restore input here.
        for file in (profile / 'Default/Sessions').glob('Session_*'):
            if file.name != args.session_file:
                file.unlink()
        prefs_path = profile / 'Default/Preferences'
        prefs = json.loads(prefs_path.read_text())
        prefs.setdefault('session', {})['restore_on_startup'] = 1
        prefs.setdefault('ahoi', {}).setdefault('session', {})['startup_mode'] = 'continue'
        prefs_path.write_text(json.dumps(prefs))
        if args.trusted_pref_receipt:
            receipt = json.loads(args.trusted_pref_receipt.read_text())
            assert receipt['source'] == args.source and receipt['phase'] == 'complete'
            assert receipt['cleanupComplete'] and receipt['originalSessionUnchanged']
            assert receipt['retainedSessionSha256'] == state['retainedSessionSha256']
            native = receipt['nativePrefAfter']['result']['value']
            assert native['setterSucceeded'] is True and native['value'] == 1
            trusted = pathlib.Path(receipt['profileCopy']).resolve()
            assert trusted.parent == pathlib.Path('/private/tmp')
            assert trusted.name.startswith('ahoi-restore-replay.')
            state['trustedPrefReceiptSha256'] = hashlib.sha256(args.trusted_pref_receipt.read_bytes()).hexdigest()
            for name in ['Preferences', 'Secure Preferences']:
                source = trusted / 'Default' / name
                assert source.is_file()
                shutil.copyfile(source, profile / 'Default' / name)
        site = profile / 'fixture'
        site.mkdir(exist_ok=True)
        for name in ['solo'] + list('abcdefgh'):
            title = 'Solo' if name == 'solo' else 'Pane' + name.upper()
            (site / (name + '.html')).write_text(f'<title>{title}</title><h1>{title}</h1>')
        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self, format, *values):
                with (args.output / 'site.log').open('a') as log:
                    log.write(format % values + '\n')
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 8827),
                 functools.partial(Handler, directory=str(site)))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        with urllib.request.urlopen('http://127.0.0.1:8827/solo.html', timeout=5) as response:
            assert response.status == 200
        state.update(profileCopy=str(profile), phase='replaying', mockKeychain=False)
        save()
        if idle_seconds() < 90 or app_running(args.app):
            raise RuntimeError('owner returned before replay launch')
        command = [str(args.app / 'Contents/MacOS/AhoiBrowser'), f'--user-data-dir={profile}',
                   '--no-first-run', '--no-default-browser-check', '--remote-debugging-port=9431',
                   '--enable-logging=stderr', '--vmodule=command_storage_backend=1,session_service_commands=1,session_restore=1']
        if args.explicit_restore:
            command.append('--restore-last-session')
        if args.trusted_pref_probe:
            command.append('chrome://settings/ahoi')
        with (args.output / 'browser.log').open('w') as log:
            browser = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        state['browserPid'] = browser.pid
        save()
        targets = []
        started = time.monotonic()
        while time.monotonic() - started < 25:
            if idle_seconds() < 2 or app_running(args.app, exclude_pid=browser.pid):
                raise RuntimeError('owner input/app returned during replay')
            try:
                with urllib.request.urlopen('http://127.0.0.1:9431/json', timeout=1) as response:
                    targets = json.load(response)
                state.setdefault('observations', []).append(dict(
                    elapsedSeconds=round(time.monotonic() - started, 1),
                    pages=[{k:t.get(k) for k in ['id', 'url', 'title']} for t in targets if t.get('type') == 'page']))
                save()
            except (OSError, ValueError):
                pass
            if args.trusted_pref_probe:
                settings = next((t for t in targets if t.get('type') == 'page' and
                                 t.get('url', '').startswith('chrome://settings')), None)
                # Startup can substitute an NTP. Navigate the owned diagnostic
                # tab explicitly; absence of a settings target is not a readback.
                if not settings and not state.get('settingsNavigationAttempted'):
                    page = next((t for t in targets if t.get('type') == 'page'), None)
                    if page:
                        cdp = pathlib.Path(__file__).with_name('cdp.mjs')
                        navigation = subprocess.run(['node', str(cdp), '9431', page['id'],
                            'Page.navigate', json.dumps(dict(url='chrome://settings/ahoi'))],
                            capture_output=True, text=True, timeout=12)
                        state['settingsNavigationAttempted'] = True
                        state['settingsNavigation'] = json.loads(navigation.stdout)
                        save()
                if settings:
                    cdp = pathlib.Path(__file__).with_name('cdp.mjs')
                    expression = "new Promise(resolve => chrome.settingsPrivate.getPref('session.restore_on_startup', p => resolve({value:p.value, controlledBy:p.controlledBy||''})))"
                    before = subprocess.run(['node', str(cdp), '9431', settings['id'], 'Runtime.evaluate',
                        json.dumps(dict(expression=expression, awaitPromise=True, returnByValue=True))],
                        capture_output=True, text=True, timeout=12)
                    state['nativePrefBefore'] = json.loads(before.stdout)
                    if before.returncode or 'exceptionDetails' in state['nativePrefBefore']:
                        raise RuntimeError('native settings getter failed')
                    expression = "new Promise(resolve => chrome.settingsPrivate.setPref('session.restore_on_startup', 1, '', ok => chrome.settingsPrivate.getPref('session.restore_on_startup', p => resolve({setterSucceeded:ok,value:p.value}))))"
                    after = subprocess.run(['node', str(cdp), '9431', settings['id'], 'Runtime.evaluate',
                        json.dumps(dict(expression=expression, awaitPromise=True, returnByValue=True))],
                        capture_output=True, text=True, timeout=12)
                    state['nativePrefAfter'] = json.loads(after.stdout)
                    if after.returncode or 'exceptionDetails' in state['nativePrefAfter']:
                        raise RuntimeError('native settings setter failed')
                    native = state['nativePrefAfter'].get('result', {}).get('value', {})
                    if native.get('setterSucceeded') is not True or native.get('value') != 1:
                        raise RuntimeError('native settings setter did not confirm restore preference')
                    save()
                    break
            time.sleep(1)
        if args.trusted_pref_probe and 'nativePrefAfter' not in state:
            raise RuntimeError('native preference probe did not obtain a settings readback')
        if restore_expectations:
            # Read only owned targets and native AX; no activation, key or drag.
            def owner_check():
                if idle_seconds() < 2 or app_running(args.app, exclude_pid=browser.pid):
                    raise RuntimeError('owner input/app returned during restore assertions')
            def cdp_call(target, method, params):
                owner_check()
                result = subprocess.run(['node', str(pathlib.Path(__file__).with_name('cdp.mjs')),
                            '9431', target, method, json.dumps(params)],
                            capture_output=True, text=True, timeout=12)
                data = json.loads(result.stdout)
                if result.returncode or 'error' in data or 'exceptionDetails' in data:
                    raise RuntimeError('restore capture protocol failed: ' + method)
                return data
            rows = []
            site_prefix = 'http://127.0.0.1:8827/'
            (args.output / 'restore-targets.json').write_text(json.dumps(targets, indent=2) + '\n')
            for target in targets:
                if target.get('type') != 'page' or not target.get('url', '').startswith(site_prefix):
                    continue
                sample = cdp_call(target['id'], 'Runtime.evaluate', dict(
                    expression='({t:document.title,w:innerWidth,h:innerHeight,v:document.visibilityState,href:location.href,ready:document.readyState})',
                    returnByValue=True))['result']['value']
                sample.update(id=target['id'], url=target['url'],
                              win=cdp_call(target['id'], 'Browser.getWindowForTarget', {})['windowId'])
                rows.append(sample)
            (args.output / 'restore-documents.json').write_text(json.dumps(rows, indent=2) + '\n')
            owner_check()
            dump = subprocess.run([str(ax), 'dump', str(browser.pid), '40'],
                                  capture_output=True, text=True, timeout=12)
            assert dump.returncode == 0
            ax_file = args.output / 'restore-sidebar.txt'
            ax_file.write_text(dump.stdout)
            group = subprocess.run(['python3', str(pathlib.Path(__file__).with_name('split_sidebar_group.py')),
                                    str(ax_file), 'PaneG', 'PaneH'], capture_output=True, text=True, timeout=5)
            (args.output / 'restore-sidebar-group.txt').write_text(group.stdout + group.stderr)
            g = next((r for r in rows if r['t'] == 'PaneG'), None)
            h = next((r for r in rows if r['t'] == 'PaneH'), None)
            solo = next((r for r in rows if r['t'] == 'Solo'), None)
            panes = [r for r in rows if g and r['win'] == g['win'] and r['t'] != 'Solo']
            # Same hidden-pane layout predicate and baseline as the original q.
            two_columns = (len(panes) == 2 and
                sum(r['h'] / restore_expectations['height'] > 0.8 for r in panes) == 2 and
                sum(r['w'] / restore_expectations['width'] > 0.8 for r in panes) == 0)
            state['restoreChecks'] = dict(
                restoredSecondWindowSplit=bool(g and h and solo and g['win'] == h['win'] and solo['win'] != g['win'] and two_columns),
                restoredWithoutPhantomTabs=sorted(r['url'].removeprefix(site_prefix) for r in rows) == sorted(restore_expectations['files']),
                restoredSecondWindowOneRow=group.returncode == 0)
            state['restoreVerdictPass'] = all(state['restoreChecks'].values())
            state['restoreAcceptanceScope'] = 'original three installed lifecycle restore assertions only'
            assert all(r['ready'] == 'complete' and r['href'] == r['url'] for r in rows)
            owner_check()
            save()
        target = next((t for t in targets if t.get('type') == 'page'), None)
        if target:
            result = subprocess.run(['node', str(pathlib.Path(__file__).with_name('cdp.mjs')), '9431',
                        target['id'], 'Browser.close', '{}'], capture_output=True, text=True, timeout=12)
            state['closeProtocolExitCode'] = result.returncode
        state['browserExitCode'] = browser.wait(timeout=20)
        state['phase'] = 'complete'
    except Exception as error:
        state.update(phase='failed', failure=str(error))
    finally:
        stop_owned(browser)
        if server:
            server.shutdown()
            server.server_close()
        assert hashlib.sha256(original.read_bytes()).hexdigest() == state['retainedSessionSha256']
        state['originalSessionUnchanged'] = True
        state['cleanupComplete'] = True
        save()
        if lock.exists() and lock.stat().st_ino == inode:
            lock.unlink()
    if state['phase'] != 'complete':
        return 4
    return 1 if state.get('restoreVerdictPass') is False else 0


if __name__ == '__main__':
    raise SystemExit(main())
