#!/bin/bash
# usage: menu-key-equivalents-probe.sh <App.app> <outdir>
# Read-only diagnostic for WORKFLOW-03: lists every main-menu item of a fresh
# profile together with its key equivalent (AXMenuItemCmdChar,
# AXMenuItemCmdVirtualKey, AXMenuItemCmdModifiers), so a menu item that owns
# ⌥⇥ (and therefore swallows it before any window) becomes visible. No input
# events. Results: <outdir>/menu-key-equivalents.tsv and tab-items.txt.
set -u
APP=$1; OUT=$2
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-menu-profile.XXXXXX)
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  about:blank > "$OUT/browser.log" 2>&1 &
PID=$!; trap 'kill $PID 2>/dev/null; sleep 2; rm -rf "$P"' EXIT
sleep 10
osascript -l JavaScript - "$PID" > "$OUT/menu-key-equivalents.tsv" 2> "$OUT/osascript.err" <<'JS'
function run(argv) {
  const se = Application("System Events");
  const proc = se.processes.whose({unixId: parseInt(argv[0])})[0];
  const rows = [];
  const attr = (el, name) => { try { return el.attributes.byName(name).value(); } catch (e) { return ""; } };
  const walk = (menu, path) => {
    let items = [];
    try { items = menu.menuItems(); } catch (e) { return; }
    for (const item of items) {
      let title = "";
      try { title = item.name() || ""; } catch (e) {}
      const ch = attr(item, "AXMenuItemCmdChar");
      const vk = attr(item, "AXMenuItemCmdVirtualKey");
      const mods = attr(item, "AXMenuItemCmdModifiers");
      if (ch !== "" || vk !== "") rows.push([path, title, JSON.stringify(ch), vk, mods].join("\t"));
      let subs = [];
      try { subs = item.menus(); } catch (e) {}
      for (const sub of subs) walk(sub, path + " > " + title);
    }
  };
  for (const bar of proc.menuBars()[0].menuBarItems()) {
    let name = ""; try { name = bar.name(); } catch (e) {}
    for (const m of bar.menus()) walk(m, name);
  }
  return rows.join("\n");
}
JS
# kVK_Tab is 48; the modifier mask bit 3 (8) means "no Command", bit 1 (2) Option.
awk -F'\t' '$4 == 48 || $3 == "\"\\t\"" || $3 == "\"⇥\""' "$OUT/menu-key-equivalents.tsv" > "$OUT/tab-items.txt"
wc -l < "$OUT/menu-key-equivalents.tsv" | tr -d ' ' | sed 's/^/items with key equivalents: /' > "$OUT/summary.txt"
echo "tab items:" >> "$OUT/summary.txt"; cat "$OUT/tab-items.txt" >> "$OUT/summary.txt"
cat "$OUT/summary.txt"
