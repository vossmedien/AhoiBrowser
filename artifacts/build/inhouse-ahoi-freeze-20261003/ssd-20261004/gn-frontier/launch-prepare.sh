#!/bin/bash
set -euo pipefail
phase=/Users/vossmedien/inhouse/evidence/ahoi-ssd-20261004/gn-frontier
policy=/Users/vossmedien/inhouse/evidence/ahoi-ssd-20261004/current-policy
frozen=/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo73
export AHOI_WORK_ROOT=/Volumes/Daten/Inhouse/AhoiBrowser/work
export AHOI_PLANNED_GROWTH_BYTES=4294967296
export DEPOT_TOOLS_UPDATE=0 AHOI_JOBS=2 AHOI_NINJA_KEEP_GOING=0
mkdir -p "$phase"
finish() {
  code=$?
  python3 - "$phase/state.json" "$code" <<'PY'
import datetime,json,pathlib,sys
p=pathlib.Path(sys.argv[1]);r=json.loads(p.read_text()) if p.exists() else {}
r.update(terminal=True,exitCode=int(sys.argv[2]),finishedAt=datetime.datetime.now(datetime.timezone.utc).isoformat())
p.write_text(json.dumps(r,indent=2)+'\n')
PY
}
trap finish EXIT
python3 - "$phase/state.json" <<'PY'
import datetime,json,os,pathlib
pathlib.Path(__import__('sys').argv[1]).write_text(json.dumps({'ownerThread':'01a0e047-9360-7122-ad28-f76ebc767c97','context':'A29','pid':os.getppid(),'sourceFreeze':'88e631d65f58ac80250e464847072eea00c4c877','plannedGrowthBytes':4294967296,'reserveBytes':8589934592,'gnThreads':2,'ninjaDryRun':True,'terminal':False,'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2)+'\n')
PY
# Current phase budget and mount guard, not the historical frozen host floor.
"$policy/scripts/check-host.sh" --compatible-dev-xcode
source "$frozen/scripts/lib/common.sh"
ahoi_select_xcode compatible-development
ahoi_enable_depot_tools
ahoi_require_overlay_state
python3 "$policy/tools/run_with_disk_reserve.py" \
  --work-root "$AHOI_WORK_ROOT" --policy "$policy/config/toolchain.json" -- \
  /bin/bash /Users/vossmedien/inhouse/evidence/ahoi-ssd-20261004/prepare-wrapper.sh \
  "$AHOI_WORK_ROOT/chromium/src/out/AhoiDev" "$frozen/config/build/ahoi-dev.gn" chrome
