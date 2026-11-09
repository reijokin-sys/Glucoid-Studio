#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Only the additional IPv6/TLS phenomena; no repeat of the route-drift test."""
import os,subprocess,time,json,re
from pathlib import Path
from wireguard_lab import cmd,ns,set_peer,peer_public
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'docs/artifacts/wireguard-fix-20261109';BIN=ROOT/'build/security-audit/security-audit-tests'
nodes=[];work=[];results=[]
def record(case,ok,**facts):
 r={'case':case,'pass':ok,**facts};results.append(r);print(json.dumps(r),flush=True)
def main():
 if os.readlink('/proc/self/ns/net')==os.readlink('/proc/1/ns/net'):raise RuntimeError('host namespace refused')
 cmd(['ip','link','set','lo','up']);cmd(['ip','link','add','br0','type','bridge']);cmd(['ip','link','set','br0','up'])
 for label,num in [('c',2),('s',3)]:
  p=subprocess.Popen(['unshare','--net','--','sleep','120'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);nodes.append(p);deadline=time.monotonic()+2
  while os.readlink('/proc/'+str(p.pid)+'/ns/net')==os.readlink('/proc/self/ns/net'):
   if time.monotonic()>deadline:raise RuntimeError('startup deadline')
   time.sleep(.01)
  cmd(['ip','link','add','v'+label,'type','veth','peer','name','p'+label]);cmd(['ip','link','set','p'+label,'netns',str(p.pid)]);cmd(['ip','link','set','v'+label,'master','br0']);cmd(['ip','link','set','v'+label,'up'])
  ns(p,['ip','link','set','lo','up']);ns(p,['ip','link','set','p'+label,'name','eth0']);ns(p,['ip','addr','add',f'192.0.2.{num}/24','dev','eth0']);ns(p,['ip','link','set','eth0','up']);ns(p,['ip','link','add','wg0','type','wireguard'])
  key=cmd(['wg','genkey']).stdout;ns(p,['wg','set','wg0','private-key','/dev/stdin','listen-port',str(51818+num)],key);key=None
  for addr in [f'10.77.0.{num-1}/32',f'fd80::{num-1}/128']:ns(p,['ip','addr','add',addr,'dev','wg0'])
  ns(p,['ip','link','set','wg0','up'])
 c,s=nodes;cp,sp=[peer_public(p) for p in nodes];psk=cmd(['wg','genpsk']).stdout;set_peer(c,sp,'10.77.0.2','192.0.2.3:51821',psk);set_peer(s,cp,'10.77.0.1','192.0.2.2:51820',psk);psk=None
 for p,pub,target in [(c,sp,'2'),(s,cp,'1')]:
  ns(p,['wg','set','wg0','peer',pub.decode(),'allowed-ips',f'10.77.0.{target}/32,fd80::{target}/128'])
  ns(p,['ip','route','add',f'10.77.0.{target}/32','dev','wg0']);ns(p,['ip','-6','route','add',f'fd80::{target}/128','dev','wg0'])
 # Both certificate and private key are captured in memory; only the public CA is persisted.
 pem=cmd(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-keyout','/dev/stdout','-out','/dev/stdout','-subj','/CN=synthetic-lab','-addext','subjectAltName=IP:10.77.0.2,IP:fd80::2']).stdout
 cert=re.search(b'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',pem,re.S)
 if not cert:raise RuntimeError('test certificate unavailable')
 (OUT/'public-test-ca.pem').write_bytes(cert[0]+b'\n');os.chmod(OUT/'public-test-ca.pem',0o644)
 with open(OUT/'tls-server-events.jsonl','wb') as log:
  server=subprocess.Popen(['nsenter','-t',str(s.pid),'-n','--','python3',str(ROOT/'tests/security/guard_dual_server.py')],stdin=subprocess.PIPE,stdout=log,stderr=subprocess.DEVNULL);work.append(server)
  server.stdin.write(pem);server.stdin.close();pem=None
 deadline=time.monotonic()+3
 while (OUT/'tls-server-events.jsonl').read_text().count('"ready": true')!=3:
  if time.monotonic()>deadline:raise RuntimeError('server readiness deadline')
  time.sleep(.02)
 owner=OUT.stat()
 for case,url,trust,expected in [('ipv4-tls-trusted','https://10.77.0.2:17443',True,True),('ipv4-tls-untrusted','https://10.77.0.2:17443',False,False),('ipv6-http','http://[fd80::2]:17580',False,True),('ipv6-tls-trusted','https://[fd80::2]:17443',True,True)]:
  args=['setpriv','--reuid',str(owner.st_uid),'--regid',str(owner.st_gid),'--clear-groups','--no-new-privs',str(BIN),'--probe',url,'--require-wireguard','wg0']
  if trust:args+=['--test-ca',str(OUT/'public-test-ca.pem')]
  before=sum('"request": true' in l for l in (OUT/'tls-server-events.jsonl').read_text().splitlines())
  p=ns(c,args,check=False,timeout=40)
  try:r=json.loads(p.stdout)
  except Exception:r={'ok':False,'harness_error':True}
  after=sum('"request": true' in l for l in (OUT/'tls-server-events.jsonl').read_text().splitlines())
  record(case,r.get('ok')==expected and not r.get('harness_error') and r.get('privileged') is False and (expected or after==before),response=r,application_requests=after-before)
try:main()
except Exception as e:record('harness',False,reason=str(e) if isinstance(e,RuntimeError) else type(e).__name__)
finally:
 for p in reversed(work+nodes):
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=2)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 record('cleanup',all(p.poll() is not None for p in work+nodes));(OUT/'tls-results.json').write_text(json.dumps(results,indent=2)+'\n')
 owner=OUT.stat()
 for name in ['tls-results.json','tls-server-events.jsonl','public-test-ca.pem']:
  if (OUT/name).exists():os.chown(OUT/name,owner.st_uid,owner.st_gid)
raise SystemExit(0 if all(r['pass'] for r in results) else 1)
