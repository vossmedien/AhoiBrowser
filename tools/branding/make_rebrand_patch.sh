#!/bin/bash
# usage: make_rebrand_patch.sh <src> <work dir> <out.patch> [ui|extras]
# Rebrands every macOS-active "Chromium" product-name message of the two
# Chromium strings bundles (English source and German translation) and
# writes the difference against the given checkout as a patch. With "ui" it
# does the same for the two general UI bundles (generated_resources and
# components_strings) instead, selecting messages with the GRIT defines of
# the checkout's out/AhoiDev build; run it on a checkout with the earlier
# patches applied, since those bundles also carry Ahoi's own messages.
# "extras" does the same for the extension and privacy sandbox bundles.
# Attribution and license texts ("The Chromium Authors", the open source
# project link) stay unchanged. Other locales keep Chromium's translation ids and fall back
# to the English text for rebranded messages.
set -eu
SRC=$1; WORK=$2; OUT=$3; MODE=${4:-product}
HERE=$(cd "$(dirname "$0")" && pwd)
if [ "$MODE" = ui ] || [ "$MODE" = extras ]; then
  if [ "$MODE" = ui ]; then
    GRDS=(chrome/app/generated_resources.grd components/components_strings.grd)
    XTBS=(chrome/app/resources/generated_resources_de.xtb
          components/strings/components_strings_de.xtb)
  else
    GRDS=(extensions/strings/extensions_strings.grd
          components/privacy_sandbox_strings.grd)
    XTBS=(extensions/strings/extensions_strings_de.xtb
          components/strings/privacy_sandbox_strings_de.xtb)
  fi
  FILES=("${XTBS[@]}")
  add_with_parts() { # <file relative to SRC>; parts nest, relative to parent
    FILES+=("$1")
    local part
    for part in $(grep -o '<part file="[^"]*"' "$SRC/$1" | cut -d'"' -f2); do
      add_with_parts "$(dirname "$1")/$part"
    done
  }
  for grd in "${GRDS[@]}"; do add_with_parts "$grd"; done
else
FILES=(chrome/app/chromium_strings.grd chrome/app/settings_chromium_strings.grdp
       chrome/app/resources/chromium_strings_de.xtb
       components/components_chromium_strings.grd
       components/strings/components_chromium_strings_de.xtb)
fi
KEEP=(--keep "Chromium Authors" --keep "open source" --keep "LINK_CHROMIUM"
      --keep "chromium.org")
rm -rf "$WORK"; mkdir -p "$WORK/a" "$WORK/b"
ln -s "$SRC/tools" "$WORK/b/tools"
for f in "${FILES[@]}"; do
  mkdir -p "$WORK/a/$(dirname "$f")" "$WORK/b/$(dirname "$f")"
  cp "$SRC/$f" "$WORK/a/$f"; cp "$SRC/$f" "$WORK/b/$f"
done
if [ "$MODE" = ui ] || [ "$MODE" = extras ]; then
  for i in 0 1; do
    python3 "$HERE/rebrand_messages.py" --checkout "$WORK/b" --mac-active \
      "${KEEP[@]}" --build-dir "$SRC/out/AhoiDev" --grd "${GRDS[$i]}" \
      --xtb "${XTBS[$i]}" > "$WORK/ui$i.log"
  done
  rm "$WORK/b/tools"
  (cd "$WORK" && git diff --no-index --no-prefix a b > "$OUT") || true
  test -s "$OUT"
  echo "$(cat "$WORK"/ui*.log | grep -c ' -> ') messages;" \
    "patch $(wc -l < "$OUT") lines"
  exit 0
fi
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
