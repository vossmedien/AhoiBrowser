#!/usr/bin/env python3
# Copyright 2026 The AhoiBrowser Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rebrands selected Chromium product-name messages and their translations.

Changing a message's English source changes its GRIT fingerprint, so the
translation bundles must move to the new id or the locale silently falls
back to English. For each named message this replaces the product word in
the .grd source, recomputes old and new ids with the checkout's own GRIT,
and rewrites the matching <translation> entries of the given .xtb files.

Usage:
  rebrand_messages.py --checkout SRC --grd REL --xtb REL [--xtb REL ...]
      --message NAME [--message NAME ...] [--old Chromium] [--new AhoiBrowser]
Writes the edited files in place inside SRC; print a summary per message.
"""

import argparse
import pathlib
import re
import sys
import tempfile


def message_blocks(text, names, strict=True):
  """Every <message> block of each name; names may repeat across <if>s."""
  blocks = []
  for name in names:
    found = list(re.finditer(
        r'<message\s+name\s*=\s*"%s"[^>]*>.*?</message>' % re.escape(name),
        text,
        re.S))
    if not found and strict:
      sys.exit("message not found: " + name)
    blocks.extend((name, match.group(0)) for match in found)
  return blocks


def message_id(checkout, block):
  sys.path.insert(0, str(checkout / "tools/grit"))
  from grit import grd_reader  # pylint: disable=import-outside-toplevel
  grd = ('<?xml version="1.0" encoding="UTF-8"?>\n'
         '<grit latest_public_release="0" current_release="1">\n'
         '<outputs></outputs>\n<release seq="1"><messages>\n' + block +
         '\n</messages></release>\n</grit>\n')
  with tempfile.NamedTemporaryFile("w", suffix=".grd", delete=False) as out:
    out.write(grd)
  root = grd_reader.Parse(out.name, debug=False)
  for node in root.Preorder():
    if node.name == "message":
      return node.GetCliques()[0].GetMessage().GetId()
  sys.exit("no message parsed")


MAC_DEFINES = {
    "_is_chrome_for_testing_branded": False,
    "enable_extensions": True,
    "enable_extensions_core": True,
    "enable_pdf": True,
    "toolkit_views": True,
    "use_titlecase": True,
}


def mac_active_messages(checkout, grd_path, old, keep):
  """Names of messages GRIT keeps for macOS whose text contains `old`."""
  sys.path.insert(0, str(checkout / "tools/grit"))
  from grit import grd_reader  # pylint: disable=import-outside-toplevel
  root = grd_reader.Parse(str(grd_path), defines=MAC_DEFINES,
                          target_platform="darwin", debug=False)
  names = []
  for node in root.ActiveDescendants():
    if node.name != "message":
      continue
    text = node.GetCliques()[0].GetMessage().GetPresentableContent()
    if (re.search(r"%s(?!\s?OS\b)" % re.escape(old), text) and
        not any(phrase in text for phrase in keep)):
      names.append(node.attrs["name"])
  return sorted(set(names))


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--checkout", type=pathlib.Path, required=True)
  parser.add_argument("--grd", required=True)
  parser.add_argument("--xtb", action="append", default=[])
  parser.add_argument("--message", action="append", default=[])
  parser.add_argument(
      "--mac-active", action="store_true",
      help="select every message active on macOS that contains --old")
  parser.add_argument(
      "--keep", action="append", default=[],
      help="skip messages whose text contains this (attribution, license)")
  parser.add_argument("--old", default="Chromium")
  parser.add_argument("--new", default="AhoiBrowser")
  args = parser.parse_args()

  # The product word only; "Chromium OS"/"ChromiumOS" name another product.
  product_word = re.compile(r"%s(?!\s?OS\b)" % re.escape(args.old))
  grd_path = args.checkout / args.grd
  # The .grd and the .grdp parts it includes hold the messages.
  sources = [grd_path] + [
      grd_path.parent / part for part in re.findall(
          r'<part file="([^"]+)"', grd_path.read_text(encoding="utf-8"))]
  texts = {path: path.read_text(encoding="utf-8") for path in sources}
  if args.mac_active:
    args.message = mac_active_messages(args.checkout, grd_path, args.old,
                                       args.keep)
  if not args.message:
    sys.exit("no messages selected")
  changes = []  # (name, path, old block, new block, old id, new id)
  for path, text in texts.items():
    for name, block in message_blocks(text, args.message, strict=False):
      head, _, rest = block.partition(">")
      content = product_word.sub(args.new, rest)
      if content == rest:
        continue
      new_block = head + ">" + content
      changes.append((name, path, block, new_block,
                      message_id(args.checkout, block),
                      message_id(args.checkout, new_block)))
  for name in args.message:
    if not any(change[0] == name for change in changes):
      sys.exit("%s has no %r to replace" % (name, args.old))
  for _, path, block, new_block, _, _ in changes:
    texts[path] = texts[path].replace(block, new_block, 1)
  for path, text in texts.items():
    path.write_text(text, encoding="utf-8")

  for xtb in args.xtb:
    path = args.checkout / xtb
    text = path.read_text(encoding="utf-8")
    for name, _, _, _, old_id, new_id in changes:
      pattern = re.compile(
          r'<translation id="%s">(.*?)</translation>' % old_id, re.S)
      match = pattern.search(text)
      if not match:
        print("%s: %s has no translation (id %s)" % (xtb, name, old_id))
        continue
      translated = product_word.sub(args.new, match.group(1))
      text = (text[:match.start()] +
              '<translation id="%s">%s</translation>' % (new_id, translated) +
              text[match.end():])
    path.write_text(text, encoding="utf-8")

  for name, _, _, _, old_id, new_id in changes:
    print("%s %s -> %s" % (name, old_id, new_id))


if __name__ == "__main__":
  main()
