#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare an allowlisted public source tree; never uploads or publishes."""
import hashlib
import json
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
version = re.search(r'project\(GlucoidStudio VERSION ([0-9.]+)', (root/'CMakeLists.txt').read_text()).group(1)
out = root/'dist'/f'github-{version}'
source = out/'source'
if source.exists():
    raise SystemExit('Source tree already exists; retain it and choose a new release version or inspect before replacing.')
source.mkdir(parents=True)
selected = {name: root/name for name in ('CMakeLists.txt', 'LICENSE', '.clineignore', '.clang-format')}
selected.update({'README.md':root/'github/README.md', '.gitignore':root/'github/.gitignore',
                 'THIRD_PARTY_NOTICES.md':root/'packaging/THIRD_PARTY_NOTICES.md'})
application_files = {
    'src': (
        'backend.cpp backend.h databasebackup.cpp databasebackup.h dailyanalysis.cpp dailyanalysis.h daychart.cpp daychart.h '
        'domain.cpp domain.h glucose.cpp glucose.h glucoseanalysis.cpp glucoseanalysis.h '
        'importer.cpp importer.h language.cpp language.h main.cpp nightscout.cpp nightscout.h '
        'nightscoutstore.cpp observation.cpp observation.h store.cpp store.h syncpolicy.h '
        'transport.cpp transport.h transportcore.cpp wireguard.cpp wireguard.h'
    ).split(),
    'qml': (
        'chartsegments.js theme.js Main.qml DailyAnalysis.qml ConnectionEditor.qml '
        'LanguageSelector.qml GlucoseChart.qml CurrentDayChart.qml ProfileChart.qml '
        'Disclosure.qml MetricCard.qml Legend.qml'
    ).split(),
    'i18n': ['studio_en.ts', 'studio_fi.ts'],
}
for directory, names in application_files.items():
    for name in names:
        selected[directory+'/'+name] = root/directory/name
for directory, suffixes in [('tests',{'.cpp','.h','.py','.tsv'}),('packaging',{'.desktop','.md'})]:
    for p in sorted((root/directory).rglob('*')):
        if p.is_file() and p.suffix in suffixes and '__pycache__' not in p.parts:
            selected[str(p.relative_to(root))] = p
for p in sorted((root/'packaging/licenses').glob('*')):
    if p.is_file(): selected[str(p.relative_to(root))] = p
for name in ('fi.reijo.glucoidstudio.svg','fi.reijo.glucoidstudio.png'):
    selected['packaging/'+name] = root/'packaging'/name
selected['tests/security/README.md'] = root/'github/SECURITY_TESTING.md'
for name in ('build.sh','install.py','qtpaths','restore-backup.py','prepare-release.py','build-release-package.py'):
    selected['tools/'+name] = root/'tools'/name
# Public source keeps the generator inputs too, so packaging is reproducible.
for name in ('README.md','.gitignore','SECURITY_TESTING.md'):
    selected['github/'+name] = root/'github'/name
for name, p in sorted(selected.items()):
    if p.is_symlink(): raise SystemExit('Symlinks are not part of the public source allowlist.')
    dest=source/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
manifest={name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in sorted(selected)}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
epoch=int(subprocess.check_output(['git','show','-s','--format=%ct','HEAD'],cwd=root,text=True).strip())
archive=out/f'glucoid-studio-{version}-source.tar.xz'
with tarfile.open(archive,'w:xz') as tar:
    for name in sorted(manifest):
        p=source/name;info=tar.gettarinfo(str(p),f'glucoid-studio-{version}/'+name)
        info.uid=info.gid=0;info.uname=info.gname='root';info.mtime=epoch
        info.mode=0o755 if p.stat().st_mode&0o111 else 0o644
        with p.open('rb') as f: tar.addfile(info,f)
print(f'Prepared {len(manifest)} allowlisted source files for {version}. No Git history or private documents included.')
