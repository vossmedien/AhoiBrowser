#!/bin/bash
# Visible Settings -> native importer -> profile History/journal acceptance.
# Synthetic closed sources only; caller supplies the exact signed candidate.
set -euo pipefail
umask 077
APP=$1
OUT=$2
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=${AHOI_E2E_ARC_ROOT:-$(mktemp -d /private/tmp/ahoi-arc-history-e2e-XXXXXX)}
CASE=${AHOI_E2E_ARC_CASE:-main}
export CASE
export ROOT OUT APP
python3 - <<'GUARD'
import os,stat
from pathlib import Path
r=Path(os.environ['ROOT'])
assert r.is_absolute() and r.parent==Path('/private/tmp')
assert r.name.startswith('ahoi-arc-history-e2e-') and not r.is_symlink()
def protected(p,directory=False,owner_only=True):
 st=p.lstat();assert st.st_uid==os.getuid(),str(p)
 if owner_only:assert not(st.st_mode&0o077),str(p)
 assert stat.S_ISDIR(st.st_mode) if directory else (stat.S_ISREG(st.st_mode) and st.st_nlink==1),str(p)
protected(r,True)
for subtree in ['source','target']:
 p=r/subtree
 if p.exists() or p.is_symlink():
  protected(p,True)
  # These are the only trees the fixture/preparation mutates. Validate every
  # actual payload before SQLite or journal operations can follow a link.
  for q in p.rglob('*'):
   if q.name.startswith('Singleton') or q.name=='RunningChromeVersion':continue
   assert not q.is_symlink(),str(q)
   protected(q,q.is_dir(),subtree=='source' or q.name in ['History','ArcHistoryImportJournal.json'])
GUARD
mkdir -p "$OUT" "$ROOT/source" "$ROOT/target"
printf '%s\n' "$ROOT" > "$OUT/fixture-root.txt"
python3 - <<'PY'
import json,os,sqlite3,time
from pathlib import Path
root=Path(os.environ['ROOT'])
arc=root/'source/Arc'
if arc.exists():
 raise SystemExit(0)
arc.mkdir(mode=0o700)
spaces=[];items=[]
for name,space in [('Default','home'),('Profile 1','work')]:
 p=arc/'User Data'/name;p.mkdir(parents=True,mode=0o700)
 (p/'Preferences').write_text('{}');(p/'Preferences').chmod(0o600)
 db=sqlite3.connect(p/'History')
 db.executescript("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT); INSERT INTO meta VALUES('version','70'); CREATE TABLE urls(id INTEGER PRIMARY KEY,url TEXT,title TEXT,visit_count INTEGER,typed_count INTEGER,last_visit_time INTEGER,hidden INTEGER);")
 now=int((time.time()+11644473600)*1000000)
 db.execute('INSERT INTO urls VALUES(?,?,?,?,?,?,?)',(1,'https://history-'+space+'.example/','Private synthetic history '+space,3,1,now-86400000000,0))
 if name=='Default':
  for n,url,stamp,hidden in [(2,'https://user:secret@credentials.invalid/',now-86400000000,0),(3,'file:///private/synthetic',now-86400000000,0),(4,'https://future.invalid/',now+9*86400000000,0),(5,'https://expired.invalid/',now-400*86400000000,0),(6,'https://hidden.invalid/',now-86400000000,1)]:
   db.execute('INSERT INTO urls VALUES(?,?,?,?,?,?,?)',(n,url,'Excluded',1,0,stamp,hidden))
 db.commit();db.close();(p/'History').chmod(0o600)
 pin=space+'-pinned';unpin=space+'-unpinned';page=space+'-page'
 profile={'default':True} if name=='Default' else {'custom':{'_0':{'directoryBasename':name,'machineID':'synthetic'}}}
 spaces += [space,{'id':space,'title':'Synthetic '+space,'profile':profile,'containerIDs':[],'newContainerIDs':[{'pinned':{}},pin,{'unpinned':{'_0':{'shared':{}}}},unpin]}]
 for ident,children in [(pin,[page]),(unpin,[])]:
  items += [ident,{'id':ident,'parentID':None,'childrenIds':children,'title':None,'data':{'itemContainer':{'containerType':{'spaceItems':{'_0':space}}}}}]
 items += [page,{'id':page,'parentID':pin,'childrenIds':[],'title':'Sidebar '+space,'data':{'tab':{'savedTitle':'Sidebar '+space,'savedURL':'https://sidebar-'+space+'.example/'}}}]
(arc/'StorableSidebar.json').write_text(json.dumps({'version':1,'sidebar':{'containers':[{'global':{}},{'spaces':spaces,'items':items}]}}));(arc/'StorableSidebar.json').chmod(0o600)
PY
python3 - <<'PREP'
import os,json,sqlite3,time,hashlib
from pathlib import Path
r=Path(os.environ['ROOT']);case=os.environ['CASE'];p=r/'target/Default'
def digest(db):
 data=[db.execute('select * from '+t+' order by 1').fetchall() for t in ['urls','visits','visit_source']]
 return hashlib.sha256(json.dumps(data,sort_keys=True).encode()).hexdigest()
if case=='rollback':
 db=sqlite3.connect(p/'History');before=digest(db)
 db.close();Path(os.environ['OUT']+'/baseline.sha256').write_text(before)
 db=sqlite3.connect(r/'source/Arc/User Data/Default/History');now=int((time.time()+11644473600)*1000000)
 db.execute('update urls set last_visit_time=? where id=1',(now-3600000000,))
 db.execute('INSERT INTO urls VALUES(?,?,?,?,?,?,?)',(7,'https://created-rollback.example/','Synthetic rollback',1,0,now-1800000000,0));db.commit();db.close()
if case=='recovery':
 journal=p/'Ahoi/ArcHistoryImportJournal.json';committed=json.loads(journal.read_text())
 # Model a post-DB/pre-journal crash from the actual verified native backup.
 candidates=list((p/'Ahoi/Arc Import Backups').glob('*/manifest.json'))
 for mf in sorted(candidates,key=lambda x:x.stat().st_mtime,reverse=True):
  m=json.loads(mf.read_text());material='arc-history-v1';entries={f['backup_name']:f for f in m['files']}
  for name,f in sorted(entries.items()):
   role=f['role']
   if role.startswith('arc_') and role.endswith('_history') and f['present']:
    key=role[4:-8];wal=entries[name+'-wal'];material+='\n'+key+'='+f['sha256']+'/'+(wal['sha256'] if wal['present'] else 'absent')
  if hashlib.sha256(material.encode()).hexdigest()!=committed['history_key']:continue
  prepared={'version':1,'state':'prepared','history_key':committed['history_key'],'backup_identifier':mf.parent.name,'manifest_sha256':hashlib.sha256(mf.read_bytes()).hexdigest(),'snapshot_sha256':m['snapshot_sha256']}
  journal.write_text(json.dumps(prepared));journal.chmod(0o600);break
 else:raise RuntimeError('No backup for committed history key')
 db=sqlite3.connect(p/'History');Path(os.environ['OUT']+'/baseline.sha256').write_text(digest(db));db.close()
PREP
PORT=${AHOI_E2E_CDP_PORT:-9357}
if lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  echo "CDP port occupied; no foreign browser driven" >&2; exit 6
fi
. "$SCRIPT_DIR/browser_launch.sh"
PID=
trap '[ -n "${PID:-}" ] && kill -0 "$PID" 2>/dev/null && kill "$PID"; true' EXIT
FAULT_ARGS=(); [ "$CASE" = rollback ] && FAULT_ARGS+=(--ahoi-e2e-arc-fail-after-write)
ahoi_launch_browser "$OUT/browser.log" --user-data-dir="$ROOT/target" --no-first-run --no-default-browser-check --remote-debugging-port="$PORT" --ahoi-e2e-arc-source-directory="$ROOT/source" chrome://settings/importData ${FAULT_ARGS[@]+"${FAULT_ARGS[@]}"}
printf '%s\n' "$PID" > "$OUT/browser.pid"
export PORT
for i in $(seq 1 60); do
  LISTENERS=$(lsof -nP -iTCP:"$PORT" -sTCP:LISTEN -t 2>/dev/null | sort -u || true)
  [ -n "$LISTENERS" ] && break
  sleep 0.5
done
[ "$LISTENERS" = "$PID" ] || { echo "CDP endpoint not owned by candidate PID" >&2; exit 7; }
ps -o command= -p "$PID" | grep -F -- "--user-data-dir=$ROOT/target" >/dev/null || exit 7
printf '%s\n' "endpointPid=$PID profile=$ROOT/target" > "$OUT/endpoint-binding.txt"
if [ -n "${AHOI_AXTOOL:-}" ]; then
  "$AHOI_AXTOOL" activate "$PID" > "$OUT/activation.txt"
  "$AHOI_AXTOOL" focused "$PID" > "$OUT/focus-before.txt"
fi
node --input-type=module - <<'JS'
import fs from 'node:fs';
const port=process.env.PORT,out=process.env.OUT,mode=process.env.CASE;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let target;
for(let i=0;i<80;i++){
 try{target=await(await fetch(`http://127.0.0.1:${port}/json/version`)).json();if(target.webSocketDebuggerUrl)break;}catch{}
 await sleep(500);
}
if(!target)throw Error('Native page not started');
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise(r=>ws.onopen=r);
let id=0,sessionId;const pending=new Map();ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(m.error.message)):p.resolve(m.result);}};
const call=(method,params={})=>new Promise((resolve,reject)=>{const n=++id;pending.set(n,{resolve,reject});ws.send(JSON.stringify({id:n,method,params,sessionId}));});
// Attach to the actual Settings target after its creation. The startup target
// is still being constructed and must not be attached across that navigation.
const {targetId}=await call('Target.createTarget',{url:'chrome://settings/importData'});
await call('Target.activateTarget',{targetId});
({sessionId}=await call('Target.attachToTarget',{targetId,flatten:true}));
const prefix=`function find(r,q){let v=r.querySelector(q);if(v)return v;for(let e of r.querySelectorAll('*'))if(e.shadowRoot){v=find(e.shadowRoot,q);if(v)return v;}return null;} const section=()=>find(document,'settings-ahoi-arc-import-section');`;
async function ev(body){const r=await call('Runtime.evaluate',{expression:`(()=>{${prefix}${body}})()`,returnByValue:true,awaitPromise:true,userGesture:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.text+JSON.stringify(r.exceptionDetails.exception));return r.result.value;}
async function until(body){for(let i=0;i<120;i++){const r=await ev(body);if(r)return r;await sleep(500);}throw Error('UI stage timeout');}
await sleep(5000);
await until(`const d=find(document,'settings-import-data-dialog');return d?.browserProfiles_?.some(p=>p.ahoiImportKind==='arc');`);
await ev(`const d=find(document,'settings-import-data-dialog'); const sel=d.shadowRoot.querySelector('#browserSelect');const idx=d.browserProfiles_.findIndex(p=>p.ahoiImportKind==='arc');if(idx<0)throw Error('Arc option missing');sel.selectedIndex=idx;sel.dispatchEvent(new Event('change',{bubbles:true}));return true;`);
async function discover(){if(!await ev(`return !!section()?.shadowRoot.querySelector('#ahoiArcDiscover');`)){
 await call('Page.reload');await until(`const d=find(document,'settings-import-data-dialog');return d?.browserProfiles_?.some(p=>p.ahoiImportKind==='arc');`);
 await ev(`const d=find(document,'settings-import-data-dialog'),sel=d.shadowRoot.querySelector('#browserSelect');sel.selectedIndex=d.browserProfiles_.findIndex(p=>p.ahoiImportKind==='arc');sel.dispatchEvent(new Event('change',{bubbles:true}));return true;`);
 await until(`return !!section()?.shadowRoot.querySelector('#ahoiArcDiscover');`);
}await ev(`section().shadowRoot.querySelector('#ahoiArcDiscover').click();return true;`);const stage=await until(`const s=section();return ['preview','error','sourceInUse'].includes(s.arcImportStage_)&&{stage:s.arcImportStage_,status:s.arcImportPreview_?.status};`);if(stage.stage!=='preview')throw Error('Discovery failed '+JSON.stringify(stage));}
async function capture(name){const r=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(`${out}/${name}.png`,Buffer.from(r.data,'base64'));}
async function commit(sidebar){await ev(`const s=section();const c=s.shadowRoot.querySelector('#ahoiArcImportSidebar');if(c.checked!==${sidebar})c.click();const split=s.shadowRoot.querySelector('#ahoiArcReconstructSplits');if(split?.checked)split.click();return true;`);await sleep(100);await ev(`const b=section().shadowRoot.querySelector('#ahoiArcCommit');if(b.disabled)throw Error('Commit disabled');b.click();return true;`);await until(`const s=section();return ['done','error','sourceInUse'].includes(s.arcImportStage_);`);return await ev(`return section().arcImportResult_;`);}
const results={};
await discover();await capture('01-preview');
if(mode==='main'){
 results.historyOnly=await commit(false);await capture('02-history-only');if(results.historyOnly.history?.added!==2||results.historyOnly.status!=='ok')throw Error('History-only failed '+JSON.stringify(results.historyOnly));
 await discover();results.combined=await commit(true);await capture('03-combined');if(results.combined.status!=='ok'||results.combined.history?.status!=='noChanges')throw Error('Combined failed');
 await discover();results.repeat=await commit(true);await capture('04-repeat');if(results.repeat.status!=='noChanges'||!results.repeat.history?.selected)throw Error('Replay skipped history');
}else if(mode==='separated'){
 await ev(`section().shadowRoot.querySelector('cr-checkbox[data-arc-profile="Profile 1"]').click();return true;`);
 results.separated=await commit(true);await capture('02-separated');if(results.separated.history?.added!==2||results.separated.separatedWorkspaces!==1)throw Error('Separated profile import failed '+JSON.stringify(results.separated));
}else if(mode==='rollback'){
 results.rollback=await commit(true);await capture('02-rollback');if(results.rollback.history?.status!=='transactionFailed')throw Error('Rollback was not exercised '+JSON.stringify(results.rollback));
}else if(mode==='recovery'){
 results.recovery={preview:'ok',automaticPreparedRecovery:true};await capture('02-recovered');
}
const privacy=await ev(`const t=section().shadowRoot.textContent;return !t.includes('history-home.example')&&!t.includes('Private synthetic history');`);if(!privacy)throw Error('History URLs/titles exposed in import UI');
fs.writeFileSync(`${out}/ui-results.json`,JSON.stringify({mode,results,privacy,entry:'visible Settings controls; native backend, no mocked responses'},null,2));
ws.close();
JS
kill "$PID"
for i in $(seq 1 30); do kill -0 "$PID" 2>/dev/null || break; sleep 1; done
PID=
if rg 'FATAL|DCHECK failed' "$OUT/browser.log" > "$OUT/renderer-failures.txt"; then
  echo "Native renderer failure; no successful journey verdict" >&2; exit 8
fi
python3 - <<'PY'
import os,sqlite3,json
from pathlib import Path
r=Path(os.environ['ROOT']);case=os.environ['CASE'];p=r/'target/Default'
def snapshot(db):
 import hashlib
 rows=[db.execute('select * from '+t+' order by 1').fetchall() for t in ['urls','visits','visit_source']]
 return hashlib.sha256(json.dumps(rows,sort_keys=True).encode()).hexdigest()
def count(path):
 db=sqlite3.connect('file:'+str(path)+'?mode=ro',uri=True)
 n=db.execute('select count(*) from visits v join visit_source s on v.id=s.id where s.source=8').fetchone()[0];db.close();return n
result={'mode':case}
if case=='main':assert count(p/'History')==2;result['arc_visits']=2
if case=='separated':
 entries=json.loads((r/'target/Local State').read_text())['ahoi']['isolated_profiles'];assert len(entries)==1,entries
 assert count(p/'History')==1;assert count(r/'target'/entries[0]['profile_dir']/'History')==1
 result['isolated_services']={'main':1,'separated':1}
if case in ['rollback','recovery']:
 db=sqlite3.connect(p/'History');actual=snapshot(db);baseline=Path(os.environ['OUT']+'/baseline.sha256').read_text();assert actual==baseline,(case,actual,baseline)
 db.close();result['baseline_url_visit_sources_preserved']=True
journal=json.loads((p/'Ahoi/ArcHistoryImportJournal.json').read_text());assert journal['state']=='committed'
result['history_journal']=journal['state'];result['pass']=True
Path(os.environ['OUT']+'/native-results.json').write_text(json.dumps(result,indent=2));Path(os.environ['OUT']+'/verdict.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
PY
