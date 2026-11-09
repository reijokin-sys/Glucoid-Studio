#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Must be launched with unshare --net; modifies only owned lab namespaces.
Private keys/PSKs are generated in memory, passed by stdin and never logged.
"""
import subprocess,os,sys,json,socket,threading,time,hashlib,signal
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'docs/artifacts/security-simulation-20261109'
BINARY=ROOT/'build/security-audit/security-audit-tests'
results=[];children=[];workloads=[];relay=None;server=None
start=time.monotonic()

def cmd(args,data=None,check=True,timeout=15):
 p=subprocess.run(args,input=data,capture_output=True,timeout=timeout)
 if check and p.returncode:raise RuntimeError('lab command failed: '+args[0]+' (output suppressed)')
 return p

def ns(proc,args,data=None,check=True,timeout=15):return cmd(['nsenter','-t',str(proc.pid),'-n','--']+args,data,check,timeout)
def record(id,status,**facts):
 row={'id':id,'status':status,**facts};results.append(row);print(json.dumps(row),flush=True)
def put(name,data):
 path=OUT/name;path.write_bytes(data);owner=OUT.stat();os.chown(path,owner.st_uid,owner.st_gid);os.chmod(path,0o640)
def rx(proc):
 o=json.loads(ns(proc,['ip','-j','-s','link','show','wg0']).stdout)
 return o[0]['stats64']['rx']['packets']
def peer_public(proc):return ns(proc,['wg','show','wg0','public-key']).stdout.strip()
def set_peer(proc,pub,ip,endpoint=None,psk=None):
 args=['wg','set','wg0','peer',pub.decode(),'allowed-ips',ip+'/32']
 if endpoint:args+=['endpoint',endpoint]
 if psk:args+=['preshared-key','/dev/stdin']
 ns(proc,args,psk)
def probe(proc,url):
 p=ns(proc,[str(BINARY),'--probe',url],check=False,timeout=45)
 try:return json.loads(p.stdout)
 except Exception:return {'ok':False,'harness_error':True,'exit':p.returncode}
def connect_port(proc,ip,port):
 code='import socket; s=socket.create_connection(('+repr(ip)+','+str(port)+'),timeout=1); s.close()'
 return ns(proc,['python3','-c',code],check=False,timeout=4).returncode==0
class Relay:
 def __init__(self):
  self.s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.s.bind(('192.0.2.1',62000));self.s.settimeout(.1)
  self.stop=False;self.hold=False;self.held=None;self.event=threading.Event();self.payloads=[];self.captured=0
  self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
 def run(self):
  while not self.stop:
   try:data,addr=self.s.recvfrom(65535)
   except socket.timeout:continue
   except OSError:break
   self.captured+=1;self.payloads.append(data)
   if addr[0]=='192.0.2.2':
    if self.hold and data[:4]==b'\x04\0\0\0' and len(data)>32:
     self.hold=False;self.held=data;self.event.set();continue
    self.s.sendto(data,('192.0.2.3',51821))
   elif addr[0]=='192.0.2.3':self.s.sendto(data,('192.0.2.2',51820))
 def close(self):self.stop=True;self.thread.join(timeout=1);self.s.close()

def main():
 global relay,server
 # Refuse running in the host's network namespace. The parent is pkexec/unshare.
 if os.readlink('/proc/self/ns/net')==os.readlink('/proc/1/ns/net'):raise RuntimeError('host namespace refused')
 cmd(['ip','link','set','lo','up']);cmd(['ip','link','add','bridge0','type','bridge']);cmd(['ip','addr','add','192.0.2.1/24','dev','bridge0']);cmd(['ip','link','set','bridge0','up'])
 for label,num in [('client',2),('source',3),('outsider',4)]:
  p=subprocess.Popen(['unshare','--net','--','sleep','580'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(p)
  # One bounded wait for namespace creation, no network measurement repeated.
  deadline=time.monotonic()+2
  while os.readlink('/proc/'+str(p.pid)+'/ns/net')==os.readlink('/proc/self/ns/net'):
   if time.monotonic()>deadline:raise RuntimeError('namespace startup deadline')
   time.sleep(.01)
  cmd(['ip','link','add','v'+label,'type','veth','peer','name','p'+label]);cmd(['ip','link','set','p'+label,'netns',str(p.pid)])
  cmd(['ip','link','set','v'+label,'master','bridge0']);cmd(['ip','link','set','v'+label,'up'])
  ns(p,['ip','link','set','lo','up']);ns(p,['ip','link','set','p'+label,'name','eth0']);ns(p,['ip','addr','add',f'192.0.2.{num}/24','dev','eth0']);ns(p,['ip','link','set','eth0','up'])
  ns(p,['ip','link','add','wg0','type','wireguard']);key=cmd(['wg','genkey']).stdout
  ns(p,['wg','set','wg0','private-key','/dev/stdin','listen-port',str(51818+num)],key);key=None
  ns(p,['ip','addr','add',f'10.77.0.{num-1}/32','dev','wg0']);ns(p,['ip','link','set','wg0','up'])
 c,s,b=children;cp,sp,bp=[peer_public(x) for x in children];psk=cmd(['wg','genpsk']).stdout
 set_peer(c,sp,'10.77.0.2','192.0.2.1:62000',psk);set_peer(s,cp,'10.77.0.1',psk=psk)
 # Outsider knows the server public key, but is not an authorized server peer.
 set_peer(b,sp,'10.77.0.2','192.0.2.3:51821')
 for p in [c,b]:ns(p,['ip','route','add','10.77.0.2/32','dev','wg0'])
 ns(s,['ip','route','add','10.77.0.1/32','dev','wg0']);relay=Relay()
 log=open(OUT/'lab-server-events.jsonl','wb');server=subprocess.Popen(['nsenter','-t',str(s.pid),'-n','--','python3',str(ROOT/'tests/security/lab_server.py')],stdout=log,stderr=subprocess.DEVNULL);log.close()
 # Bounded service readiness through stdout, no trial TCP requests.
 deadline=time.monotonic()+3
 while True:
  text=(OUT/'lab-server-events.jsonl').read_text()
  if text.count('ready_port')==2:break
  if time.monotonic()>deadline:raise RuntimeError('server startup deadline')
  time.sleep(.02)
 normal=probe(c,'http://10.77.0.2:17580');record('B01-wireguard-transport','PASS' if normal.get('ok') else 'FAIL',response=normal)
 # Hold a fresh encrypted client data packet. Tamper first, then deliver original control.
 relay.event.clear();relay.hold=True
 pending=subprocess.Popen(['nsenter','-t',str(c.pid),'-n','--',str(BINARY),'--probe','http://10.77.0.2:17580'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 workloads.append(pending)
 if not relay.event.wait(5):pending.kill();pending.communicate();raise RuntimeError('ciphertext interception deadline')
 packet=relay.held;before=rx(s);modified=bytearray(packet);modified[-1]^=1;relay.s.sendto(modified,('192.0.2.3',51821));time.sleep(.3);after_bad=rx(s)
 relay.s.sendto(packet,('192.0.2.3',51821));stdout,_=pending.communicate(timeout=10);control=json.loads(stdout)
 before_replay=rx(s);relay.s.sendto(packet,('192.0.2.3',51821));time.sleep(.3);after_replay=rx(s)
 marker=b'LAB_WG_KNOWN_PLAINTEXT_20261109';credential_hash=hashlib.sha1(b'LAB_ONLY_NOT_A_REAL_CREDENTIAL').hexdigest().encode()
 found=any(marker in p or credential_hash in p for p in relay.payloads)
 record('B04-encryption-tamper-replay','PASS' if before==after_bad and before_replay==after_replay and control.get('ok') and not found else 'FAIL',mutated_decrypted_packets=after_bad-before,replayed_decrypted_packets=after_replay-before_replay,original_control_ok=control.get('ok'),known_plaintext_in_observed_outer_payloads=found,observed_outer_packets=len(relay.payloads))
 put('sample-encrypted-datagram.bin',packet)
 # Missing authorization is an expected negative control. Finite connect timeout.
 before=rx(s);outside=connect_port(b,'10.77.0.2',17580);after=rx(s)
 record('B03-unauthorized-peer','PASS' if not outside and before==after else 'FAIL',connected=outside,decrypted_packets=after-before)
 # Wrong PSK on a distinct authorized lab peer; does not modify working peer keys.
 wrong=cmd(['wg','genpsk']).stdout;set_peer(s,bp,'10.77.0.3',psk=psk);set_peer(b,sp,'10.77.0.2','192.0.2.3:51821',wrong);wrong=None
 before=rx(s);wrong_ok=connect_port(b,'10.77.0.2',17580);after=rx(s)
 record('B03-wrong-psk','PASS' if not wrong_ok and before==after else 'FAIL',connected=wrong_ok,decrypted_packets=after-before)
 # Compare permitted peer's broad baseline and a candidate least-privilege policy in the lab only.
 broad=connect_port(c,'10.77.0.2',18080);record('B05-broad-peer-baseline','EXPECTED-LIMIT' if broad else 'OPEN',extra_service_reachable=broad)
 ns(c,['nft','add','table','inet','lab']);ns(c,['nft','add','chain','inet','lab','out','{ type filter hook output priority 0; policy accept; }'])
 ns(c,['nft','add','rule','inet','lab','out','oifname','wg0','ip','daddr','10.77.0.2','tcp','dport','17580','accept'])
 ns(c,['nft','add','rule','inet','lab','out','oifname','wg0','reject'])
 restricted=connect_port(c,'10.77.0.2',18080);allowed=connect_port(c,'10.77.0.2',17580)
 record('B05-restricted-peer','PASS' if not restricted and allowed else 'FAIL',extra_service_reachable=restricted,glucose_port_reachable=allowed)
 # Explicit no-forwarding policy for both lab endpoints, independent of defaults.
 for p in [c,s]:
  ns(p,['sysctl','-q','-w','net.ipv4.ip_forward=0','net.ipv6.conf.all.forwarding=0'])
 # B09: remove tunnel path, provide the same IP through the separate lab underlay route.
 # This is a controlled representation of route/configuration drift, not a claim that
 # every tunnel outage creates a fallback route in production.
 ns(c,['nft','delete','table','inet','lab']);ns(c,['ip','route','del','10.77.0.2/32','dev','wg0']);ns(c,['ip','link','set','wg0','down'])
 ns(s,['ip','addr','del','10.77.0.2/32','dev','wg0']);ns(s,['ip','addr','add','10.77.0.2/32','dev','eth0'])
 ns(c,['ip','route','add','10.77.0.2/32','via','192.0.2.3','dev','eth0'])
 fallback=probe(c,'http://10.77.0.2:17580')
 record('B09-unprotected-alternate-route','FAIL' if fallback.get('ok') else 'PASS',application_request_succeeded_without_vpn=fallback.get('ok'),scope='controlled route drift, not automatic outage behavior')
 psk=None

def cleanup():
 global relay
 if relay:relay.close()
 for p in workloads:
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=2)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 if server:
  server.terminate()
  try:server.wait(timeout=3)
  except subprocess.TimeoutExpired:server.kill();server.wait()
 for p in reversed(children):
  p.terminate()
  try:p.wait(timeout=2)
  except subprocess.TimeoutExpired:p.kill();p.wait()
 owner=OUT.stat()
 for name in ['lab-server-events.jsonl']:
  p=OUT/name
  if p.exists():os.chown(p,owner.st_uid,owner.st_gid);os.chmod(p,0o640)

if __name__=='__main__':
 try:main()
 except Exception as e:record('harness','OPEN',reason=str(e) if isinstance(e,RuntimeError) else type(e).__name__)
 finally:
  cleanup();record('cleanup','PASS',all_owned_namespace_processes_exited=all(p.poll() is not None for p in children),elapsed_seconds=round(time.monotonic()-start,2));put('wireguard-results.json',json.dumps(results,indent=2).encode()+b'\n')
 sys.exit(2 if any(r['status']=='OPEN' for r in results) else 0)
