#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guarded transport regression, including alternate-route negative controls.
All addresses/credentials belong only to this isolated synthetic laboratory.
"""
import os,subprocess,time,json
from pathlib import Path
from wireguard_lab import cmd,ns,set_peer,peer_public
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'docs/artifacts/wireguard-fix-20261109'
BIN=ROOT/'build/security-audit/security-audit-tests'
nodes=[];workloads=[];rows=[]
def result(name,ok,**facts):
 r={'case':name,'pass':ok,**facts};rows.append(r);print(json.dumps(r),flush=True)
def probe(c,guard=True,extra=()):
 owner=OUT.stat()
 args=['setpriv','--reuid',str(owner.st_uid),'--regid',str(owner.st_gid),'--clear-groups','--no-new-privs',str(BIN),'--probe','http://10.77.0.2:17580']
 if guard:args+=['--require-wireguard','wg0']
 args+=list(extra)
 p=ns(c,args,check=False,timeout=45)
 try:return json.loads(p.stdout)
 except Exception:return {'ok':False,'harness_error':True,'rc':p.returncode}
def request_count():
 return sum(1 for l in (OUT/'server-events.jsonl').read_text().splitlines() if json.loads(l).get('request'))
def main():
 if os.readlink('/proc/self/ns/net')==os.readlink('/proc/1/ns/net'):raise RuntimeError('host namespace refused')
 cmd(['ip','link','set','lo','up']);cmd(['ip','link','add','br0','type','bridge']);cmd(['ip','link','set','br0','up'])
 for label,num in [('c',2),('s',3)]:
  p=subprocess.Popen(['unshare','--net','--','sleep','180'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);nodes.append(p);deadline=time.monotonic()+2
  while os.readlink('/proc/'+str(p.pid)+'/ns/net')==os.readlink('/proc/self/ns/net'):
   if time.monotonic()>deadline:raise RuntimeError('startup deadline')
   time.sleep(.01)
  cmd(['ip','link','add','v'+label,'type','veth','peer','name','p'+label]);cmd(['ip','link','set','p'+label,'netns',str(p.pid)]);cmd(['ip','link','set','v'+label,'master','br0']);cmd(['ip','link','set','v'+label,'up'])
  ns(p,['ip','link','set','lo','up']);ns(p,['ip','link','set','p'+label,'name','eth0']);ns(p,['ip','addr','add',f'192.0.2.{num}/24','dev','eth0']);ns(p,['ip','link','set','eth0','up']);ns(p,['ip','link','add','wg0','type','wireguard'])
  key=cmd(['wg','genkey']).stdout;ns(p,['wg','set','wg0','private-key','/dev/stdin','listen-port',str(51818+num)],key);key=None
  ns(p,['ip','addr','add',f'10.77.0.{num-1}/32','dev','wg0']);ns(p,['ip','link','set','wg0','up'])
 c,s=nodes;cp,sp=[peer_public(p) for p in nodes];psk=cmd(['wg','genpsk']).stdout;set_peer(c,sp,'10.77.0.2','192.0.2.3:51821',psk);set_peer(s,cp,'10.77.0.1','192.0.2.2:51820',psk);psk=None
 ns(c,['ip','route','add','10.77.0.2/32','dev','wg0']);ns(s,['ip','route','add','10.77.0.1/32','dev','wg0'])
 with open(OUT/'server-events.jsonl','wb') as log:
  server=subprocess.Popen(['nsenter','-t',str(s.pid),'-n','--','python3',str(ROOT/'tests/security/lab_server.py')],stdout=log,stderr=subprocess.DEVNULL);workloads.append(server)
 deadline=time.monotonic()+3
 while (OUT/'server-events.jsonl').read_text().count('ready_port')!=2:
  if time.monotonic()>deadline:raise RuntimeError('server deadline')
  time.sleep(.02)
 r=probe(c,extra=['--test-proxy']);result('active-wireguard-unprivileged-no-proxy',r.get('ok',False) and r.get('privileged') is False,response=r)
 # Keep the genuine WG device up, but replace its destination route by an underlay route.
 ns(c,['ip','route','del','10.77.0.2/32','dev','wg0']);ns(s,['ip','addr','del','10.77.0.2/32','dev','wg0']);ns(s,['ip','addr','add','10.77.0.2/32','dev','eth0']);ns(c,['ip','route','add','10.77.0.2/32','via','192.0.2.3','dev','eth0'])
 baseline=probe(c,guard=False);result('alternate-route-positive-control',baseline.get('ok',False),response=baseline)
 before=request_count();r=probe(c);after=request_count();result('device-up-alternate-route-blocked',not r.get('ok') and after==before and not r.get('harness_error'),response=r,requests_received=after-before)
 ns(c,['ip','link','set','wg0','down']);before=request_count();r=probe(c);result('device-down-blocked',not r.get('ok') and request_count()==before and r.get('elapsed_ms',9999)<1000,response=r)
 ns(c,['ip','link','del','wg0']);before=request_count();r=probe(c);result('device-missing-blocked',not r.get('ok') and request_count()==before and r.get('elapsed_ms',9999)<1000,response=r)
 ns(c,['ip','link','add','wg0','type','dummy']);ns(c,['ip','link','set','wg0','up']);before=request_count();r=probe(c);result('same-name-non-wireguard-blocked',not r.get('ok') and request_count()==before and r.get('elapsed_ms',9999)<1000,response=r)
try:main()
except Exception as e:result('harness',False,reason=str(e) if isinstance(e,RuntimeError) else type(e).__name__)
finally:
 for p in reversed(workloads+nodes):
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=2)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 result('cleanup',all(p.poll() is not None for p in workloads+nodes))
 text=json.dumps(rows,indent=2)+'\n';(OUT/'guard-results.json').write_text(text)
 owner=OUT.stat()
 for name in ['guard-results.json','server-events.jsonl']:
  os.chown(OUT/name,owner.st_uid,owner.st_gid);os.chmod(OUT/name,0o640)
raise SystemExit(0 if all(r['pass'] for r in rows) else 1)
