#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic HTTP and harmless secondary listener inside a lab namespace."""
import socket,threading,json,hashlib,signal,sys
MARKER=b'LAB_WG_KNOWN_PLAINTEXT_20261109'
BODY=b'Sensorid\tnr\tUnixTime\tmmol/L\n'+MARKER+b'\t1\t1760000100\t6\n'
AUTH=hashlib.sha1(b'LAB_ONLY_NOT_A_REAL_CREDENTIAL').hexdigest().encode()
stop=threading.Event()
def emit(o):print(json.dumps(o),flush=True)
def serve(port):
 s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('0.0.0.0',port));s.listen(8);s.settimeout(.2)
 emit({'ready_port':port})
 while not stop.is_set():
  try:c,_=s.accept()
  except socket.timeout:continue
  c.settimeout(2)
  with c:
   try:
    data=b''
    while b'\r\n\r\n' not in data and len(data)<8192:
     chunk=c.recv(4096)
     if not chunk:break
     data+=chunk
    if not data:continue
    via_tunnel=c.getsockname()[0].startswith('10.77.')
    authenticated=b'api-secret: '+AUTH+b'\r\n' in data
    emit({'request':True,'destination_is_test_service_ip':via_tunnel,'authenticated_test_marker':authenticated,'port':port})
    body=BODY if authenticated and port==17580 else b''
    status=b'200 OK' if authenticated else b'403 Forbidden'
    c.sendall(b'HTTP/1.0 '+status+b'\r\nContent-Length: '+str(len(body)).encode()+b'\r\nConnection: close\r\n\r\n'+body)
   except (OSError,TimeoutError):pass
 s.close()
for port in [17580,18080]:threading.Thread(target=serve,args=(port,),daemon=True).start()
signal.signal(signal.SIGTERM,lambda *_:stop.set())
while not stop.wait(.2):pass
