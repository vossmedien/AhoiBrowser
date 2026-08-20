import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PROFILE_PATHS = (
    ROOT / "config/build/upstream-release.gn",
    ROOT / "config/build/ahoi-dev.gn",
    ROOT / "config/build/ahoi-release.gn",
)


class MacOSBuildContractTests(unittest.TestCase):
    def test_launch_requirement_is_separate_from_chromium_compile_target(self):
        for profile_path in PROFILE_PATHS:
            with self.subTest(profile=profile_path.name):
                profile = profile_path.read_text(encoding="utf-8")
                self.assertIn('mac_sdk_min = "26.5"', profile)
                self.assertIn('mac_deployment_target = "13.0"', profile)
                self.assertIn('mac_min_system_version = "26.0"', profile)
                self.assertNotIn('mac_deployment_target = "26.0"', profile)

    def test_building_documentation_explains_the_separation(self):
        building = (ROOT / "docs/BUILDING.md").read_text(encoding="utf-8")
        self.assertIn('mac_deployment_target = "13.0"', building)
        self.assertIn('mac_min_system_version = "26.0"', building)
        self.assertIn("LSMinimumSystemVersion", building)


if __name__ == "__main__":
    unittest.main()
