import json
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from release import assets, cli, common  # noqa: E402


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_bytes(common.canonical_json(value))


def release_fixture(root: pathlib.Path) -> dict[str, pathlib.Path]:
    paths = {
        name: root / name
        for name in (
            "AhoiBrowser.zip",
            "AhoiBrowser.dmg",
            "package.json",
            "materials.json",
            "release-components.json",
            "sbom.json",
            "LICENSES.zip",
            "THIRD-PARTY-NOTICES.txt",
            "SOURCE-OFFER.txt",
        )
    }
    for name in (
        "AhoiBrowser.zip",
        "AhoiBrowser.dmg",
        "release-components.json",
        "sbom.json",
        "LICENSES.zip",
        "THIRD-PARTY-NOTICES.txt",
        "SOURCE-OFFER.txt",
    ):
        paths[name].write_bytes(f"fixture:{name}\n".encode())
    package_artifacts = {}
    for kind in ("zip", "dmg"):
        path = paths[f"AhoiBrowser.{kind}"]
        package_artifacts[kind] = {
            "file": path.name,
            "sha256": common.sha256_file(path),
            "size": path.stat().st_size,
        }
    write_json(
        paths["package.json"],
        {
            "schemaVersion": 1,
            "kind": "package-provenance",
            "artifacts": package_artifacts,
        },
    )
    material_names = {
        "componentInventory": "release-components.json",
        "sbom": "sbom.json",
        "licenseArchive": "LICENSES.zip",
        "thirdPartyNotices": "THIRD-PARTY-NOTICES.txt",
        "correspondingSourceOffer": "SOURCE-OFFER.txt",
    }
    write_json(
        paths["materials.json"],
        {
            "schemaVersion": 1,
            "kind": "release-materials",
            **{
                field: {
                    "file": paths[name].name,
                    "sha256": common.sha256_file(paths[name]),
                }
                for field, name in material_names.items()
            },
        },
    )
    symbols = root / "build-symbols"
    dwarf = symbols / "AhoiBrowser.app.dSYM/Contents/Resources/DWARF/AhoiBrowser"
    dwarf.parent.mkdir(parents=True, exist_ok=True)
    dwarf.write_bytes(b"DWARF fixture\n")
    (symbols / "renderer.sym").write_bytes(b"MODULE mac arm64 fixture renderer\n")
    paths["symbols-root"] = symbols
    return paths


class ReleaseAssetsTests(unittest.TestCase):
    def create(self, root: pathlib.Path) -> tuple[dict, dict[str, pathlib.Path]]:
        paths = release_fixture(root)
        receipt = assets.create_release_assets(
            symbols_root=paths["symbols-root"],
            package_receipt_path=paths["package.json"],
            materials_receipt_path=paths["materials.json"],
            symbol_archive_output=root / "AhoiBrowser-symbols.zip",
            checksums_output=root / "SHA256SUMS",
            receipt_output=root / "release-assets.json",
        )
        return receipt, paths

    def test_symbols_checksums_and_receipt_are_deterministic_and_complete(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-release-assets-") as raw:
            root = pathlib.Path(raw)
            first, paths = self.create(root)
            archive_bytes = (root / "AhoiBrowser-symbols.zip").read_bytes()
            checksum_bytes = (root / "SHA256SUMS").read_bytes()
            receipt_bytes = (root / "release-assets.json").read_bytes()
            second = assets.create_release_assets(
                symbols_root=paths["symbols-root"],
                package_receipt_path=paths["package.json"],
                materials_receipt_path=paths["materials.json"],
                symbol_archive_output=root / "AhoiBrowser-symbols.zip",
                checksums_output=root / "SHA256SUMS",
                receipt_output=root / "release-assets.json",
            )
            self.assertEqual(first, second)
            self.assertEqual(archive_bytes, (root / "AhoiBrowser-symbols.zip").read_bytes())
            self.assertEqual(checksum_bytes, (root / "SHA256SUMS").read_bytes())
            self.assertEqual(receipt_bytes, (root / "release-assets.json").read_bytes())
            self.assertEqual(2, len(first["symbols"]["members"]))
            self.assertEqual(8, len(first["checksums"]["entries"]))
            self.assertEqual(
                sorted(item["file"] for item in first["checksums"]["entries"]),
                [item["file"] for item in first["checksums"]["entries"]],
            )
            assets.validate_release_assets(
                root,
                first,
                package_reference={
                    "file": "package.json",
                    "sha256": common.sha256_file(paths["package.json"]),
                },
                materials_reference={
                    "file": "materials.json",
                    "sha256": common.sha256_file(paths["materials.json"]),
                },
            )

    def test_missing_symbols_and_tampered_archive_fail_closed(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-release-assets-fail-") as raw:
            root = pathlib.Path(raw)
            paths = release_fixture(root)
            empty = root / "empty-symbols"
            empty.mkdir()
            with self.assertRaisesRegex(common.ReleaseError, "no .dSYM"):
                assets.create_release_assets(
                    symbols_root=empty,
                    package_receipt_path=paths["package.json"],
                    materials_receipt_path=paths["materials.json"],
                    symbol_archive_output=root / "AhoiBrowser-symbols.zip",
                    checksums_output=root / "SHA256SUMS",
                    receipt_output=root / "release-assets.json",
                )
            receipt, paths = self.create(root)
            (root / "AhoiBrowser-symbols.zip").write_bytes(b"tampered")
            with self.assertRaisesRegex(common.ReleaseError, "symbols artifact"):
                assets.validate_release_assets(
                    root,
                    receipt,
                    package_reference={
                        "file": "package.json",
                        "sha256": common.sha256_file(paths["package.json"]),
                    },
                    materials_reference={
                        "file": "materials.json",
                        "sha256": common.sha256_file(paths["materials.json"]),
                    },
                )

    def test_checksum_file_tampering_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-release-checksums-") as raw:
            root = pathlib.Path(raw)
            receipt, paths = self.create(root)
            with (root / "SHA256SUMS").open("ab") as handle:
                handle.write(b"0" * 64 + b"  injected\n")
            with self.assertRaisesRegex(common.ReleaseError, "checksum file SHA-256"):
                assets.validate_release_assets(
                    root,
                    receipt,
                    package_reference={
                        "file": "package.json",
                        "sha256": common.sha256_file(paths["package.json"]),
                    },
                    materials_reference={
                        "file": "materials.json",
                        "sha256": common.sha256_file(paths["materials.json"]),
                    },
                )

    def test_review_and_trademark_contract_stays_explicitly_closed(self):
        cli._policy()
        policy = json.loads((ROOT / "config/release-review-policy.json").read_text())
        external = json.loads((ROOT / "config/external-gates.json").read_text())
        gates = {item["id"]: item for item in external["gates"]}
        self.assertFalse(policy["releasePassEnabled"])
        self.assertEqual([], policy["trustedReviewerKeyIds"])
        self.assertEqual(
            {"third-party-license-and-source", "trademark-and-branding"},
            {item["id"] for item in policy["requiredReviews"]},
        )
        gate = gates[policy["externalGate"]]
        self.assertEqual("blocked-legal-review", gate["state"])
        trademarks = (ROOT / "docs/TRADEMARKS.md").read_text()
        review = (ROOT / "docs/THIRD_PARTY_REVIEW.md").read_text()
        self.assertIn("Every public fork or modified binary", trademarks)
        self.assertIn("must rebrand", trademarks)
        self.assertIn("releasePassEnabled=false", review)
        self.assertIn("blocked-legal-review", review)


if __name__ == "__main__":
    unittest.main()
