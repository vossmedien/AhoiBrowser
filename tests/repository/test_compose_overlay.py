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
        (checkout / "base.txt").write_text("base\n", encoding="utf-8")
        run("git", "add", "base.txt", cwd=checkout)
        run("git", "commit", "-q", "-m", "base", cwd=checkout)
        return checkout

    def invoke(
        self,
        checkout: pathlib.Path,
        overlay: pathlib.Path,
        series: pathlib.Path,
        patch_root: pathlib.Path,
        output: pathlib.Path,
        check: bool = True,
    ):
        return run(
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
            cwd=ROOT,
            check=check,
        )

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

            self.invoke(checkout, overlay, series, patch_root, combined)

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

            result = self.invoke(
                checkout,
                overlay,
                series,
                patch_root,
                root / "combined.patch",
                check=False,
            )

            self.assertNotEqual(0, result.returncode)
            self.assertEqual("base\n", (checkout / "base.txt").read_text())
            self.assertFalse((checkout / "new.txt").exists())
            self.assertEqual("", run("git", "status", "--porcelain", cwd=checkout).stdout)


if __name__ == "__main__":
    unittest.main()
