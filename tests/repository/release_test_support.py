"""Shared deterministic helpers for release-pipeline repository tests."""

import pathlib
import subprocess

from release import assets, common, crypto


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_bytes(common.canonical_json(value))


def create_release_assets_fixture(root: pathlib.Path, paths: dict) -> None:
    symbol = root / "symbols/AhoiBrowser.app.dSYM/Contents/Resources/DWARF/AhoiBrowser"
    symbol.parent.mkdir(parents=True)
    symbol.write_bytes(b"fixture symbols")
    assets.create_release_assets(
        symbols_root=root / "symbols",
        package_receipt_path=paths["package.json"],
        materials_receipt_path=paths["materials.json"],
        symbol_archive_output=root / "AhoiBrowser-symbols.zip",
        checksums_output=root / "SHA256SUMS",
        receipt_output=paths["assets.json"],
    )


class TemporaryEd25519Key:
    def __init__(self, root: pathlib.Path):
        self.private = root / "test-only-private.pem"
        self.public = root / "test-only-public.pem"
        subprocess.run(
            ["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(self.private)],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(self.private),
                "-pubout",
                "-out",
                str(self.public),
            ],
            check=True,
            capture_output=True,
        )
        self.key_id = crypto.public_key_id(self.public)
