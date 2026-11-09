#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic IPv4/IPv6 HTTP and TLS server; ephemeral key stays in a memfd."""
import os,sys,socket,ssl,threading,signal,json
stop=threading.Event();context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
fd=os.memfd_create('synthetic-tls-key',os.MFD_CLOEXEC)
os.write(fd,sys.stdin.buffer.read());context.load_cert_chain('/proc/self/fd/'+str(fd));os.close(fd)
def emit(x):print(json.dumps(x),flush=True)
def serve(family,port,tls):
 s=socket.socket(family);s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
 if family==socket.AF_INET6:s.setsockopt(socket.IPPROTO_IPV6,socket.IPV6_V6ONLY,1)
 s.bind(('::' if family==socket.AF_INET6 else '0.0.0.0',port));s.listen(4);s.settimeout(.1);emit({'ready':True,'port':port,'tls':tls,'ipv6':family==socket.AF_INET6})
 while not stop.is_set():
  try:c,_=s.accept()
  except socket.timeout:continue
  c.settimeout(2)
  try:
   if tls:c=context.wrap_socket(c,server_side=True)
   with c:
    b=b''
    while b'\r\n\r\n' not in b and len(b)<8192:
     piece=c.recv(4096)
     if not piece:break
     b+=piece
    if b:
     emit({'request':True,'tls':tls,'ipv6':family==socket.AF_INET6})
     c.sendall(b'HTTP/1.0 200 OK\r\nContent-Length: 3\r\n\r\nLAB')
  except (OSError,ssl.SSLError):c.close()
 s.close()
for args in [(socket.AF_INET,17443,True),(socket.AF_INET6,17580,False),(socket.AF_INET6,17443,True)]:threading.Thread(target=serve,args=args,daemon=True).start()
signal.signal(signal.SIGTERM,lambda *_:stop.set())
while not stop.wait(.1):pass
