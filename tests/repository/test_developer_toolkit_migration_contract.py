import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
OVERLAY = ROOT / "overlay/chromium/src/ahoi/browser/developer_toolkit"
PATCH = ROOT / "patches/chromium/0001-ahoi-m152-integration-seams.patch"
BROWSER_PREFS_PATH = "chrome/browser/prefs/browser_prefs.cc"


def text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def braced_block(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        return ""
    opening = source.find("{", start + len(signature))
    if opening < 0:
        return ""
    depth = 0
    for offset, character in enumerate(source[opening:], start=opening):
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    return ""


def patch_section(payload: str, path: str) -> str:
    match = re.search(
        rf"^diff --git a/{re.escape(path)} b/{re.escape(path)}\n"
        r".*?(?=^diff --git |\Z)",
        payload,
        re.MULTILINE | re.DOTALL,
    )
    return "" if match is None else match.group(0)


def hunk_containing(section: str, marker: str) -> str:
    for hunk in re.split(r"(?=^@@ )", section, flags=re.MULTILINE):
        if hunk.startswith("@@ ") and marker in hunk:
            return hunk
    return ""


def postimage(hunk: str) -> str:
    return "\n".join(
        line[1:] if line.startswith(("+", " ")) else line
        for line in hunk.splitlines()[1:]
        if not line.startswith("-")
    )


class DeveloperToolkitMigrationContractTest(unittest.TestCase):
    def test_overlay_materializes_legacy_activation_and_covers_boundaries(self):
        header = text(OVERLAY / "developer_toolkit_prefs.h")
        implementation = text(OVERLAY / "developer_toolkit_prefs.cc")
        unit_tests = text(OVERLAY / "developer_toolkit_prefs_unittest.cc")

        self.assertIn("void MigrateLegacyActivation(PrefService* prefs);", header)
        migration = braced_block(
            implementation,
            "void MigrateLegacyActivation(PrefService* prefs)",
        )
        self.assertTrue(migration)
        self.assertIn("FindPreference(kToolkitEnabled)", migration)
        self.assertIn("!enabled_pref->IsDefaultValue()", migration)
        for legacy_pref in (
            "kShowCookieButton",
            "kShowCacheButton",
            "kShowToolkitButton",
        ):
            self.assertIn(legacy_pref, migration)
        self.assertIn("!pref->IsDefaultValue()", migration)
        self.assertIn("prefs->GetBoolean(pref_name)", migration)
        self.assertEqual(
            1, migration.count("prefs->SetBoolean(kToolkitEnabled, true)")
        )

        self.assertIn("MigratesLegacyVisibleToolbarWithoutMasterPref", unit_tests)
        self.assertIn(
            "LegacyMigrationPreservesExplicitMasterAndFreshDefaults", unit_tests
        )
        self.assertGreaterEqual(
            unit_tests.count("MigrateLegacyActivation(&prefs)"), 3
        )
        self.assertIn(
            "EXPECT_TRUE(prefs.FindPreference(kToolkitEnabled)->IsDefaultValue())",
            unit_tests,
        )
        self.assertIn("EXPECT_FALSE(prefs.GetBoolean(kToolkitEnabled))", unit_tests)

    def test_integration_patch_invokes_migration_in_desktop_profile_migrations(self):
        browser_prefs = patch_section(text(PATCH), BROWSER_PREFS_PATH)
        self.assertTrue(browser_prefs)
        added_source = "\n".join(
            line[1:]
            for line in browser_prefs.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )
        self.assertEqual(
            1,
            len(
                re.findall(
                    r"ahoi::developer_toolkit_prefs::MigrateLegacyActivation\(\s*"
                    r"profile_prefs\s*\);",
                    added_source,
                )
            ),
        )

        migration_hunk = hunk_containing(browser_prefs, "MigrateLegacyActivation")
        self.assertTrue(migration_hunk)
        self.assertIn("MigrateObsoleteProfilePrefs", migration_hunk.splitlines()[0])
        migrated_source = postimage(migration_hunk)
        invocation = migrated_source.index("MigrateLegacyActivation(profile_prefs)")
        source_before_invocation = migrated_source[:invocation]
        guard_start = source_before_invocation.rfind(
            "#if !BUILDFLAG(IS_ANDROID)"
        )
        guard_end = source_before_invocation.rfind("#endif")
        self.assertGreater(guard_start, guard_end)


if __name__ == "__main__":
    unittest.main()
