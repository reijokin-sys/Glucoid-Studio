#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install the local desktop launcher and logged-in user collector. Never removes data."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import shlex
import subprocess
import time

project = Path(__file__).resolve().parent.parent
home = Path.home()
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, default=project / 'build', help='Build directory containing the application binary')
parser.add_argument('--enable-collector', action='store_true', help='Explicitly enable the optional logged-in background collector')
parser.add_argument('--remove', action='store_true', help='Disable/remove integration; preserve all health data and builds')
parser.add_argument('--desktop-only', action='store_true', help='Create only the desktop shortcut for the already installed application')
args = parser.parse_args()
build_binary = args.build_dir.resolve() / 'glucoid-studio'
binary = project / 'runtime/glucoid-studio'
wrapper = home / '.local/bin/glucoid-studio'
desktop = home / '.local/share/applications/fi.reijo.glucoidstudio.desktop'
unit = home / '.config/systemd/user/glucoid-studio.service'
icons = {kind: home / ('.local/share/icons/hicolor/'+folder+'/apps/fi.reijo.glucoidstudio.'+kind) for kind, folder in [('svg','scalable'),('png','256x256')]}
artifact = project / 'docs/artifacts/installation'
artifact.mkdir(parents=True, exist_ok=True)
marker = '# Managed by Glucoid Studio local installer'
manifest = artifact / 'installed.json'
previous = json.loads(manifest.read_text()) if manifest.exists() else {}
desktop_dir = Path(subprocess.run(['xdg-user-dir', 'DESKTOP'], capture_output=True, text=True, check=True).stdout.strip())
if not desktop_dir.is_absolute():
    raise SystemExit('Desktop directory could not be resolved.')
shortcut = desktop_dir / 'fi.reijo.glucoidstudio.desktop'
mime_env = os.environ.copy()
mime_env['PATH'] = str(project / 'tools') + os.pathsep + mime_env.get('PATH', '')

def systemctl(*parts):
    subprocess.run(['systemctl', '--user', *parts], check=True)

def preserve(path):
    if path.exists():
        content = path.read_text()
        if marker not in content:
            raise SystemExit('Refusing to replace an unmanaged integration file: ' + str(path))
        digest = hashlib.sha256(content.encode()).hexdigest()[:16]
        target = artifact / (path.name + '.' + digest + '.previous')
        if not target.exists():
            target.write_text(content)

def preserve_icon(kind, path):
    if not path.exists():
        return
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    source = project / ('packaging/fi.reijo.glucoidstudio.' + kind)
    known = previous.get('files', {}).get('icon_' + kind)
    if digest != known and path.read_bytes() != source.read_bytes():
        raise SystemExit('Refusing to replace an unmanaged Studio icon.')
    target = artifact / (path.name + '.' + digest[:16] + '.previous')
    if not target.exists():
        shutil.copy2(path, target)

if args.desktop_only:
    if args.remove:
        raise SystemExit('--desktop-only and --remove cannot be combined.')
    if not wrapper.is_file() or not os.access(wrapper, os.X_OK) or not binary.is_file() or not desktop.is_file():
        raise SystemExit('Install the application before creating its desktop shortcut.')
    preserve(shortcut)
    desktop_dir.mkdir(parents=True, exist_ok=True)
    content = desktop.read_text()
    if marker not in content:
        raise SystemExit('The installed application launcher is not managed by Glucoid Studio.')
    temp = shortcut.with_name(shortcut.name + '.new')
    temp.write_text(content)
    temp.chmod(0o755)
    temp.replace(shortcut)
    previous['desktop_shortcut'] = {'path': str(shortcut), 'sha256': hashlib.sha256(shortcut.read_bytes()).hexdigest()}
    manifest.write_text(json.dumps(previous, indent=2) + '\n')
    print('Desktop shortcut created: ' + str(shortcut))
elif args.remove:
    for kind, path in icons.items():
        preserve_icon(kind, path)
    systemctl('disable', '--now', 'glucoid-studio.service')
    for path in [wrapper, desktop, unit, shortcut]:
        preserve(path)
        if path.exists():
            path.unlink()
    for path in icons.values():
        if path.exists():
            path.unlink()
    systemctl('daemon-reload')
    handler = previous.get('previous_uri_handler', '')
    if handler and handler != desktop.name:
        subprocess.run(['xdg-mime', 'default', handler, 'x-scheme-handler/glucoid-studio'], check=True, env=mime_env)
    elif not handler:
        for path in [home / '.config/mimeapps.list', home / '.local/share/applications/mimeapps.list']:
            if path.exists():
                lines = path.read_text().splitlines(keepends=True)
                content = ''.join(line for line in lines if not line.startswith('x-scheme-handler/glucoid-studio='))
                if content != ''.join(lines):
                    path.write_text(content)
    print('Desktop integration removed; history, credentials, backups and source retained.')
else:
    if not build_binary.exists():
        raise SystemExit('Build the application first with tools/build.sh')
    for kind, path in icons.items():
        preserve_icon(kind, path)
    binary.parent.mkdir(parents=True, exist_ok=True)
    if binary.exists() and binary.read_bytes()!=build_binary.read_bytes():
        digest=hashlib.sha256(binary.read_bytes()).hexdigest()
        history=binary.parent/'history'/digest
        history.mkdir(parents=True,exist_ok=True)
        if not (history/binary.name).exists():
            shutil.copy2(binary,history/binary.name)
    temp_binary=binary.with_name(binary.name+'.new')
    shutil.copy2(build_binary,temp_binary)
    temp_binary.chmod(0o755)
    temp_binary.replace(binary)
    queried_handler = subprocess.run(['xdg-mime', 'query', 'default', 'x-scheme-handler/glucoid-studio'], capture_output=True, text=True, check=True, env=mime_env).stdout.strip()
    previous_handler = previous.get('previous_uri_handler', queried_handler if queried_handler != desktop.name else '')
    quoted_binary = shlex.quote(str(binary))
    wrapper_text = '#!/bin/sh\n' + marker + '\nexec ' + quoted_binary + ' "$@"\n'
    desktop_text = (project / 'packaging/fi.reijo.glucoidstudio.desktop').read_text()
    desktop_text = marker + '\n' + desktop_text.replace('Exec=glucoid-studio %u', 'Exec="' + str(wrapper).replace('"', '\\"') + '" %u')
    # systemd escaping differs from shell escaping; use a quoted argument and literal %%.
    systemd_binary = '"' + str(binary).replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%') + '"'
    unit_text = marker + '\n' + '''[Unit]
Description=Glucoid Studio — local glucose history collector
PartOf=graphical-session.target
After=graphical-session.target

[Service]
Type=simple
ExecStart=''' + systemd_binary + ''' --collect
Restart=on-failure
RestartSec=15
UMask=0077
NoNewPrivileges=true

[Install]
WantedBy=graphical-session.target
'''
    for path, content in [(wrapper, wrapper_text), (desktop, desktop_text), (unit, unit_text)]:
        preserve(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name(path.name + '.new')
        temp.write_text(content)
        temp.chmod(0o755 if path == wrapper else 0o644)
        temp.replace(path)
    for kind, path in icons.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name(path.name + '.new')
        shutil.copyfile(project / ('packaging/fi.reijo.glucoidstudio.' + kind), temp)
        temp.chmod(0o644)
        temp.replace(path)
    if previous.get('desktop_shortcut') and shortcut.exists():
        preserve(shortcut)
        temp = shortcut.with_name(shortcut.name + '.new')
        temp.write_text(desktop_text)
        temp.chmod(0o755)
        temp.replace(shortcut)
        previous['desktop_shortcut'] = {'path': str(shortcut), 'sha256': hashlib.sha256(shortcut.read_bytes()).hexdigest()}
    systemctl('daemon-reload')
    if args.enable_collector:
        systemctl('enable', '--now', 'glucoid-studio.service')
        systemctl('restart', 'glucoid-studio.service')
    else:
        systemctl('disable', '--now', 'glucoid-studio.service')
    subprocess.run(['xdg-mime', 'default', desktop.name, 'x-scheme-handler/glucoid-studio'], check=True, env=mime_env)
    confirmed_handler = subprocess.run(['xdg-mime', 'query', 'default', 'x-scheme-handler/glucoid-studio'], capture_output=True, text=True, check=True, env=mime_env).stdout.strip()
    if confirmed_handler != desktop.name:
        raise SystemExit('URI registration did not take effect; integration files and data preserved.')
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in [('launcher', wrapper), ('desktop', desktop), ('service', unit), ('binary', binary)]}
    hashes.update({'icon_' + kind: hashlib.sha256(path.read_bytes()).hexdigest() for kind, path in icons.items()})
    report = {'time': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'files': hashes, 'previous_uri_handler': previous_handler, 'data_preserved_on_remove': True, 'collector_mode': 'enabled' if args.enable_collector else 'manual', 'runtime_binary': str(binary)}
    if previous.get('desktop_shortcut'):
        report['desktop_shortcut'] = previous['desktop_shortcut']
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    print('Desktop launcher and URI handler installed; background collection is optional.')
