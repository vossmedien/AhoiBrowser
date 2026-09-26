#!/usr/bin/env python3
"""Report Ahoi UI strings without a translation in a patched checkout.

Computes the GRIT message ID of every IDS_AHOI_* message in the checkout's
chrome/app/generated_resources.grd (the patch stack must be applied) and
checks it against a translation bundle, German by default. Exit 1 when
translations are missing. Uses the checkout's own GRIT for the fingerprint.

usage: check_ahoi_translations.py [--checkout DIR] [--lang de]
"""
import argparse
import html
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MESSAGE = re.compile(r'<message name="(IDS_AHOI_[A-Z0-9_]+)"([^>]*)>(.*?)</message>', re.S)
PLACEHOLDER = re.compile(r'<ph name="([^"]+)"\s*(?:/>|>.*?</ph>)', re.S)


def presentable(body):
    """The text GRIT fingerprints: placeholders as upper-case names."""
    text = PLACEHOLDER.sub(lambda m: m.group(1).upper(), body).strip()
    if text.startswith("'''"):
        text = text[3:]
    if text.endswith("'''"):
        text = text[:-3]
    return html.unescape(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--checkout", default=str(ROOT / ".work/chromium/src"))
    parser.add_argument("--lang", default="de")
    args = parser.parse_args()
    src = pathlib.Path(args.checkout)
    sys.path.insert(0, str(src / "tools/grit"))
    from grit.extern import tclib  # pylint: disable=import-outside-toplevel

    grd = (src / "chrome/app/generated_resources.grd").read_text()
    xtb = src / f"chrome/app/resources/generated_resources_{args.lang}.xtb"
    translated = set(re.findall(r'<translation id="(\d+)"', xtb.read_text()))
    missing = []
    total = 0
    for name, attrs, body in MESSAGE.findall(grd):
        total += 1
        meaning = re.search(r'meaning="([^"]*)"', attrs)
        message_id = tclib.GenerateMessageId(presentable(body),
                                             meaning.group(1) if meaning else None)
        if message_id not in translated:
            missing.append(name)
    print(f"{total} Ahoi messages, {len(missing)} without a {args.lang} translation")
    for name in missing:
        print(f"  {name}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
