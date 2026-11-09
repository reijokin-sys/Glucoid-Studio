#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a Debian package from an inspected public source tree; no publication."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True,type=Path)
parser.add_argument('--build',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
args=parser.parse_args()
source=args.source.resolve();build=args.build.resolve();out=args.output.resolve()
version=re.search(r'project\(GlucoidStudio VERSION ([0-9.]+)',(source/'CMakeLists.txt').read_text()).group(1)
out.mkdir(parents=True,exist_ok=True)
def run(*cmd,**kw):return subprocess.run(list(map(str,cmd)),check=True,**kw)
run('cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX=/usr','-DBUILD_TESTING=ON','-DSTUDIO_LOCAL_DATA_ROOT=')
run('cmake','--build',build,'--parallel','4')
stage=out/'package-root'
if stage.exists():raise SystemExit('Package staging directory already exists; do not overwrite release artifacts.')
run('cmake','--install',build,env=dict(os.environ,DESTDIR=str(stage)))
binary=stage/'usr/bin/glucoid-studio'
run('strip','--strip-unneeded',binary)
if b'/home/' in binary.read_bytes() or b'/private/live' in binary.read_bytes():
    raise SystemExit('Private build/default data path detected in release binary.')
work=build/'dependency-check';(work/'debian').mkdir(parents=True,exist_ok=True)
(work/'debian/control').write_text('Source: glucoid-studio\nSection: science\nPriority: optional\nMaintainer: Glucoid Studio contributors\n\nPackage: glucoid-studio\nArchitecture: amd64\nDescription: Local glucose history viewer\n')
deps=run('dpkg-shlibdeps','-O',f'-e{binary}',cwd=work,capture_output=True,text=True).stdout.strip().removeprefix('shlibs:Depends=')
qml=['qml6-module-qtquick','qml6-module-qtquick-controls','qml6-module-qtquick-layouts','qml6-module-qtquick-window','qml6-module-qtquick-templates','qml6-module-qtquick-shapes','qml6-module-qtqml','qml6-module-qtqml-models','qml6-module-qtqml-workerscript','qml6-module-qtgraphs','qml6-module-org-kde-kirigami','qt6-qpa-plugins','kwallet6','tzdata']
deps+=', '+', '.join(qml)
arch=run('dpkg','--print-architecture',capture_output=True,text=True).stdout.strip()
if arch!='amd64':raise SystemExit('This release target is Ubuntu/Kubuntu 26.04 amd64.')
control=stage/'DEBIAN';control.mkdir()
size=sum(p.stat().st_size for p in stage.rglob('*') if p.is_file())//1024+1
(control/'control').write_text(f'Package: glucoid-studio\nVersion: {version}\nArchitecture: {arch}\nMaintainer: Glucoid Studio contributors\nSection: science\nPriority: optional\nInstalled-Size: {size}\nDepends: {deps}\nSuggests: adb\nDescription: Local Juggluco and Nightscout glucose history viewer\n Finnish desktop glucose summaries, daily analysis and charts.\n')
for p in stage.rglob('*'):p.chmod(0o755 if p.is_dir() or p==binary else 0o644)
package=out/f'glucoid-studio_{version}_{arch}.deb'
run('dpkg-deb','--root-owner-group','--build',stage,package)
(out/'package.json').write_text(json.dumps({'version':version,'architecture':arch,'target':'Ubuntu/Kubuntu 26.04 amd64','dependencies':deps,'bytes':package.stat().st_size,'sha256':hashlib.sha256(package.read_bytes()).hexdigest()},indent=2)+'\n')
print(package.name)
