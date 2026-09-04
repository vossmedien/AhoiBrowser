import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPOSER = ROOT / "tools/compose_overlay.py"


def run(*args: str, cwd: pathlib.Path, check: bool = True):
    return subprocess.run(
        args,
        cwd=cwd,
        check=check,
        capture_output=True,
        text=True,
    )


class ComposeOverlayTests(unittest.TestCase):
    def create_checkout(self, root: pathlib.Path) -> pathlib.Path:
        checkout = root / "checkout"
        checkout.mkdir()
        run("git", "init", "-q", cwd=checkout)
        run("git", "config", "user.name", "Ahoi Test", cwd=checkout)
        run("git", "config", "user.email", "test@example.invalid", cwd=checkout)
        # Snapshot tests must not race a user's globally configured background
        # maintenance task started by the fixture's initial commit.
        run("git", "config", "maintenance.auto", "false", cwd=checkout)
        run("git", "config", "gc.auto", "0", cwd=checkout)
        (checkout / "base.txt").write_text("base\n", encoding="utf-8")
        run("git", "add", "base.txt", cwd=checkout)
        run("git", "commit", "-q", "-m", "base", cwd=checkout)
        return checkout

    def object_inventory(
        self, checkout: pathlib.Path
    ) -> dict[str, tuple[str, bytes]]:
        objects = checkout / ".git/objects"
        inventory: dict[str, tuple[str, bytes]] = {}
        for path in sorted(objects.rglob("*")):
            relative = path.relative_to(objects).as_posix()
            if path.is_symlink():
                inventory[relative] = ("symlink", os.fsencode(os.readlink(path)))
            elif path.is_file():
                inventory[relative] = ("file", path.read_bytes())
            else:
                inventory[relative] = ("directory", b"")
        return inventory

    def invoke(
        self,
        checkout: pathlib.Path,
        overlay: pathlib.Path,
        series: pathlib.Path,
        patch_root: pathlib.Path,
        output: pathlib.Path,
        base_revision: str | None = None,
        check: bool = True,
    ):
        command = [
            "python3",
            str(COMPOSER),
            "--checkout",
            str(checkout),
            "--overlay",
            str(overlay),
            "--series",
            str(series),
            "--patch-root",
            str(patch_root),
            "--output",
            str(output),
        ]
        if base_revision is not None:
            command.append(f"--base-revision={base_revision}")
        return run(*command, cwd=ROOT, check=check)

    def test_dependent_series_and_overlay_are_composed_without_mutation(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            checkout = self.create_checkout(root)
            overlay = root / "overlay"
            overlay.mkdir()
            (overlay / "new.txt").write_text("overlay\n", encoding="utf-8")
            patch_root = root / "patches"
            patch_root.mkdir()
            (patch_root / "first.patch").write_text(
                """diff --git a/base.txt b/base.txt
--- a/base.txt
+++ b/base.txt
@@ -1 +1 @@
-base
+first
""",
                encoding="utf-8",
            )
            (patch_root / "second.patch").write_text(
                """diff --git a/base.txt b/base.txt
--- a/base.txt
+++ b/base.txt
@@ -1 +1 @@
-first
+second
""",
                encoding="utf-8",
            )
            series = patch_root / "series"
            series.write_text("first.patch\nsecond.patch\n", encoding="utf-8")
            combined = root / "combined.patch"
            objects_before = self.object_inventory(checkout)

            self.invoke(checkout, overlay, series, patch_root, combined)

            self.assertEqual(objects_before, self.object_inventory(checkout))
            self.assertEqual("base\n", (checkout / "base.txt").read_text())
            self.assertFalse((checkout / "new.txt").exists())
            self.assertEqual("", run("git", "status", "--porcelain", cwd=checkout).stdout)

            run("git", "apply", "--whitespace=error-all", str(combined), cwd=checkout)
            self.assertEqual("second\n", (checkout / "base.txt").read_text())
            self.assertEqual("overlay\n", (checkout / "new.txt").read_text())

    def test_failed_series_leaves_checkout_pristine(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            checkout = self.create_checkout(root)
            overlay = root / "overlay"
            overlay.mkdir()
            (overlay / "new.txt").write_text("overlay\n", encoding="utf-8")
            patch_root = root / "patches"
            patch_root.mkdir()
            (patch_root / "bad.patch").write_text(
                """diff --git a/missing.txt b/missing.txt
--- a/missing.txt
+++ b/missing.txt
@@ -1 +1 @@
-missing
+changed
""",
                encoding="utf-8",
            )
            series = patch_root / "series"
            series.write_text("bad.patch\n", encoding="utf-8")
            objects_before = self.object_inventory(checkout)

            result = self.invoke(
                checkout,
                overlay,
                series,
                patch_root,
                root / "combined.patch",
                check=False,
            )

            self.assertNotEqual(0, result.returncode)
            self.assertEqual(objects_before, self.object_inventory(checkout))
            self.assertEqual("base\n", (checkout / "base.txt").read_text())
            self.assertFalse((checkout / "new.txt").exists())
            self.assertEqual("", run("git", "status", "--porcelain", cwd=checkout).stdout)

    def test_non_head_base_is_composed_without_checkout_mutation(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            checkout = self.create_checkout(root)
            base_revision = run("git", "rev-parse", "HEAD", cwd=checkout).stdout.strip()
            (checkout / "base.txt").write_text("head\n", encoding="utf-8")
            run("git", "add", "base.txt", cwd=checkout)
            run("git", "commit", "-q", "-m", "new head", cwd=checkout)

            overlay = root / "overlay"
            overlay.mkdir()
            (overlay / "new.txt").write_text("overlay\n", encoding="utf-8")
            patch_root = root / "patches"
            patch_root.mkdir()
            (patch_root / "change.patch").write_text(
                """diff --git a/base.txt b/base.txt
--- a/base.txt
+++ b/base.txt
@@ -1 +1 @@
-base
+historical
""",
                encoding="utf-8",
            )
            series = patch_root / "series"
            series.write_text("change.patch\n", encoding="utf-8")
            combined = root / "combined.patch"
            head_before = run("git", "rev-parse", "HEAD", cwd=checkout).stdout
            index_before = run("git", "write-tree", cwd=checkout).stdout
            status_before = run("git", "status", "--porcelain", cwd=checkout).stdout

            self.invoke(
                checkout,
                overlay,
                series,
                patch_root,
                combined,
                base_revision=base_revision,
            )

            self.assertEqual(head_before, run("git", "rev-parse", "HEAD", cwd=checkout).stdout)
            self.assertEqual(index_before, run("git", "write-tree", cwd=checkout).stdout)
            self.assertEqual(
                status_before,
                run("git", "status", "--porcelain", cwd=checkout).stdout,
            )
            self.assertEqual("head\n", (checkout / "base.txt").read_text())
            self.assertFalse((checkout / "new.txt").exists())

            verification = root / "verification"
            run("git", "clone", "-q", str(checkout), str(verification), cwd=root)
            run("git", "checkout", "-q", base_revision, cwd=verification)
            run("git", "apply", "--whitespace=error-all", str(combined), cwd=verification)
            self.assertEqual("historical\n", (verification / "base.txt").read_text())
            self.assertEqual("overlay\n", (verification / "new.txt").read_text())

    def test_invalid_base_revision_fails_cleanly_without_checkout_mutation(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            checkout = self.create_checkout(root)
            overlay = root / "overlay"
            overlay.mkdir()
            (overlay / "new.txt").write_text("overlay\n", encoding="utf-8")
            patch_root = root / "patches"
            patch_root.mkdir()
            series = patch_root / "series"
            series.write_text("# no patches\n", encoding="utf-8")
            output = root / "combined.patch"
            head_before = run("git", "rev-parse", "HEAD", cwd=checkout).stdout
            index_before = run("git", "write-tree", cwd=checkout).stdout

            result = self.invoke(
                checkout,
                overlay,
                series,
                patch_root,
                output,
                base_revision="--definitely-not-a-revision",
                check=False,
            )

            self.assertNotEqual(0, result.returncode)
            self.assertNotIn("Traceback", result.stderr)
            self.assertIn("invalid base revision", result.stderr)
            self.assertFalse(output.exists())
            self.assertEqual(head_before, run("git", "rev-parse", "HEAD", cwd=checkout).stdout)
            self.assertEqual(index_before, run("git", "write-tree", cwd=checkout).stdout)
            self.assertEqual("", run("git", "status", "--porcelain", cwd=checkout).stdout)

    def test_vendor_whitespace_can_be_preserved_as_an_exact_binary_delta(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            checkout = self.create_checkout(root)
            overlay = root / "overlay"
            overlay.mkdir()
            vendor = overlay / "vendor.txt"
            vendor.write_bytes(b"licensed vendor line with trailing space \n")
            patch_root = root / "patches"
            patch_root.mkdir()
            (patch_root / "series").write_text("# no patches\n", encoding="utf-8")
            (patch_root / "diff.gitattributes").write_text(
                "vendor.txt diff=ahoi-binary\n", encoding="utf-8"
            )
            combined = root / "combined.patch"

            self.invoke(
                checkout,
                overlay,
                patch_root / "series",
                patch_root,
                combined,
            )

            self.assertIn(b"GIT binary patch", combined.read_bytes())
            run(
                "git",
                "apply",
                "--whitespace=error-all",
                str(combined),
                cwd=checkout,
            )
            self.assertEqual(
                b"licensed vendor line with trailing space \n",
                (checkout / "vendor.txt").read_bytes(),
            )

    def test_batch_preserves_names_bytes_modes_and_links_without_mutation(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            checkout = self.create_checkout(root)
            overlay = root / "overlay"
            overlay.mkdir()
            payloads = {
                "base.txt": b"replacement\n",
                "-literal, name\twith\nnewline.txt": b"exact\x00binary\xff",
                "ausführbar.sh": b"#!/bin/sh\nexit 0\n",
            }
            for name, payload in payloads.items():
                (overlay / name).write_bytes(payload)
            (overlay / "ausführbar.sh").chmod(0o755)
            (overlay / "symbolic link").symlink_to("base.txt")
            patch_root = root / "patches"
            patch_root.mkdir()
            series = patch_root / "series"
            series.write_text("# no patches\n", encoding="utf-8")
            objects_before = self.object_inventory(checkout)
            index_before = (checkout / ".git/index").read_bytes()
            combined = root / "combined.patch"

            self.invoke(checkout, overlay, series, patch_root, combined)

            self.assertEqual(objects_before, self.object_inventory(checkout))
            self.assertEqual(index_before, (checkout / ".git/index").read_bytes())
            self.assertEqual(b"base\n", (checkout / "base.txt").read_bytes())
            run("git", "apply", str(combined), cwd=checkout)
            for name, payload in payloads.items():
                self.assertEqual(payload, (checkout / name).read_bytes())
            self.assertTrue((checkout / "ausführbar.sh").stat().st_mode & 0o111)
            self.assertFalse((checkout / "base.txt").stat().st_mode & 0o111)
            self.assertEqual("base.txt", os.readlink(checkout / "symbolic link"))

    def test_empty_overlay_is_rejected_and_preserves_real_index(self):
        with tempfile.TemporaryDirectory() as raw_root:
            root = pathlib.Path(raw_root)
            checkout = self.create_checkout(root)
            overlay = root / "overlay"
            overlay.mkdir()
            patch_root = root / "patches"
            patch_root.mkdir()
            series = patch_root / "series"
            series.write_text("", encoding="utf-8")
            index_before = (checkout / ".git/index").read_bytes()
            combined = root / "combined.patch"

            result = self.invoke(
                checkout, overlay, series, patch_root, combined, check=False
            )

            self.assertNotEqual(0, result.returncode)
            self.assertIn("produced no checkout delta", result.stderr)
            self.assertFalse(combined.exists())
            self.assertEqual(index_before, (checkout / ".git/index").read_bytes())


if __name__ == "__main__":
    unittest.main()
