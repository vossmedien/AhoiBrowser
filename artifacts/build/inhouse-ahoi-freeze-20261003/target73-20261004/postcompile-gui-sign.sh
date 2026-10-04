#!/bin/bash
# Run once in the authenticated target GUI Terminal; no compilation or launch.
set -euo pipefail
umask 077

export AHOI_WORK_ROOT='/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work'
export AHOI_XCODE_DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer'
export DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer'
export AHOI_DEV_CODESIGN_IDENTITY='Apple Development: Christian Voss (2265UJB5KF)'
unset AHOI_ALLOW_ADHOC_DEV_SIGNING AHOI_DEV_CODESIGN_KEYCHAIN

repo='/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo73'
out='/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/chromium/src/out/AhoiDev'
app="${out}/AhoiBrowser.app"
phase='/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/target73-88e631d6-attempt3/postcompile-gui-sign'
lock='/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/state/target73-postcompile-sign.lock'
receipt="${phase}/receipt.json"
log="${phase}/sign-verify-provenance.log"
expected='88e631d65f58ac80250e464847072eea00c4c877'

[ ! -e "${phase}" ] || { echo 'Existing signing phase: inspect its receipt before any retry.' >&2; exit 70; }
mkdir "${lock}"
mkdir "${phase}"
stage='preflight'
finish() {
  rc=$?
  trap - EXIT
  python3 - "${receipt}" "${rc}" "${stage}" <<'PY'
import datetime, json, pathlib, sys
p = pathlib.Path(sys.argv[1])
x = json.loads(p.read_text()) if p.exists() else {}
x.update(status="terminal", exitCode=int(sys.argv[2]), lastStage=sys.argv[3],
         terminalAt=datetime.datetime.now(datetime.timezone.utc).isoformat())
p.write_text(json.dumps(x, indent=2) + "\n")
PY
  rmdir "${lock}"
  echo "Ahoi postcompile exit ${rc}; receipt ${receipt}"
  exit "${rc}"
}
trap finish EXIT
exec >"${log}" 2>&1

python3 - "${receipt}" "${expected}" "${app}" "${log}" <<'PY'
import datetime, json, pathlib, sys
x = dict(ownerThread="01a0e047-9360-7122-ad28-f76ebc767c97",
         sourceCommit=sys.argv[2], app=sys.argv[3], log=sys.argv[4],
         executionContext="target authenticated GUI Terminal",
         status="running", rebuild=False, adHoc=False,
         startedAt=datetime.datetime.now(datetime.timezone.utc).isoformat())
pathlib.Path(sys.argv[1]).write_text(json.dumps(x, indent=2) + "\n")
PY

[ "$(git -C "${repo}" rev-parse HEAD)" = "${expected}" ]
[ -z "$(git -C "${repo}" status --porcelain)" ]
[ "$(plutil -extract AhoiSourceCommit raw "${app}/Contents/Info.plist")" = "${expected}" ]
[ "$(plutil -extract AhoiBuildProfile raw "${app}/Contents/Info.plist")" = 'dev' ]
[ -f "${out}/ahoi-dependency-build-workarounds.json" ]

stage='sign-development-app'
'/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo73/scripts/sign-development-app.sh' "${app}"
stage='verify-built-app'
'/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo73/scripts/verify-built-app.sh' "${app}"
stage='build-provenance'
python3 '/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo73/tools/build_provenance.py' \
  --kind dev \
  --app "${app}" \
  --out-dir "${out}" \
  --gn-args '/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo73/config/build/ahoi-dev.gn' \
  --output '/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo73/artifacts/build/ahoi-dev-build.json'
stage='complete'
