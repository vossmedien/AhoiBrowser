#!/bin/bash
set -euo pipefail
SOURCE=b6bce96774f5440567f4d2f6d08d43526e403b13
E=/Users/vossmedien/inhouse/evidence/ahoi-root-arc-barrier-b6bce967-20261008
N=/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo88-m155-f492d294
W=/Volumes/Daten/Inhouse/AhoiBrowser/work
LOCK="$W/state/desktop-build.lock"
mkdir -p "$E"
phase() { printf '%s %s\n' "$(date -u +%FT%TZ)" "$*" >> "$E/phases.log"; }
cd "$N"
/Users/vossmedien/.local/bin/inhouse-external-root >/dev/null
mkdir "$LOCK" || { phase 'NOT_STARTED: desktop build owner already present'; exit 9; }
printf '{"owner":"%s","thread":"01a11179-cb6b-7dc1-9e53-f3d72fefc198","source":"%s","pid":%s}\n' "$E" "$SOURCE" "$$" > "$LOCK/owner.json"
release() {
  if test -f "$LOCK/owner.json" && /usr/bin/grep -q "$E" "$LOCK/owner.json"; then
    /bin/rm "$LOCK/owner.json"
    /bin/rmdir "$LOCK"
  fi
}
finish() { local result=$?; phase "terminal exit=$result"; printf "%s\n" "$result" > "$E/runner.exit"; release; }
trap finish EXIT
test "$(git rev-parse HEAD)" = 6f06daf0ca0e79091a2fc48f6bd79bb838588149
test -z "$(git status --porcelain --untracked-files=no)"
test ! -e /Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/e2e.lock
phase 'accepted source bundle; mirror checkout under existing Root build lock'
git fetch /private/tmp/ahoi-root-arc-b6bce967.bundle refs/cockpit/root-arc-barrier-b6bce967 > "$E/source-fetch.log" 2>&1
git checkout --detach "$SOURCE" >> "$E/source-fetch.log" 2>&1
test "$(git rev-parse HEAD)" = "$SOURCE"
test -z "$(git status --porcelain --untracked-files=no)"
AHOI_CHROMIUM_OUT_ROOT=/Users/vossmedien/inhouse/scratch/ahoi-chromium.fMMeXkCh
test -d "$AHOI_CHROMIUM_OUT_ROOT/AhoiDev"
printf '%s\n' "$AHOI_CHROMIUM_OUT_ROOT" > "$E/output-root.txt"
export AHOI_CHROMIUM_OUT_ROOT AHOI_WORK_ROOT="$W"
# Reviewed incremental budget: 12.91 GiB existing outputs vs old 16 GiB baseline,
# four own source corrections plus final link/staging; unchanged 8 GiB reserve.
export AHOI_JOBS=2 AHOI_NINJA_KEEP_GOING=1 AHOI_PLANNED_GROWTH_BYTES=4294967296
export DEPOT_TOOLS_UPDATE=0
PB="$W/depot_tools/bootstrap-2@3.11.8.chromium.35_bin/python3/bin"
export PATH="/Users/vossmedien/inhouse/toolchains/git-lfs-v3.8.0/git-lfs-3.8.0:$PB:/usr/bin:/bin:/usr/sbin:/sbin"
phase "output root=$AHOI_CHROMIUM_OUT_ROOT source=$SOURCE pid=$$"
phase apply-overlay
nice -n 10 ./scripts/apply-overlay.sh --compatible-dev-xcode > "$E/focused-apply.log" 2>&1
phase 'guarded SSH build; final signing uses GUI session on internal bundle'
set +e
nice -n 10 ./scripts/build-ahoi.sh dev ahoi_arc_import_unittests > "$E/focused-build-ssh.log" 2>&1
result=$?
set -e
phase "build command exit=$result"
if test "$result" != 0; then
  # Preserve the existing separate signing gate, accepting only its known
  # SSH Keychain failure after a complete compile/link/stamp. Every other
  # failure still terminates this candidate.
  grep -q 'stamped product/revision provenance' "$E/focused-build-ssh.log"
  grep -q errSecInternalComponent "$E/focused-build-ssh.log"
  if grep -E 'error:|FAILED:' "$E/focused-build-ssh.log" | grep -v errSecInternalComponent; then
    phase 'compile/staging failure; signing not attempted'
    exit 3
  fi
fi
APP="$AHOI_CHROMIUM_OUT_ROOT/AhoiDev/AhoiBrowser.app"
phase 'GUI signing and bundle verification'
AHOI_WORK_ROOT="$AHOI_CHROMIUM_OUT_ROOT" bash tools/desktop_e2e/run-in-gui-session.sh \
  sign-root-arc-barrier-b6bce967 "$E/focused-sign" bash -c \
  './scripts/sign-development-app.sh "$1" && ./scripts/verify-built-app.sh "$1"' -- "$APP"
phase 'exact-source output provenance'
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer python3 tools/build_provenance.py \
  --kind dev --app "$APP" --out-dir "$AHOI_CHROMIUM_OUT_ROOT/AhoiDev" \
  --gn-args "$N/config/build/ahoi-dev.gn" --output "$E/focused-build-provenance.json" \
  > "$E/focused-provenance.log" 2>&1
phase 'focused binaries built; visible acceptance remains open'
