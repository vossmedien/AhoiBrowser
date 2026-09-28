#!/bin/bash
# usage: make_rebrand_patch.sh <chromium src> <work dir> <out.patch>
# Rebrands every macOS-active "Chromium" product-name message of the two
# Chromium strings bundles (English source and German translation) and
# writes the difference against the given checkout as a patch. Attribution
# and license texts ("The Chromium Authors", the open source project link)
# stay unchanged. Other locales keep Chromium's translation ids and fall back
# to the English text for rebranded messages.
set -eu
SRC=$1; WORK=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
FILES=(chrome/app/chromium_strings.grd chrome/app/settings_chromium_strings.grdp
       chrome/app/resources/chromium_strings_de.xtb
       components/components_chromium_strings.grd
       components/strings/components_chromium_strings_de.xtb)
KEEP=(--keep "Chromium Authors" --keep "open source" --keep "LINK_CHROMIUM"
      --keep "chromium.org")
rm -rf "$WORK"; mkdir -p "$WORK/a" "$WORK/b"
ln -s "$SRC/tools" "$WORK/b/tools"
for f in "${FILES[@]}"; do
  mkdir -p "$WORK/a/$(dirname "$f")" "$WORK/b/$(dirname "$f")"
  cp "$SRC/$f" "$WORK/a/$f"; cp "$SRC/$f" "$WORK/b/$f"
done
python3 "$HERE/rebrand_messages.py" --checkout "$WORK/b" --mac-active "${KEEP[@]}" \
  --grd chrome/app/chromium_strings.grd --xtb chrome/app/resources/chromium_strings_de.xtb \
  > "$WORK/chrome.log"
python3 "$HERE/rebrand_messages.py" --checkout "$WORK/b" --mac-active "${KEEP[@]}" \
  --grd components/components_chromium_strings.grd \
  --xtb components/strings/components_chromium_strings_de.xtb > "$WORK/components.log"
rm "$WORK/b/tools"
(cd "$WORK" && git diff --no-index --no-prefix a b > "$OUT") || true
test -s "$OUT"
echo "$(grep -c ' -> ' "$WORK/chrome.log" "$WORK/components.log" | tr '\n' ' ')messages; patch $(wc -l < "$OUT") lines"
