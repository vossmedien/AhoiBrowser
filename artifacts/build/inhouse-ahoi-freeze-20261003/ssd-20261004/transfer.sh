#!/bin/bash
set -euo pipefail
owner_evidence=/Users/vossmedien/inhouse/evidence/ahoi-ssd-20261004
source_work=/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work
ssd_work=/Volumes/Daten/Inhouse/AhoiBrowser/work
monitor=/private/tmp/ahoi-phase-budget-check/tools/run_with_disk_reserve.py
policy=/private/tmp/ahoi-phase-budget-check/config/toolchain.json
mkdir -p "$owner_evidence"
finish() {
  rc=$?
  python3 - "$owner_evidence/state.json" "$rc" <<'PY'
import datetime,json,pathlib,sys
p=pathlib.Path(sys.argv[1]); old=json.loads(p.read_text()) if p.exists() else {}
old.update(terminal=True,exitCode=int(sys.argv[2]),finishedAt=datetime.datetime.now(datetime.timezone.utc).isoformat())
tmp=p.with_suffix('.tmp'); tmp.write_text(json.dumps(old,indent=2)+'\n'); tmp.replace(p)
PY
}
trap finish EXIT
python3 "$monitor" --work-root "$ssd_work" --check-work-root
# Mount/identity check precedes mkdir. Old source/cache is kept until acceptance.
mkdir -p "$ssd_work"
python3 - "$owner_evidence/state.json" "$source_work" "$ssd_work" <<'PY'
import datetime,json,os,pathlib,shutil,sys
p=pathlib.Path(sys.argv[1]);p.write_text(json.dumps({'ownerThread':'01a0e047-9360-7122-ad28-f76ebc767c97','context':'A29','pid':os.getppid(),'sourceWork':sys.argv[2],'destinationWork':sys.argv[3],'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'terminal':False,'copyVerified':False,'ssdFreeBeforeBytes':shutil.disk_usage(sys.argv[3]).free},indent=2)+'\n')
PY
python3 "$monitor" --work-root "$ssd_work" --policy "$policy" -- \
  /usr/bin/rsync -aEH --partial --stats \
  "$source_work/chromium" "$source_work/depot_tools" "$source_work/state" "$ssd_work/"
# Recheck mount and compare content/metadata; no deletion in this phase.
python3 "$monitor" --work-root "$ssd_work" --check-work-root
python3 "$monitor" --work-root "$ssd_work" --policy "$policy" -- \
  /usr/bin/rsync -aEHnc --itemize-changes \
  "$source_work/chromium" "$source_work/depot_tools" "$source_work/state" "$ssd_work/" \
  > "$owner_evidence/checksum-dry-run.txt"
test ! -s "$owner_evidence/checksum-dry-run.txt"
python3 - "$owner_evidence/state.json" "$ssd_work" <<'PY'
import json,pathlib,shutil,sys
p=pathlib.Path(sys.argv[1]);r=json.loads(p.read_text());r.update(copyVerified=True,ssdFreeAfterBytes=shutil.disk_usage(sys.argv[2]).free,sourceRetained=True,rootSwitched=False);p.write_text(json.dumps(r,indent=2)+'\n')
PY
