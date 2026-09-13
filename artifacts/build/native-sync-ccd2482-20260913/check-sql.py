"""Narrow SQL regression against the exact built Chromium SQLite library.

Only synthetic in-memory rows are used; no profile or Keychain is opened.
This checks SQL compatibility and ACK clock ordering, not real transport/E2E.
"""
import ctypes
import pathlib
import re
import subprocess
import sys

repo = pathlib.Path(sys.argv[1])
library = pathlib.Path(sys.argv[2])
sqlite = ctypes.CDLL(str(library))
initialize = sqlite.chrome_sqlite3_initialize
initialize.restype = ctypes.c_int
assert initialize() == 0
open_db = sqlite.chrome_sqlite3_open
open_db.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
open_db.restype = ctypes.c_int
db = ctypes.c_void_p()
assert open_db(b":memory:", ctypes.byref(db)) == 0
callback_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                                ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_char_p))
execute = sqlite.chrome_sqlite3_exec
execute.argtypes = [ctypes.c_void_p, ctypes.c_char_p, callback_type,
                    ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p)]
execute.restype = ctypes.c_int

def run(sql, expected=0):
    rows = []
    @callback_type
    def collect(_, count, values, names):
        rows.append(tuple(values[i].decode() if values[i] else None for i in range(count)))
        return 0
    error = ctypes.c_char_p()
    result = execute(db, sql.encode(), collect, None, ctypes.byref(error))
    assert result == expected, (result, error.value, sql)
    return rows

def statement(source, variable):
    expression = source.split(f"sql::Statement {variable}(", 1)[1].split("));", 1)[0]
    return "".join(re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', expression))

ack_path = "overlay/chromium/src/ahoi/browser/sync/sync_store_acknowledgements.cc"
native_path = "overlay/chromium/src/ahoi/browser/sync/native_tree_sync_journal.cc"
baseline = subprocess.check_output(["git", "-C", str(repo), "show", f"e4de9e1:{ack_path}"], text=True)
ack = statement((repo / ack_path).read_text(), "receipt")
native = statement((repo / native_path).read_text(), "observed")
run("CREATE TABLE sync_outbox(mutation_id TEXT PRIMARY KEY,entity_type INTEGER,entity_id TEXT,"
    "version_model INTEGER,version_physical INTEGER,version_logical INTEGER,version_device TEXT);")
run("CREATE TABLE sync_acknowledged_records(entity_type INTEGER,entity_id TEXT,version_model INTEGER,"
    "version_physical INTEGER,version_logical INTEGER,version_device TEXT,PRIMARY KEY(entity_type,entity_id));")
run("CREATE TABLE sync_native_tree_observations(receipt_id TEXT PRIMARY KEY,payload TEXT);")
run(statement(baseline, "receipt"), expected=1)
print("PASS: exact original UPSERT is rejected by Chromium SQLite")

for mutation, physical, logical, device, expected in [
    ("first", 20, 0, "a", ("20", "0", "a")),
    ("old", 10, 9, "z", ("20", "0", "a")),
    ("logical", 20, 1, "a", ("20", "1", "a")),
    ("tie", 20, 1, "b", ("20", "1", "b")),
    ("equal", 20, 1, "b", ("20", "1", "b")),
]:
    run(f"INSERT INTO sync_outbox VALUES('{mutation}',1,'entity',3,{physical},{logical},'{device}');")
    run(ack.replace("?", f"'{mutation}'", 1))
    assert run("SELECT version_physical,version_logical,version_device FROM sync_acknowledged_records") == [expected]
print("PASS: initial, stale, logical, device-tiebreak and equal ACK clocks")
run(native.replace("?", "'receipt'", 1).replace("?", "'first'", 1))
run(native.replace("?", "'receipt'", 1).replace("?", "'second'", 1))
assert run("SELECT receipt_id,payload FROM sync_native_tree_observations") == [("receipt", "second")]
print("PASS: native observation receipt is replaced once")
sqlite.chrome_sqlite3_close.argtypes = [ctypes.c_void_p]
assert sqlite.chrome_sqlite3_close(db) == 0
