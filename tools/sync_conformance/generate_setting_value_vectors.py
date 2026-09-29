#!/usr/bin/env python3
"""Generate fixtures/sync-conformance/setting_values_v3.json.

PermittedSetting records merge `value_json` as opaque RFC JSON; the value rules
of the browser-setting catalogue are enforced by each platform's adapter:
C++ `ValidateBrowserSettingValue` (browser_setting_catalog.cc) and Swift
`CompanionBrowserSettingCatalog.validatesValue`. These hand-derived boundary
cases let both runners prove the same accept/reject decision per value.
`catalogueIds` is checked against both source files by the repository test.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "fixtures/sync-conformance/setting_values_v3.json"

SEARCH = "ahoi.browser.default_search_engine"
BOOLEAN_IDS = [
    "ahoi.appearance.glass_enabled", "ahoi.appearance.sidebar_page_tint_enabled",
    "ahoi.navigation.floating_auto_hide_enabled",
    "ahoi.navigation.floating_reveal_notch_enabled",
    "browser.show_home_button", "browser.show_forward_button",
    "browser.pin_split_tab_button", "browser.split_view_drag_and_drop_enabled",
    "download.prompt_for_download", "plugins.always_open_pdf_externally",
    "browser.enable_spellchecking", "translate.enabled",
    "settings.a11y.read_anything.links_enabled",
    "settings.a11y.read_anything.images_enabled",
    "enable_do_not_track", "search.suggest_enabled",
]
DELAY = "ahoi.navigation.floating_auto_hide_delay_ms"
CHARSET = "intl.charset_default"
FONT = "settings.a11y.read_anything.font_scale"
COLOR = "settings.a11y.read_anything.color_info"
SPACING = ["settings.a11y.read_anything.line_spacing",
           "settings.a11y.read_anything.letter_spacing"]
PREDICTION = "net.network_prediction_options"
CATALOGUE = sorted([SEARCH, *BOOLEAN_IDS, DELAY, CHARSET, FONT, COLOR, *SPACING, PREDICTION])

# (setting id, value_json, valid, reason)
RULES = [
    (SEARCH, '"duckDuckGo"', True, "allowlisted engine"),
    (SEARCH, '"google"', True, "allowlisted engine"),
    (SEARCH, '"bing"', True, "allowlisted engine"),
    (SEARCH, '"DuckDuckGo"', False, "engine names are case-sensitive"),
    (SEARCH, '"yahoo"', False, "not allowlisted"),
    (SEARCH, "1", False, "engine must be a string"),
    (DELAY, "100", True, "lower bound inclusive"),
    (DELAY, "10000", True, "upper bound inclusive"),
    (DELAY, "99", False, "below lower bound"),
    (DELAY, "10001", False, "above upper bound"),
    (DELAY, "1500.0", True, "integral double is an integer"),
    (DELAY, "1e3", True, "exponent form of an integer"),
    (DELAY, "150.5", False, "fraction for an integer setting"),
    (DELAY, "true", False, "boolean is not a number"),
    (DELAY, '"500"', False, "string is not a number"),
    (CHARSET, '"UTF-8"', True, "canonical charset"),
    (CHARSET, '"windows-1252"', True, "canonical charset"),
    (CHARSET, '"utf-8"', False, "aliases are not admitted"),
    (CHARSET, '"latin1"', False, "aliases are not admitted"),
    (FONT, "0.5", True, "lower bound inclusive"),
    (FONT, "4.5", True, "upper bound inclusive"),
    (FONT, "2", True, "integer inside the double range"),
    (FONT, "0.49", False, "below lower bound"),
    (FONT, "4.51", False, "above upper bound"),
    (COLOR, "0", True, "exposed color"),
    (COLOR, "5", True, "exposed color"),
    (COLOR, "6", False, "deprecated low-contrast color"),
    (COLOR, "7", True, "exposed color"),
    (COLOR, "8", True, "exposed color"),
    (COLOR, "9", False, "unknown color"),
    (PREDICTION, "0", True, "prediction option"),
    (PREDICTION, "1", True, "registered default"),
    (PREDICTION, "1.0", True, "integral double"),
    (PREDICTION, "3", True, "prediction option"),
    (PREDICTION, "4", False, "unknown option"),
    (PREDICTION, "-1", False, "unknown option"),
    ("ahoi.unknown.setting", "true", False, "unknown id"),
    ("ahoi.unknown.setting", "null", False, "reset only for a known id"),
    (BOOLEAN_IDS[0], "tru", False, "not JSON"),
]
for spacing in SPACING:
    RULES += [(spacing, "1", True, "exposed spacing"), (spacing, "3", True, "exposed spacing"),
              (spacing, "0", False, "deprecated tight spacing"), (spacing, "4", False, "unknown")]
for setting in BOOLEAN_IDS:
    RULES += [(setting, "true", True, "boolean"), (setting, "false", True, "boolean"),
              (setting, "1", False, "number is not a boolean"),
              (setting, '"true"', False, "string is not a boolean")]
for setting in CATALOGUE:
    RULES.append((setting, "null", True, "explicit reset of a known id"))


def generate() -> dict:
    cases = []
    for index, (setting, value, valid, reason) in enumerate(RULES):
        cases.append({"name": f"{index:03d}.{setting}", "settingId": setting,
                      "valueJson": value, "valid": valid, "reason": reason})
    return {"schemaVersion": 1,
            "contract": "Ahoi browser-setting catalogue value conformance",
            "generatedBy": "tools/sync_conformance/generate_setting_value_vectors.py",
            "catalogueIds": CATALOGUE, "cases": cases}


def render(data: dict) -> str:
    return json.dumps(data, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    text = render(generate())
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            print(f"{OUTPUT.relative_to(ROOT)} is stale", file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(text)
    print(f"wrote {len(generate()['cases'])} cases to {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
