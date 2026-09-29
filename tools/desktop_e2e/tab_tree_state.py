# Read-only view of an Ahoi profile's "Ahoi Tab Tree" SQLite database for
# installed-app journeys (ws-merge, cmd-move). It works on a copy of the
# database and its WAL, so the running browser's file is never opened.
# usage: tab_tree_state.py <db> dump|wsid|wsstate|nodeid|nodeinfo|roots|
#        children|undocount|integrity [args]
import json, os, shutil, sqlite3, sys, tempfile
db, cmd, args = sys.argv[1], sys.argv[2], sys.argv[3:]
tmp = tempfile.mkdtemp()
for suffix in ("", "-wal", "-shm", "-journal"):
    if os.path.exists(db + suffix):
        shutil.copy(db + suffix, os.path.join(tmp, "t" + suffix))
c = sqlite3.connect(os.path.join(tmp, "t"))
ws = [dict(zip(("id", "name", "tomb", "merged"), r)) for r in
      c.execute("select id,name,tombstone,merged_into from workspaces")]
nodes = [dict(zip(("id", "ws", "parent", "type", "title", "url", "sort", "tomb", "temp"), r)) for r in
         c.execute("select id,workspace_id,parent_id,node_type,title,url,sort_key,tombstone,is_temporary from tree_nodes")]
live = [n for n in nodes if not n["tomb"]]
label = lambda n: n["title"] if n["type"] == 0 else (n["url"].rsplit("/", 1)[-1] or n["url"])
def find(key):
    if key.startswith("title:"):
        hits = [n for n in live if n["title"] == key[6:]]
    else:
        hits = [n for n in live if key in n["url"]]
    return hits[0] if hits else None
if cmd == "dump":
    print(json.dumps({"workspaces": ws, "nodes": live}, indent=1))
elif cmd == "wsid":
    print(next((w["id"] for w in ws if w["name"] == args[0] and not w["tomb"]), ""))
elif cmd == "wsstate":  # live|<name> or gone|<merged_into>
    w = next((w for w in ws if w["id"] == args[0]), None)
    print("missing" if not w else ("gone|%s" % (w["merged"] or "") if w["tomb"] else "live|%s" % w["name"]))
elif cmd == "nodeid":
    n = find(args[0]); print(n["id"] if n else "")
elif cmd == "nodeinfo":  # <workspace> <parent or -> <type> <temporary>
    n = next((n for n in live if n["id"] == args[0]), None)
    print("%s %s %d %d" % (n["ws"], n["parent"] or "-", n["type"], n["temp"]) if n else "missing")
elif cmd == "roots":  # ordered root labels of a Workspace
    print(",".join(label(n) for n in sorted((n for n in live if n["ws"] == args[0] and not n["parent"]),
                                            key=lambda n: (n["sort"], n["id"]))))
elif cmd == "children":
    print(",".join(label(n) for n in sorted((n for n in live if n["parent"] == args[0]),
                                            key=lambda n: (n["sort"], n["id"]))))
elif cmd == "undocount":
    print(c.execute("select count(*) from undo_operations").fetchone()[0])
elif cmd == "integrity":
    print(c.execute("pragma integrity_check").fetchone()[0])
shutil.rmtree(tmp)
