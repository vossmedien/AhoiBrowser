import pathlib,subprocess,json,os,hashlib,plistlib,datetime
R=pathlib.Path.cwd();D=R/'.work/agent-queue/68/dev-native';O=R/'.work/chromium/src/out/AhoiDev';L=pathlib.Path('/private/tmp/claude-501/-Volumes-Macintosh-HD---Daten-Cloud-Projekte-Apps-Plattformuebergreifend-AhoiBrowser/4203bcfe-a545-4871-a5c2-f559b26f9fdb/scratchpad');source='c2943397d86499f563070e8ee09adce7bde66436';assert plistlib.loads((O/'AhoiBrowser.app/Contents/Info.plist').read_bytes())['AhoiSourceCommit']==source;assert json.loads((R/'.work/agent-queue/68/state.json').read_text())['phase']=='compiled-signed-provenance-complete-native-gates-pending';assert not any((L/n).exists() for n in ['build.lock','e2e.lock','h3.lock']);D.mkdir(exist_ok=False);lock=L/'build.lock';fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600);os.write(fd,f'codex dev-native pid={os.getpid()}\n'.encode());os.close(fd);lock_inode=lock.stat().st_ino
state={'runnerPid':os.getpid(),'sourceCommit':source,'phase':'running','jobs':1,'retries':0,'results':[],'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat()}
def save(): (D/'state.json').write_text(json.dumps(state,indent=2)+'\n')
save()
try:
 name='ahoi_developer_toolkit_unittests';binary=O/name;cmd=[str(binary),'--test-launcher-jobs=1','--test-launcher-retry-limit=0','--test-launcher-summary-output='+str(D/'summary.json')];state['command']=cmd;state['binarySha256']=hashlib.sha256(binary.read_bytes()).hexdigest();state['componentManifestSha256']=hashlib.sha256((O/'AhoiBrowser.app/Contents/Resources/ahoi-component-runtime.sha256').read_bytes()).hexdigest();save()
 with (D/'test.log').open('w') as f:p=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT);state['testPid']=p.pid;save();rc=p.wait()
 data=json.loads((D/'summary.json').read_text());rows={}
 for iteration in data.get('per_iteration_data',[]):
  for case,runs in iteration.items():rows.setdefault(case,[]).extend(runs)
 required=json.loads((R/'.work/agent-queue/68/native-execution-plan.json').read_text())['core']['requiredCases'];missing=[case for case in required if case not in rows];bad={case:[run.get('status') for run in runs] for case,runs in rows.items() if not runs or any(run.get('status')!='SUCCESS' for run in runs)};state.update(executedCases=len(rows),missingRequiredCases=missing,nonSuccess=bad,nativePass=rc==0 and bool(rows) and not missing and not bad);save()
 state['exitCode']=rc;state['phase']='complete';state['finishedAt']=datetime.datetime.now(datetime.timezone.utc).isoformat();save();print(name,rc,flush=True)
finally:
 if lock.exists() and lock.stat().st_ino==lock_inode:lock.unlink()
