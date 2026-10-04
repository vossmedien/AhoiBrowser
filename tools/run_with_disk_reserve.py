#!/usr/bin/env python3
"""Run an owned command with a disk reserve and mounted-work-root checks."""
import argparse
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys


def check_work_root(work_root):
    root = pathlib.Path(work_root)
    if not root.is_absolute():
        raise ValueError("work root must be absolute")
    volume = pathlib.Path("/Volumes/Daten")
    if root == volume or volume in root.parents:
        # Existing Inhouse helper owns the approved APFS/UUID/mount check.
        subprocess.run([str(pathlib.Path.home() / ".local/bin/inhouse-external-root")],
                       check=True, stdout=subprocess.DEVNULL)
        if root.resolve() != root:
            raise ValueError("external work root is redirected")
        parent = root
        while not parent.exists():
            parent = parent.parent
        if parent.stat().st_dev != volume.stat().st_dev:
            raise ValueError("external work root is on another filesystem")


def stop_owned_group(child):
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        child.wait(timeout=20)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.wait()


def run(command, work_root, reserve, interval=5):
    check_work_root(work_root)
    if shutil.disk_usage(work_root).free < reserve:
        print("disk safety reserve unavailable; command not started", file=sys.stderr)
        return 2
    child = None
    old_handlers = {}

    def interrupted(signum, _frame):
        raise InterruptedError(signum)

    try:
        for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
            old_handlers[sig] = signal.signal(sig, interrupted)
        child = subprocess.Popen(command, start_new_session=True)
        while True:
            try:
                return child.wait(timeout=interval)
            except subprocess.TimeoutExpired:
                check_work_root(work_root)
                if shutil.disk_usage(work_root).free < reserve:
                    print("disk reserve reached; stopping owned command group", file=sys.stderr)
                    stop_owned_group(child)
                    return 2
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"work-root/reserve check failed: {error}", file=sys.stderr)
        return 2
    finally:
        # Do not let a second signal interrupt bounded owned-child cleanup.
        for sig in old_handlers:
            signal.signal(sig, signal.SIG_IGN)
        if child is not None:
            stop_owned_group(child)
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-root", type=pathlib.Path, required=True)
    parser.add_argument("--policy", type=pathlib.Path)
    parser.add_argument("--check-work-root", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.check_work_root:
        try:
            check_work_root(args.work_root)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            parser.error(str(error))
        return 0
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.policy is None:
        parser.error("policy and command required")
    reserve = json.loads(args.policy.read_text())["diskSpace"]["safetyReserveBytes"]
    if not isinstance(reserve, int) or isinstance(reserve, bool) or reserve <= 0:
        parser.error("positive integer disk safety reserve required")
    return run(command, args.work_root, reserve)


if __name__ == "__main__":
    sys.exit(main())
