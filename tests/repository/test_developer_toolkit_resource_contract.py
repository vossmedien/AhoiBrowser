import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches/chromium/0001-ahoi-m152-integration-seams.patch"

GENERATED_RESOURCES_PATH = "chrome/app/generated_resources.grd"
GERMAN_TRANSLATIONS_PATH = (
    "chrome/app/resources/generated_resources_de.xtb"
)
BRITISH_ENGLISH_TRANSLATIONS_PATH = (
    "chrome/app/resources/generated_resources_en-GB.xtb"
)


def text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def patch_section(payload: str, path: str) -> str:
    match = re.search(
        rf"^diff --git a/{re.escape(path)} b/{re.escape(path)}\n"
        r".*?(?=^diff --git |\Z)",
        payload,
        re.MULTILINE | re.DOTALL,
    )
    return "" if match is None else match.group(0)


def added_payload(section: str) -> str:
    return "\n".join(
        line[1:]
        for line in section.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    )


class DeveloperToolkitResourceContractTest(unittest.TestCase):
    def setUp(self) -> None:
        patch = text(PATCH)
        self.generated_resources = added_payload(
            patch_section(patch, GENERATED_RESOURCES_PATH)
        )
        self.german_translations = added_payload(
            patch_section(patch, GERMAN_TRANSLATIONS_PATH)
        )
        self.british_english_translations = added_payload(
            patch_section(patch, BRITISH_ENGLISH_TRANSLATIONS_PATH)
        )

    def test_grd_binds_developer_cookie_and_password_resource_ids(self):
        bindings = {
            "IDS_AHOI_DEVELOPER_SAVED_PASSWORDS": "Saved passwords",
            "IDS_AHOI_DEVELOPER_COOKIE_PARTITIONED": "Partitioned (CHIPS)",
            "IDS_AHOI_DEVELOPER_COOKIE_ERROR_PARTITIONED": (
                "Partitioned cookies require Secure on an HTTPS site."
            ),
        }

        for resource_id, expected_text in bindings.items():
            with self.subTest(resource_id=resource_id):
                self.assertRegex(
                    self.generated_resources,
                    rf'<message name="{resource_id}"[^>]*>\s*'
                    rf"{re.escape(expected_text)}\s*</message>",
                )

    def test_xtb_translations_cover_the_bound_resource_ids(self):
        german_lines = {
            "8011502031648601514": "Partitioniert (CHIPS)",
            "8375942819465594172": (
                "Partitionierte Cookies erfordern „Sicher“ auf einer "
                "HTTPS-Website."
            ),
        }
        british_english_lines = {
            "1562210081663744222": "Saved passwords",
            "8011502031648601514": "Partitioned (CHIPS)",
            "8375942819465594172": (
                "Partitioned cookies require Secure on an HTTPS site."
            ),
        }

        for translation_id, expected_text in german_lines.items():
            with self.subTest(language="de", translation_id=translation_id):
                self.assertIn(
                    f'<translation id="{translation_id}">'
                    f"{expected_text}</translation>",
                    self.german_translations,
                )

        for translation_id, expected_text in british_english_lines.items():
            with self.subTest(
                language="en-GB", translation_id=translation_id
            ):
                self.assertIn(
                    f'<translation id="{translation_id}">'
                    f"{expected_text}</translation>",
                    self.british_english_translations,
                )


if __name__ == "__main__":
    unittest.main()
