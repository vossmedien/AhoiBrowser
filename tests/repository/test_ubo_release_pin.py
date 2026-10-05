import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_json(relative_path: str):
    return json.loads((ROOT / relative_path).read_text(encoding="utf-8"))


class UboReleasePinTests(unittest.TestCase):
    def test_ubo_official_github_release_pin_is_exact(self):
        pin = load_json("config/third-party-pins.json")["dependencies"][
            "uBlockOriginClassic"
        ]
        self.assertTrue(pin["enabled"])
        self.assertEqual("1.74.0", pin["version"])
        self.assertEqual(
            "6dd2d95e50d134a477a4e183343c0b26e9147123", pin["commit"]
        )
        self.assertEqual("Official GitHub release", pin["distribution"])
        self.assertEqual("selectiveUboClassicMv2", pin["gate"])
        self.assertEqual(
            "https://github.com/gorhill/uBlock/releases/tag/1.74.0",
            pin["source"],
        )
        archive = pin["archive"]
        self.assertEqual(
            "https://github.com/gorhill/uBlock/releases/download/1.74.0/"
            "uBlock0_1.74.0.chromium.crx",
            archive["url"],
        )
        self.assertEqual(4535482, archive["size"])
        self.assertEqual(
            "b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e",
            archive["sha256"],
        )
        self.assertEqual(
            "5a6a81097514fb940453d5d46329eca78100e3cc0c5fca508e1a413f77f567bf",
            archive["crxPublicKeySha256"],
        )
        self.assertEqual(
            "fkgkibajhfbepljeaefdnfnegdcjomkh", archive["extensionId"]
        )
        self.assertEqual(
            "/github-production-release-asset/33263118/"
            "ade4daf2-50e8-4953-8821-5c2d43f07a65",
            archive["releaseAssetPath"],
        )

        product_config = (
            ROOT
            / "overlay/chromium/src/ahoi/browser/extensions/ubo_product_config.h"
        ).read_text(encoding="utf-8")
        product_config = re.sub(r'"\s*"', "", product_config)
        for value in (
            pin["version"],
            pin["commit"],
            pin["distribution"],
            archive["url"],
            archive["sha256"],
            archive["crxPublicKeySha256"],
            archive["extensionId"],
            archive["releaseAssetPath"],
        ):
            with self.subTest(browser_pin=value):
                self.assertIn(value, product_config)


if __name__ == "__main__":
    unittest.main()
