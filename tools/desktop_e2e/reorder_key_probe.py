"""Bounded real-key diagnosis on the caller's owned synthetic replay only."""
import hashlib
import json
import pathlib
import re
import subprocess
import time
import urllib.request

from installed_navigation_probe import app_running, idle_seconds


def probe_reorder_key(app, browser, output, site_port, state, save, reduce_to_three=False,
                      cycle_layouts=False, menu_presets=False, hid_dismiss_menus=False):
    source = pathlib.Path(__file__).parent
    ax = pathlib.Path('/private/tmp/ahoi-axtool')
    assert ax.is_file() and ax.stat().st_mtime >= (source / 'axtool.swift').stat().st_mtime
    state['axBinarySha256'] = hashlib.sha256(ax.read_bytes()).hexdigest()
    state['axSourceSha256'] = hashlib.sha256((source / 'axtool.swift').read_bytes()).hexdigest()
    state['reorderKeyObservations'] = []
    prefix = f'http://127.0.0.1:{site_port}/'

    def own_focus():
        if browser.poll() is not None or app_running(app, exclude_pid=browser.pid):
            raise RuntimeError('owned key probe browser is gone or another app owner returned')
        focus = subprocess.run([str(ax), 'focused', str(browser.pid)],
                               capture_output=True, text=True, timeout=5)
        lines = focus.stdout.splitlines()
        if not lines or f' pid={browser.pid} target={browser.pid}' not in lines[0]:
            raise RuntimeError('owner focus returned during reorder key probe')

    def cdp(target, method, params):
        own_focus()
        result = subprocess.run(['node', str(source / 'cdp.mjs'), '9431', target, method,
                                 json.dumps(params)], capture_output=True, text=True, timeout=12)
        data = json.loads(result.stdout)
        if result.returncode or 'error' in data or 'exceptionDetails' in data:
            raise RuntimeError('owned reorder protocol capture failed')
        return data

    def snapshot(label):
        own_focus()
        with urllib.request.urlopen('http://127.0.0.1:9431/json', timeout=2) as response:
            targets = json.load(response)
        rows = []
        for target in targets:
            if target.get('type') != 'page' or not target.get('url', '').startswith(prefix):
                continue
            value = cdp(target['id'], 'Runtime.evaluate', dict(
                expression='({t:document.title,w:innerWidth,h:innerHeight,f:document.hasFocus(),v:document.visibilityState,href:location.href,ready:document.readyState,m:window.__m||"",val:document.getElementById("f")?.value||""})',
                returnByValue=True))['result']['value']
            value.update(id=target['id'], url=target['url'])
            rows.append(value)
        item = dict(label=label, rows=rows)
        state['reorderKeyObservations'].append(item)
        save()
        return rows

    def key(code, *modifiers):
        own_focus()
        result = subprocess.run([str(ax), 'hidkey', str(browser.pid), str(code), *modifiers],
                                capture_output=True, text=True, timeout=5)
        with (output / 'reorder-keys.txt').open('a') as file:
            file.write(f'{code} {" ".join(modifiers)}: ' + result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('owned reorder key refused')

    def ax_action(action, *args):
        own_focus()
        result = subprocess.run([str(ax), action, str(browser.pid), *args],
                                capture_output=True, text=True, timeout=12)
        with (output / 'reorder-menu-actions.txt').open('a') as file:
            file.write(f'{action} {args}: ' + result.stdout + result.stderr)
        return result

    def native_dump(label):
        result = ax_action('dump', '40')
        if result.returncode:
            raise RuntimeError('owned native menu capture failed')
        (output / f'reorder-menu-{label}.txt').write_text(result.stdout)
        return result.stdout

    def wait_menu(text, label):
        deadline = time.monotonic() + 5
        while True:
            dump = native_dump(label)
            if f'AXMenuItem | {text}' in dump:
                return
            if time.monotonic() >= deadline:
                raise RuntimeError('native preset menu did not expose ' + text)
            time.sleep(0.25)

    def preset(item, previous, label):
        dump = native_dump(label + '-row')
        row = re.search(r'(AX(?:RadioButton|Tab|Row|Cell|Button)) \| ([^|\n]*PaneA[^|\n]*)', dump)
        if not row:
            raise RuntimeError('owned PaneA sidebar row missing')
        # The same native row/submenu actions and check-mark sequence as matrix.
        if hid_dismiss_menus:
            key(53)
        else:
            ax_action('key', '53')
        time.sleep(1)
        ax_action('press', row.group(1) + ':' + row.group(2).strip(), 'AXShowMenu')
        wait_menu('Geteilte Ansicht anordnen', label + '-context')
        ax_action('press', 'AXMenuItem:Geteilte Ansicht anordnen')
        time.sleep(1)
        wait_menu(item or previous, label + '-submenu')
        if previous and ax_action('checked', 'AXMenuItem:' + previous).returncode:
            raise RuntimeError('native previous preset mark missing')
        if item:
            ax_action('press', 'AXMenuItem:' + item)
        else:
            if hid_dismiss_menus:
                key(53)
            else:
                ax_action('key', '53')
            time.sleep(0.5)
            if hid_dismiss_menus:
                key(53)
            else:
                ax_action('key', '53')
        time.sleep(2)

    if idle_seconds() < 90 or app_running(app, exclude_pid=browser.pid):
        raise RuntimeError('owner input/app returned before owned key activation')
    activated = subprocess.run([str(ax), 'activate', str(browser.pid)],
                               capture_output=True, text=True, timeout=5)
    (output / 'reorder-activation.txt').write_text(activated.stdout + activated.stderr)
    own_focus()
    if reduce_to_three:
        key(21, 'cmd', 'ctrl')
        time.sleep(1)
        fourth = snapshot('fourth-pane-before-close')
        assert [r['t'] for r in fourth if r['f'] and r['v'] == 'visible'] == ['PaneD']
        key(13, 'cmd', 'ctrl')
        time.sleep(3)
        remaining = snapshot('three-panes-after-close')
        assert sorted(r['t'] for r in remaining if r['v'] == 'visible') == ['PaneA', 'PaneB', 'PaneC']
    if cycle_layouts or menu_presets:
        for row in remaining:
            cdp(row['id'], 'Runtime.evaluate', dict(
                expression='window.__m="kept";document.getElementById("f").value=document.title+"-draft";1',
                returnByValue=True))
        if cycle_layouts:
            for index in range(6):
                key(37, 'cmd', 'ctrl')
                time.sleep(2)
                snapshot(f'three-layout-cycle-{index + 1}')
        if menu_presets:
            previous = ''
            for index, item in enumerate(['Drei Zeilen', 'Großes Pane links', 'Großes Pane rechts',
                                           'Großes Pane oben', 'Großes Pane unten', 'Drei Spalten']):
                preset(item, previous, f'preset-{index + 1}')
                snapshot(f'after-menu-preset-{index + 1}')
                previous = item
            preset('', previous, 'final-mark')
        for code in [18, 19, 20]:
            key(code, 'cmd', 'ctrl')
            time.sleep(1)
    key(18, 'cmd', 'ctrl')
    time.sleep(1)
    before = snapshot('focused-pane-one-before')
    focused = [r['t'] for r in before if r['f'] and r['v'] == 'visible']
    assert len(focused) == 1 and focused[0].startswith('Pane')
    assert all(r['ready'] == 'complete' and r['href'] == r['url'] for r in before)
    own_focus()
    dump = subprocess.run([str(ax), 'dump', str(browser.pid), '40'],
                          capture_output=True, text=True, timeout=12)
    (output / 'reorder-before-key-ax.txt').write_text(dump.stdout)
    window_part = dump.stdout.split('AXMenuBar', 1)[0]
    state['trackingMenuPresentBeforeReorder'] = bool(re.search(r'^\s*AXMenu(?:\s|$)', window_part, re.MULTILINE))
    save()
    if hid_dismiss_menus and state['trackingMenuPresentBeforeReorder']:
        raise RuntimeError('native tracking menu remained after HID dismissal')
    key(124, 'cmd', 'ctrl', 'shift')
    for delay in [0.1, 0.5, 1.0]:
        time.sleep(delay)
        snapshot(f'after-reorder-delay-{delay}')
    key(19, 'cmd', 'ctrl')
    time.sleep(1)
    pane_two = snapshot('focused-pane-two-after')
    key(18, 'cmd', 'ctrl')
    time.sleep(1)
    snapshot('focused-pane-one-after')
    state['reorderFocusedBefore'] = focused[0]
    state['reorderFocusedSecondAfter'] = [r['t'] for r in pane_two if r['f'] and r['v'] == 'visible']
    state['reorderDiagnosticComplete'] = True
    save()
