"""REAL local PID1; harness baseline-starts dummy once, NEVER restores after launch.
Only test-compiled helpers are executed. No Xochitl/vendor/evdev calls exist here.
"""
import os,sys,subprocess,json,time,uuid,tempfile,shutil,signal
from pathlib import Path
R=Path(__file__).resolve().parents[1];BASE=Path('/run/inkbound-owner3-test')
BUILD=Path(os.environ.get('OWNER_TEST_BUILD',str(R/'build-owner-host')))
def run(*args,check=True):return subprocess.run(args,text=True,capture_output=True,check=check)
def show(unit):
 p=run('systemctl','show',unit,'-p','Id,LoadState,ActiveState,SubState,MainPID,ControlPID,Job,InvocationID,ControlGroup,Result,ExecMainCode,ExecMainStatus',check=False)
 return dict(l.split('=',1) for l in p.stdout.splitlines() if '=' in l)
def wait(fn,seconds=22):
 until=time.monotonic()+seconds
 while time.monotonic()<until:
  if fn():return
  time.sleep(.03)
 raise AssertionError('wait deadline')
def logs(unit):return run('journalctl','-u',unit,'--no-pager','-o','cat','-n','150',check=False).stdout
assert os.geteuid()==0 and Path('/proc/1/comm').read_text().strip()=='systemd'
version=run('systemctl','--version').stdout.splitlines()[0];print(version,flush=True)
print('HOST_LAYOUT',Path('/proc/self/cgroup').read_text().strip(),flush=True)
cases=sys.argv[1:] or ['normal','crash','term-responsive','term-ignoring','descendant','shell-loss','controller-kill','before-permit','after-permit','pruning','duplicate','initially-inactive','restore-failure','reference-loss','owner-loss','permit-reuse','forged-permit','launcher-stock-cgroup','arm-provider','frontend-loss-before']
results=[]
for case in cases:
 s=uuid.uuid4().hex;owner=f'inkbound-owner3-{s}.service';child=f'inkbound-direct3-{s}.service';stock=f'inkbound-stock3-{s}.service'
 tmp=Path(tempfile.mkdtemp(prefix='inkbound-owner3-vm-'));tmp.chmod(0o755)
 for name in ['inkbound-recovery-owner-vm','recovery-client-vm']:
  shutil.copy2(BUILD/name,tmp/name);(tmp/name).chmod(0o755)
 stockdir=tmp/'stock';stockdir.mkdir();launcher=None;out=None;controller=None
 try:
  run('systemd-run','--quiet','--collect','--unit='+stock,'-p','Type='+('exec' if case=='launcher-stock-cgroup' else 'oneshot'),'-p','RemainAfterExit=yes',str(tmp/'inkbound-recovery-owner-vm'),'--dummy-loop' if case=='launcher-stock-cgroup' else '--dummy',str(stockdir))
  wait(lambda:show(stock).get('ActiveState')=='active');assert (stockdir/'starts').read_text().count('start')==1
  if case=='initially-inactive':run('systemctl','stop',stock)
  out=open(tmp/'launcher.log','w');env=dict(os.environ,OWNER_TEST_SESSION=s)
  command=[str(tmp/'inkbound-recovery-owner-vm'),'--launch','normal' if case=='launcher-stock-cgroup' else case]
  if case=='frontend-loss-before':env['OWNER_TEST_LAUNCH_CASE']='before-permit'
  if case in ['arm-provider','frontend-loss-before']:command=[str(tmp/'recovery-client-vm'),'--arm']
  if case=='launcher-stock-cgroup':command=['/bin/bash','-c','kill -STOP $$; exec "$@"','stock-launcher',*command]
  if case in ['shell-loss','controller-kill']:
   # A real initiating shell, distinct from the controller and both PID1 units.
   command=['/bin/bash','-c','"$1" --launch "$2" & c=$!; echo "$c" > "$3"; wait "$c"; exec sleep 30','test-shell',str(tmp/'inkbound-recovery-owner-vm'),case,str(tmp/'controller.pid')]
  launcher=subprocess.Popen(command,env=env,stdout=out,stderr=out,start_new_session=True)
  if case=='launcher-stock-cgroup':
   os.waitpid(launcher.pid,os.WUNTRACED)
   cg=show(stock)['ControlGroup'];assert cg=='/system.slice/'+stock
   Path('/sys/fs/cgroup'+cg+'/cgroup.procs').write_text(str(launcher.pid))
   assert '0::'+cg in Path(f'/proc/{launcher.pid}/cgroup').read_text()
   os.kill(launcher.pid,signal.SIGCONT)
  if case in ['before-permit','frontend-loss-before']:
   wait(lambda:'LAUNCH_READY' in (tmp/'launcher.log').read_text())
   if case=='frontend-loss-before':os.kill(launcher.pid,signal.SIGKILL)
   else:os.killpg(launcher.pid,signal.SIGKILL)
  elif case!='initially-inactive':
   wait(lambda:'HANDOFF_COMPLETE' in (tmp/'launcher.log').read_text(),8)
   if case in ['user-ended-window','full-window-deadline']:
    wait(lambda:'PAYLOAD_RUNNING' in logs(owner))
    props=run('systemctl','show',child,'-p','RuntimeMaxUSec,LimitRTPRIO,LimitRTPRIOSoft,LimitRTTIME,LimitRTTIMESoft,CapabilityBoundingSet,NoNewPrivileges,ProtectControlGroups').stdout
    assert 'RuntimeMaxUSec=5min 2s' in props and 'LimitRTPRIO=99' in props and 'LimitRTTIME=100000' in props,props
    assert 'RuntimeMaxUSec=5min 20s' in run('systemctl','show',owner,'-p','RuntimeMaxUSec').stdout
    print('FULL_WINDOW_PROPERTIES',props,flush=True)
    time.sleep(12)
    assert show(owner).get('ActiveState')=='active' and show(child).get('ActiveState')=='active'
    assert show(stock).get('ActiveState')=='inactive' and (stockdir/'starts').read_text().count('start')==1
    print('WINDOW_SURVIVES_OLD_10_SECOND_CUTOFF',case,flush=True)
    if case=='user-ended-window':run(str(tmp/'inkbound-recovery-owner-vm'),'--request-stop',s)
   if case=='controller-kill':
    controller=int((tmp/'controller.pid').read_text());os.kill(controller,signal.SIGKILL);os.killpg(launcher.pid,signal.SIGKILL)
    assert show(owner).get('ActiveState')=='active'
   elif case=='shell-loss':os.killpg(launcher.pid,signal.SIGKILL);assert show(owner).get('ActiveState')=='active'
   elif case=='after-permit':os.killpg(launcher.pid,signal.SIGKILL);assert show(owner).get('ActiveState')=='active'
   elif case in ['term-responsive','term-ignoring']:
    wait(lambda:'PAYLOAD_RUNNING' in logs(owner));run(str(tmp/'inkbound-recovery-owner-vm'),'--request-stop',s)
   elif case=='duplicate':
    wait(lambda:'PAYLOAD_RUNNING' in logs(owner))
    run(str(tmp/'inkbound-recovery-owner-vm'),'--request-stop',s);run(str(tmp/'inkbound-recovery-owner-vm'),'--request-stop',s,check=False)
   elif case=='restore-failure':(stockdir/'fail').touch()
   elif case=='reference-loss':
    wait(lambda:'PAYLOAD_RUNNING' in logs(owner));run(str(tmp/'inkbound-recovery-owner-vm'),'--test-drop-reference',s)
   elif case=='owner-loss':
    wait(lambda:'PAYLOAD_RUNNING' in logs(owner));os.kill(int(show(owner)['MainPID']),signal.SIGKILL)
   elif case=='forged-permit':
    wait(lambda:'PAYLOAD_RUNNING' in logs(owner));run(str(tmp/'inkbound-recovery-owner-vm'),'--test-forge',s)
  launcher.wait(timeout=10);out.close();out=None
  wait(lambda:show(owner).get('ActiveState') in ('inactive','failed') or show(owner).get('LoadState')=='not-found',330 if case=='full-window-deadline' else 25)
  # Let independently enforced BindsTo/RuntimeMax finish child/witness after owner loss.
  wait(lambda:show(child).get('MainPID','0')=='0' and show(child).get('ControlPID','0')=='0',5)
  text=logs(owner);childlog=logs(child);starts=(stockdir/'starts').read_text().count('start')
  failclosed=case in ['reference-loss','owner-loss','restore-failure','initially-inactive']
  if case in ['reference-loss','owner-loss']:
   assert 'RESTORING' not in text and starts==1 and show(stock).get('ActiveState')!='active'
   if case=='reference-loss':assert 'actual_reference_connection_closed' in text
  elif case=='restore-failure':
   assert text.count('RESTORING stock_start_requests=1')==1 and starts==2 and 'MANUAL_RECOVERY_REQUIRED' in text and 'COMPLETE' not in text
  elif case=='initially-inactive':
   assert 'PAYLOAD_PERMITTED' not in text and 'STOCK_STOP_REQUESTED' not in text and starts==1
  else:
   assert 'COMPLETE' in text and not BASE.joinpath(s).exists(),(text,'cleanup')
   assert show(stock).get('ActiveState')=='active'
   if case in ['before-permit','frontend-loss-before']:assert 'PAYLOAD_PERMITTED' not in text and 'INERT_PAYLOAD_ENTERED' not in childlog and starts==1
   else:
    assert starts==2 and text.count('RESTORING stock_start_requests=1')==1
    assert text.index('DRAIN_PROVEN')<text.index('RESTORING')
    assert 'INERT_PAYLOAD_ENTERED actual_recovery_provider=1' in childlog
  if case in ['term-ignoring','controller-kill','after-permit','descendant']:assert 'status=9/KILL' in childlog or 'Killing process' in childlog or 'signal SIGKILL' in childlog,childlog
  if case=='permit-reuse':assert 'PERMIT_REUSE_REJECTED' in childlog
  results.append(dict(case=case,result='PASS_FAIL_CLOSED' if failclosed else 'PASS',harnessRestorationCalls=0,stockStarts=starts,controllerPid=controller,ownerLog=text,childLog=childlog,childFinal=show(child),ownerFinal=show(owner)))
  print('CASE',case,results[-1]['result'],'dummy_starts',starts,'harness_restore_calls=0',flush=True)
 except Exception as ex:
  print('FAIL',case,repr(ex),logs(owner),logs(child),(tmp/'launcher.log').read_text() if (tmp/'launcher.log').exists() else '',flush=True)
  (R/'evidence/real-owner-results.json').write_text(json.dumps(results,indent=2)+'\n');raise
 finally:
  if launcher and launcher.poll() is None:os.killpg(launcher.pid,signal.SIGKILL);launcher.wait()
  if out:out.close()
  for unit in [child,owner,stock]:run('systemctl','stop',unit,check=False);run('systemctl','reset-failed',unit,check=False)
  if BASE.exists():shutil.rmtree(BASE)
  shutil.rmtree(tmp)
(R/'evidence/real-owner-results.json').write_text(json.dumps(results,indent=2)+'\n')
print('REAL_OWNER_CASES_PASS',len(results),'VM',version,flush=True)
