import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ROLL = ROOT / "tools/chromium_roll.py"
PIN = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))


def run(*args: str, cwd: pathlib.Path = ROOT):
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


class ChromiumRollCandidatePromotionTests(unittest.TestCase):
    def make_fixture(self, root: pathlib.Path):
        repository = root / "repository"
        config = repository / "config"
        config.mkdir(parents=True)
        production_path = config / "chromium.json"
        production_path.write_text(
            json.dumps(PIN, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        candidate = {
            **PIN,
            "milestone": PIN["milestone"] + 1,
            "version": "153.0.8000.1",
            "tag": "refs/tags/153.0.8000.1",
            "commit": "1" * 40,
            "branchHead": 8000,
            "branchHeadPosition": PIN["branchHeadPosition"] + 1,
            "branchPosition": PIN["branchPosition"] + 1,
            "retrievedAt": "2026-08-27T12:00:00Z",
        }
        discovery = root / "reviewed-candidate.json"
        discovery.write_text(
            json.dumps(candidate, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return repository, production_path, discovery, candidate

    def invoke(
        self,
        repository: pathlib.Path,
        discovery: pathlib.Path,
        candidate: dict,
        *extra: str,
    ):
        return run(
            "python3",
            str(ROLL),
            "promote-candidate",
            "--repository",
            str(repository),
            "--candidate",
            str(discovery),
            "--accept-sha256",
            hashlib.sha256(discovery.read_bytes()).hexdigest(),
            "--accept-version",
            candidate["version"],
            "--accept-commit",
            candidate["commit"],
            *extra,
        )

    def test_reviewed_candidate_is_atomically_bound_without_changing_production(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-promotion-") as raw:
            repository, production, discovery, candidate = self.make_fixture(
                pathlib.Path(raw)
            )
            production_before = production.read_bytes()

            result = self.invoke(repository, discovery, candidate)

            binding = repository / "config/upstream-roll-candidate.json"
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(candidate, json.loads(binding.read_text(encoding="utf-8")))
            self.assertEqual(production_before, production.read_bytes())
            receipt = json.loads(result.stdout)
            self.assertFalse(receipt["productionPinChanged"])
            self.assertEqual(
                "config/upstream-roll-candidate.json",
                receipt["candidate"]["binding"],
            )

    def test_confirmation_mismatch_preserves_existing_candidate_binding(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-promotion-") as raw:
            repository, production, discovery, candidate = self.make_fixture(
                pathlib.Path(raw)
            )
            binding = repository / "config/upstream-roll-candidate.json"
            binding.write_text("preserve existing review\n", encoding="utf-8")
            production_before = production.read_bytes()
            result = run(
                "python3",
                str(ROLL),
                "promote-candidate",
                "--repository",
                str(repository),
                "--candidate",
                str(discovery),
                "--accept-sha256",
                hashlib.sha256(discovery.read_bytes()).hexdigest(),
                "--accept-version",
                candidate["version"],
                "--accept-commit",
                "2" * 40,
            )

            self.assertNotEqual(0, result.returncode)
            self.assertIn("accepted commit does not match", result.stderr)
            self.assertEqual("preserve existing review\n", binding.read_text())
            self.assertEqual(production_before, production.read_bytes())

    def test_symlink_or_non_newer_candidate_is_never_promoted(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-promotion-") as raw:
            root = pathlib.Path(raw)
            repository, _, discovery, candidate = self.make_fixture(root)
            symlink = root / "candidate-link.json"
            symlink.symlink_to(discovery)
            linked = self.invoke(repository, symlink, candidate)
            self.assertNotEqual(0, linked.returncode)
            self.assertIn("non-symlink", linked.stderr)

            discovery.write_text(
                json.dumps(PIN, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            same = self.invoke(repository, discovery, PIN)
            self.assertNotEqual(0, same.returncode)
            self.assertIn("must be newer", same.stderr)
            self.assertFalse(
                (repository / "config/upstream-roll-candidate.json").exists()
            )

    def test_full_file_hash_confirmation_prevents_post_review_replacement(self):
        with tempfile.TemporaryDirectory(prefix="ahoi-roll-promotion-") as raw:
            repository, production, discovery, candidate = self.make_fixture(
                pathlib.Path(raw)
            )
            reviewed_hash = hashlib.sha256(discovery.read_bytes()).hexdigest()
            candidate["branchHeadPosition"] += 1
            discovery.write_text(
                json.dumps(candidate, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            result = run(
                "python3",
                str(ROLL),
                "promote-candidate",
                "--repository",
                str(repository),
                "--candidate",
                str(discovery),
                "--accept-sha256",
                reviewed_hash,
                "--accept-version",
                candidate["version"],
                "--accept-commit",
                candidate["commit"],
            )

            self.assertNotEqual(0, result.returncode)
            self.assertIn("SHA-256 does not match", result.stderr)
            self.assertEqual(PIN, json.loads(production.read_text(encoding="utf-8")))
            self.assertFalse(
                (repository / "config/upstream-roll-candidate.json").exists()
            )


if __name__ == "__main__":
    unittest.main()
