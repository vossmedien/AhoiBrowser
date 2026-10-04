#!/usr/bin/env python3
"""Bounded direct byte/link/metadata comparison for this SSD takeover."""
import argparse
from collections import deque
from concurrent.futures import ThreadPoolExecutor
import datetime
import ctypes
import json
import os
from pathlib import Path
import stat
import sys


# macOS Python does not expose Linux's os.listxattr/getxattr API.
libc = ctypes.CDLL(None, use_errno=True)
libc.listxattr.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
libc.listxattr.restype = ctypes.c_ssize_t
libc.getxattr.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
                        ctypes.c_size_t, ctypes.c_uint32, ctypes.c_int]
libc.getxattr.restype = ctypes.c_ssize_t


def xattrs(path):
    encoded = os.fsencode(path)
    def checked(value):
        if value < 0:
            error = ctypes.get_errno(); raise OSError(error, os.strerror(error))
        return value
    size = checked(libc.listxattr(encoded, None, 0, 1))
    if not size: return {}
    buffer = ctypes.create_string_buffer(size)
    actual = checked(libc.listxattr(encoded, buffer, size, 1))
    result = {}
    for name in buffer.raw[:actual].split(b'\0'):
        if not name: continue
        length = checked(libc.getxattr(encoded, name, None, 0, 0, 1))
        value = ctypes.create_string_buffer(length)
        read = checked(libc.getxattr(encoded, name, value, length, 0, 1))
        result[name] = value.raw[:read]
    return result


def metadata(info):
    # rsync protocol29 preserves mtime seconds; atime is changed by reads.
    return (stat.S_IMODE(info.st_mode), info.st_uid, info.st_gid,
            int(info.st_mtime), getattr(info, 'st_flags', 0))


def compare_node(source, destination, relative):
    result = {'path': str(relative), 'bytes': 0, 'errors': []}
    try:
        a, b = source.lstat(), destination.lstat()
        if stat.S_IFMT(a.st_mode) != stat.S_IFMT(b.st_mode):
            result['errors'].append('type'); return result
        if metadata(a) != metadata(b):
            result['errors'].append('metadata')
        if stat.S_ISLNK(a.st_mode):
            if os.readlink(source) != os.readlink(destination):
                result['errors'].append('symlink-target')
        elif stat.S_ISREG(a.st_mode):
            result['bytes'] = a.st_size
            if a.st_nlink > 1 or b.st_nlink > 1:
                result['hardlink'] = ((a.st_dev, a.st_ino), (b.st_dev, b.st_ino))
            if a.st_size != b.st_size:
                result['errors'].append('size')
            else:
                with source.open('rb') as left, destination.open('rb') as right:
                    while True:
                        x, y = left.read(1024 * 1024), right.read(1024 * 1024)
                        if x != y:
                            result['errors'].append('content'); break
                        if not x: break
                # Detect a source/destination mutation during the comparison.
                for path, before in ((source, a), (destination, b)):
                    after = path.lstat()
                    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                        result['errors'].append('changed-during-read')
        elif not stat.S_ISDIR(a.st_mode):
            result['errors'].append('unsupported-type')
        if xattrs(source) != xattrs(destination):
            result['errors'].append('xattrs')
    except OSError as error:
        result['errors'].append(type(error).__name__)
    return result


def nodes(source, destination, relative):
    yield source, destination, relative
    if source.is_symlink() or not source.is_dir(): return
    if destination.is_symlink() or not destination.is_dir(): return
    with os.scandir(source) as left, os.scandir(destination) as right:
        source_names = {entry.name for entry in left}
        target_names = {entry.name for entry in right}
    for name in sorted(target_names - source_names):
        yield None, None, relative / name
    for name in sorted(source_names):
        yield from nodes(source / name, destination / name, relative / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('parts', nargs='+')
    args = parser.parse_args()
    if not 1 <= args.workers <= 4 or any(Path(p).name != p or p in ('.', '..') for p in args.parts):
        parser.error('bounded workers and direct child parts required')
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'startedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'source': str(args.source), 'destination': str(args.destination),
              'workers': args.workers, 'parts': args.parts, 'nodes': 0,
              'bytesCompared': 0, 'mismatches': 0, 'terminal': False,
              'method': 'direct full byte comparison; lstat/link/xattr metadata; extras/missing'}
    def save():
        tmp = args.output / 'comparison.tmp'
        tmp.write_text(json.dumps(report, indent=2) + '\n')
        tmp.replace(args.output / 'comparison.json')
    save()
    with (args.output / 'mismatches.jsonl').open('w') as errors, ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending = deque()
        source_links, target_links = {}, {}
        def consume():
            r = pending.popleft().result(); report['nodes'] += 1
            # Preserve aliases within the copied trees, independent of links
            # outside this transfer's source/destination scope.
            if 'hardlink' in r:
                source_key, target_key = r.pop('hardlink')
                if source_links.setdefault(source_key, target_key) != target_key or target_links.setdefault(target_key, source_key) != source_key:
                    r['errors'].append('hardlink-relationship')
            report['bytesCompared'] += r['bytes']
            if r['errors']:
                report['mismatches'] += 1; errors.write(json.dumps(r) + '\n'); errors.flush()
            if report['nodes'] % 25000 == 0: save()
        for part in args.parts:
            for a, b, relative in nodes(args.source / part, args.destination / part, Path(part)):
                if a is None:
                    pending.append(pool.submit(lambda p: {'path': str(p), 'bytes': 0, 'errors': ['extra']}, relative))
                else:
                    pending.append(pool.submit(compare_node, a, b, relative))
                if len(pending) >= 32: consume()
        while pending: consume()
    report.update(terminal=True, passed=report['mismatches'] == 0,
                  finishedAt=datetime.datetime.now(datetime.timezone.utc).isoformat())
    save()
    print(json.dumps(report))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
