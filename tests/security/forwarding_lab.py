#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Single new phenomenon: forwarding isolation, with positive destination control."""
import subprocess,os,socket,threading,time,json
from wireguard_lab import cmd,ns,set_peer,peer_public,put
nodes=[];listener=None;accepted=0;stop=threading.Event()
def connect(p,address):
 return ns(p,['python3','-c','import socket; s=socket.create_connection(('+repr(address)+',19090),timeout=1); s.close()'],check=False,timeout=3).returncode==0

def main():
 global listener,accepted
 if os.readlink('/proc/self/ns/net')==os.readlink('/proc/1/ns/net'):raise RuntimeError('host namespace refused')
 cmd(['ip','link','set','lo','up']);cmd(['ip','link','add','br0','type','bridge']);cmd(['ip','addr','add','192.0.2.1/24','dev','br0']);cmd(['ip','addr','add','198.18.0.1/32','dev','br0']);cmd(['ip','link','set','br0','up'])
 for label,num in [('c',2),('s',3)]:
  p=subprocess.Popen(['unshare','--net','--','sleep','60'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);nodes.append(p);deadline=time.monotonic()+2
  while os.readlink('/proc/'+str(p.pid)+'/ns/net')==os.readlink('/proc/self/ns/net'):
   if time.monotonic()>deadline:raise RuntimeError('startup deadline')
   time.sleep(.01)
  cmd(['ip','link','add','v'+label,'type','veth','peer','name','p'+label]);cmd(['ip','link','set','p'+label,'netns',str(p.pid)]);cmd(['ip','link','set','v'+label,'master','br0']);cmd(['ip','link','set','v'+label,'up'])
  ns(p,['ip','link','set','lo','up']);ns(p,['ip','link','set','p'+label,'name','eth0']);ns(p,['ip','addr','add',f'192.0.2.{num}/24','dev','eth0']);ns(p,['ip','link','set','eth0','up']);ns(p,['ip','link','add','wg0','type','wireguard'])
  key=cmd(['wg','genkey']).stdout;ns(p,['wg','set','wg0','private-key','/dev/stdin','listen-port',str(51818+num)],key);key=None
  ns(p,['ip','addr','add',f'10.77.0.{num-1}/32','dev','wg0']);ns(p,['ip','link','set','wg0','up'])
 c,s=nodes;cp,sp=[peer_public(p) for p in nodes];psk=cmd(['wg','genpsk']).stdout
 set_peer(c,sp,'10.77.0.2','192.0.2.3:51821',psk);set_peer(s,cp,'10.77.0.1','192.0.2.2:51820',psk);psk=None
 ns(c,['wg','set','wg0','peer',sp.decode(),'allowed-ips','10.77.0.2/32,198.18.0.1/32'])
 ns(c,['ip','route','add','198.18.0.1/32','dev','wg0']);ns(s,['ip','route','add','10.77.0.1/32','dev','wg0']);ns(s,['ip','route','add','198.18.0.1/32','via','192.0.2.1','dev','eth0'])
 # Return route exists, so lack of a listener/return route is not the reason for failure.
 cmd(['ip','route','add','10.77.0.1/32','via','192.0.2.3','dev','br0'])
 ns(s,['sysctl','-q','-w','net.ipv4.ip_forward=0','net.ipv6.conf.all.forwarding=0'])
 listener=socket.socket();listener.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);listener.bind(('198.18.0.1',19090));listener.listen(2);listener.settimeout(.1)
 def accept():
  global accepted
  while not stop.is_set():
   try:sock,_=listener.accept();sock.close();accepted+=1
   except socket.timeout:continue
 t=threading.Thread(target=accept);t.start()
 control=connect(s,'198.18.0.1');blocked=not connect(c,'198.18.0.1');stop.set();t.join()
 rc=ns(s,['wg','show','wg0','latest-handshakes']).stdout.split()
 handshake=bool(rc and rc[-1]!=b'0')
 return {'id':'B05-forwarding','status':'PASS' if control and blocked and handshake and accepted==1 else 'FAIL','destination_positive_control':control,'tunnel_handshake':handshake,'forwarded_connection_blocked':blocked,'destination_accepted_connections':accepted}

try:result=main()
except Exception as e:result={'id':'B05-forwarding','status':'OPEN','reason':str(e) if isinstance(e,RuntimeError) else type(e).__name__}
finally:
 stop.set()
 if listener:listener.close()
 for p in reversed(nodes):
  p.terminate()
  try:p.wait(timeout=2)
  except subprocess.TimeoutExpired:p.kill();p.wait()
 result['owned_namespace_processes_exited']=all(p.poll() is not None for p in nodes)
 put('forwarding-results.json',json.dumps(result,indent=2).encode()+b'\n');print(json.dumps(result))
