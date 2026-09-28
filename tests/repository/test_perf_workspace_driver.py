import os
import pathlib
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
DRIVERS = ROOT / "tools/perf/drivers"
# `key` posts to the process (CGEventPostToPid), not the HID tap; the others
# are HID-system input and would cancel a guarded run.
HID_MODES = {"hidkey", "type", "click", "rightclick", "hidrightclick"}

# Simulated axtool: keeps the active Workspace, an open menu and a dialog in a
# state directory and logs every invocation. Only the AX subcommands the
# drivers may use are implemented; anything else fails the run.
FAKE_AXTOOL = textwrap.dedent("""\
    #!/bin/bash
    S=$FAKE_AX_STATE; echo "$*" >> "$S/calls"
    active=$(cat "$S/active"); spaces=$(cat "$S/spaces")
    case "$1" in
      dump)
        echo "AXButton | $active, Workspace wechseln |"
        if [ -f "$S/menu" ]; then
          echo "AXMenuItem | Neuer Workspace… |"
          while read -r w; do echo "AXMenuItem | $w |"; done < "$S/spaces"
        fi
        [ -f "$S/dialog" ] && echo "AXTextField | Workspace-Name |"
        exit 0 ;;
      press)
        case "$3" in
          "$active, Workspace wechseln") [ "${4:-}" = AXShowMenu ] && touch "$S/menu"; exit 0 ;;
          "Neuer Workspace…") rm -f "$S/menu"; touch "$S/dialog"; exit 0 ;;
          "Erstellen") rm -f "$S/dialog"; cat "$S/name" >> "$S/spaces"; cp "$S/name" "$S/active"; exit 0 ;;
          *) if [ -f "$S/menu" ] && grep -qxF "$3" "$S/spaces"; then
               rm -f "$S/menu"; echo "$3" > "$S/active"; exit 0; fi
             echo "NOT FOUND"; exit 1 ;;
        esac ;;
      setvalue) echo "$4" > "$S/name"; exit 0 ;;
      *) echo "forbidden mode $1" >&2; exit 9 ;;
    esac
    """)


class WorkspaceDriverTest(unittest.TestCase):
    def run_drivers(self, switches="4"):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            state = root / "state"
            state.mkdir()
            (state / "active").write_text("Inbox\n")
            (state / "spaces").write_text("Inbox\n")
            axtool = root / "axtool"
            axtool.write_text(FAKE_AXTOOL)
            axtool.chmod(0o755)
            env = {**os.environ, "AHOI_AXTOOL": str(axtool), "AHOI_PERF_PID": "4242",
                   "AHOI_PERF_STATE_DIR": str(root), "FAKE_AX_STATE": str(state),
                   "AHOI_PERF_SWITCHES": switches}
            setup = subprocess.run([str(DRIVERS / "workspace_switch_setup.sh")], env=env,
                                   capture_output=True, text=True, timeout=60)
            driver = subprocess.run([str(DRIVERS / "workspace_switch_driver.sh")], env=env,
                                    capture_output=True, text=True, timeout=120)
            calls = (state / "calls").read_text().splitlines()
            return setup, driver, calls, (state / "active").read_text().strip()

    def test_setup_then_alternating_switches_without_hid(self):
        setup, driver, calls, active = self.run_drivers("3")
        self.assertEqual(setup.returncode, 0, setup.stderr)
        self.assertEqual(driver.returncode, 0, driver.stderr)
        self.assertEqual({call.split()[0] for call in calls} & HID_MODES, set())
        switches = [call for call in calls if call.startswith("press 4242 ")
                    and call.split(" ", 2)[2] in ("Inbox", "Perf B")]
        self.assertEqual([call.split(" ", 2)[2] for call in switches],
                         ["Inbox", "Perf B", "Inbox"])
        self.assertEqual(active, "Inbox")
        self.assertIn("setvalue 4242 Workspace-Name Perf B", calls)

    def test_driver_refuses_without_setup_and_without_prebuilt_axtool(self):
        env = {**os.environ, "AHOI_AXTOOL": "/nonexistent/axtool", "AHOI_PERF_PID": "1"}
        result = subprocess.run([str(DRIVERS / "workspace_switch_driver.sh")], env=env,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 5)
        with tempfile.TemporaryDirectory() as directory:
            axtool = pathlib.Path(directory) / "axtool"
            axtool.write_text("#!/bin/bash\nexit 0\n")
            axtool.chmod(0o755)
            env = {**env, "AHOI_AXTOOL": str(axtool), "AHOI_PERF_STATE_DIR": directory}
            result = subprocess.run([str(DRIVERS / "workspace_switch_driver.sh")], env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 4)

    def test_drivers_never_compile_axtool(self):
        for path in DRIVERS.glob("*.sh"):
            text = path.read_text()
            self.assertNotIn("swiftc", text, path.name)
            self.assertNotIn("xcrun", text, path.name)


FAKE_BAR_AXTOOL = textwrap.dedent("""\
    #!/bin/bash
    S=$FAKE_AX_STATE; echo "$*" >> "$S/calls"
    case "$1" in
      dump) [ -f "$S/open" ] && echo "AXWindow | Suchen oder URL eingeben |"; exit 0 ;;
      key) [ "$3 $4" = "17 cmd" ] && touch "$S/open" && exit 0; exit 1 ;;
      setvalue) exit 0 ;;
      *) echo "forbidden mode $1" >&2; exit 9 ;;
    esac
    """)


class CommandBarDriverTest(unittest.TestCase):
    def test_opens_once_clears_then_inserts_each_query_without_hid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            axtool, insert = root / "axtool", root / "insert"
            axtool.write_text(FAKE_BAR_AXTOOL)
            insert.write_text('#!/bin/bash\necho "insert $*" >> "$FAKE_AX_STATE/calls"\n')
            for tool in (axtool, insert):
                tool.chmod(0o755)
            env = {**os.environ, "AHOI_AXTOOL": str(axtool), "AHOI_AX_INSERT": str(insert),
                   "AHOI_PERF_PID": "4242", "FAKE_AX_STATE": directory,
                   "AHOI_PERF_QUERIES": "alpha beta"}
            result = subprocess.run([str(DRIVERS / "command_bar_driver.sh")], env=env,
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            calls = [c for c in (root / "calls").read_text().splitlines()
                     if not c.startswith("dump")]
            field = "Mit Google suchen oder eine URL eingeben"
            self.assertEqual(calls, [
                "key 4242 17 cmd",
                f"setvalue 4242 {field} ", f"insert 4242 {field} alpha 400",
                f"setvalue 4242 {field} ", f"insert 4242 {field} beta 400"])

    def test_insert_helper_uses_selected_text_not_hid(self):
        source = (DRIVERS / "ax_insert_text.swift").read_text()
        self.assertIn("kAXSelectedTextAttribute", source)
        self.assertNotIn("CGEvent", source)


if __name__ == "__main__":
    unittest.main()
