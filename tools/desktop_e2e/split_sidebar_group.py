#!/usr/bin/env python3
"""Check a captured native AX split group; shared by journeys and replay."""
import re, sys
lines = open(sys.argv[1]).read().splitlines(); want = sys.argv[2:]
row = re.compile(r'^( *)AX(RadioButton|Tab|Row|Cell|Button)\b[^|]* \| (.*)$')
indent = lambda l: len(l) - len(l.lstrip(" "))
def parent(i):
    for j in range(i - 1, -1, -1):
        if indent(lines[j]) < indent(lines[i]): return j
    return -1
rows = [(i, m.group(3)) for i, l in enumerate(lines) for m in [row.match(l)] if m]
found = {}
for t in want:
    hit = next((i for i, text in rows if any(p.startswith(t) for p in text.split(" | "))), None)
    if hit is not None: found[t] = parent(hit)
parents = set(found.values())
p = parents.pop() if len(parents) == 1 else -1
members = [i for i, _ in rows if p >= 0 and parent(i) == p]
solo = next((i for i, text in rows if text.split(" | ")[0].startswith("Solo")), None)
ok = (len(found) == len(want) and p >= 0 and lines[p].strip().startswith("AXGroup")
      and len(members) == len(want) and (solo is None or parent(solo) != p))
print("sidebar group %s: found=%s parent=%r members=%d -> %s" % (want, sorted(found), lines[p].strip() if p >= 0 else None, len(members), ok))
sys.exit(0 if ok else 1)
