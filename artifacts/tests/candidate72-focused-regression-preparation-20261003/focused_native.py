import datetime,hashlib,json,os,pathlib,plistlib,re,signal,subprocess,time,sys
R=pathlib.Path.cwd();BASE=R/'.work/agent-queue/72-combined-20261003-run2';O=R/'.work/chromium/src/out/AhoiDev';SOURCE='b6d3237758e10e010cf1ae52cefb57b099b79abb'
GUI=pathlib.Path(sys.argv[1]);verdict=json.loads((GUI/'verdict.json').read_text());required=re.findall(r'^check ([A-Za-z][A-Za-z0-9]+) ',(R/'tools/desktop_e2e/tab-cache-journey.sh').read_text(),re.M)
assert len(required)==12 and verdict.get('pass') is True and all(verdict.get(n) is True for n in required)
launches=[json.loads(s) for s in (GUI/'launch-preflight.jsonl').read_text().splitlines()];assert len(launches)>=2 and all(x.get('source')==SOURCE and 'refusal' not in x for x in launches)
for app in [pathlib.Path('/Applications/AhoiBrowser.app'),O/'AhoiBrowser.app']:assert plistlib.loads((app/'Contents/Info.plist').read_bytes())['AhoiSourceCommit']==SOURCE
L=pathlib.Path('/private/tmp/claude-501/-Volumes-Macintosh-HD---Daten-Cloud-Projekte-Apps-Plattformuebergreifend-AhoiBrowser/4203bcfe-a545-4871-a5c2-f559b26f9fdb/scratchpad');assert not any((L/n).exists() for n in ['build.lock','e2e.lock','h3.lock'])
D=BASE/('focused-native-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'));D.mkdir(exist_ok=False)
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
F=R/'.work/agent-queue/repo'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=F,text=True).strip()==SOURCE
def cases(file,fixture,macro='TEST_F'):
 return [fixture+'.'+n for c,n in re.findall(macro+r'\(\s*(\w+)\s*,\s*(\w+)\s*\)',(F/file).read_text()) if c==fixture]
core=cases('overlay/chromium/src/ahoi/browser/developer_toolkit/developer_tab_cache_unittest.cc','DeveloperTabCacheTest')
core+=cases('overlay/chromium/src/ahoi/browser/developer_toolkit/developer_network_factory_proxy_unittest.cc','DeveloperNetworkFactoryProxyTest')
core+=cases('overlay/chromium/src/ahoi/browser/developer_toolkit/developer_network_factory_revocation_unittest.cc','DeveloperNetworkFactoryProxyTest')
worker=cases('overlay/chromium/src/ahoi/browser/developer_toolkit/developer_network_worker_browsertest.cc','DeveloperNetworkWorkerBrowserTest','IN_PROC_BROWSER_TEST_F')
ui=cases('overlay/chromium/src/ahoi/browser/ui/developer_toolkit/developer_toolkit_bubble_view_unittest.cc','DeveloperToolkitBubbleViewTest')
sync=['ProfileSyncServiceTest.'+n for n in ['ReentrantDifferentSettingRemainsLocalAndSkipsLaterRemoteOverwrite','ReentrantSameSettingIsNotMistakenForRemoteEcho','NormalizedNumericRemoteValueDoesNotAuthorAnEcho','ReentrantLocalValueBounceRetainsItsLatestIntent','ReentrantSyncRevocationDoesNotRestoreOldEchoScope']]
plan=[('ahoi_developer_toolkit_unittests',core),('ahoi_developer_toolkit_ui_unittests',ui),('ahoi_developer_style_compiler_browsertests',worker),('ahoi_sync_unittests',sync)]
assert all(names for _,names in plan) and len(worker)==4 and len(ui)==1 and len(sync)==5
state=dict(sourceCommit=SOURCE,runnerPid=os.getpid(),phase='preflight',jobs=1,retries=0,visibleVerdictSha256=digest(GUI/'verdict.json'),componentManifestSha256=digest(O/'AhoiBrowser.app/Contents/Resources/ahoi-component-runtime.sha256'),results=[])
def save():(D/'state.json').write_text(json.dumps(state,indent=2)+'\n')
def capacity():
 raw=subprocess.check_output(['ioreg','-c','IOHIDSystem'],text=True);idle=int(re.search(r'"HIDIdleTime"\s*=\s*(\d+)',raw).group(1))//10**9
 commands=subprocess.check_output(['ps','-axo','comm='],text=True)
 assert idle>=90 and int(subprocess.check_output(['sysctl','-n','kern.memorystatus_vm_pressure_level'],text=True))==1
 assert '/Applications/AhoiBrowser.app/Contents/' not in commands and not re.search(r'/(clang\+\+|clang|ninja|xcodebuild)\s*$',commands,re.M)
save();capacity();lock=L/'build.lock';fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600);os.write(fd,f'focused candidate regression pid={os.getpid()} source={SOURCE}\n'.encode());os.close(fd);inode=lock.stat().st_ino;proc=None
try:
 for binary,names in plan:
  capacity();assert digest(O/'AhoiBrowser.app/Contents/Resources/ahoi-component-runtime.sha256')==state['componentManifestSha256']
  summary=D/(binary+'-summary.json');cmd=['/usr/bin/nice','-n','15',str(O/binary),'--gtest_filter='+':'.join(names),'--test-launcher-jobs=1','--test-launcher-retry-limit=0','--test-launcher-summary-output='+str(summary)]
  state.update(phase=binary,binarySha256=digest(O/binary),command=cmd);save()
  with (D/(binary+'.log')).open('x') as log:
   proc=subprocess.Popen(cmd,cwd=O,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);state['testPid']=proc.pid;save()
   try:rc=proc.wait(timeout=600)
   except subprocess.TimeoutExpired:
    os.killpg(proc.pid,signal.SIGTERM)
    try:proc.wait(timeout=10)
    except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
    raise RuntimeError('owned focused test timed out; no acceptance')
  data=json.loads(summary.read_text()) if summary.exists() else {};rows={}
  for iteration in data.get('per_iteration_data',[]):
   for name,runs in iteration.items():rows.setdefault(name,[]).extend(runs)
  bad=[name for name in names if not rows.get(name) or any(r.get('status')!='SUCCESS' for r in rows[name])]
  result=dict(binary=binary,exitCode=rc,requiredCases=names,missingOrNonSuccess=bad,binarySha256=state['binarySha256'],nativePass=rc==0 and not bad);state['results'].append(result);save();print(binary,len(names),result['nativePass'],flush=True)
  if not result['nativePass']:raise RuntimeError('focused native regression failed')
 state.update(phase='complete',nativePass=True);save()
except Exception as e:state.update(phase='failed',nativePass=False,failure=str(e));save();raise
finally:
 if (proc is None or proc.poll() is not None) and lock.exists() and lock.stat().st_ino==inode:lock.unlink()
 state['ownLockReleased']=not lock.exists();save()
