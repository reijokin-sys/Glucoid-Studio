#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Restore a verified Studio backup into a NEW directory; never overwrite history."""
import argparse, hashlib, json, os, shutil, sqlite3
from pathlib import Path
from purge_comparison import purge_database
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backup',type=Path,required=True)
parser.add_argument('--data-dir',type=Path,required=True)
args=parser.parse_args()
os.umask(0o077)
source=args.backup.resolve(strict=True)
target=args.data_dir.absolute()
if target.exists():
    raise SystemExit('Target already exists; choose a new directory. Nothing was overwritten.')
manifest=source.with_name(source.name+'.manifest.json')
if not manifest.is_file():
    raise SystemExit('Missing complete-backup manifest. Use a verified full Studio backup.')
metadata=json.loads(manifest.read_text())
if metadata.get('schema')!=1:raise SystemExit('Unsupported backup manifest.')
dataset=Path(metadata.get('dataset','studio.sqlite3'))
if dataset.is_absolute() or '..' in dataset.parts or dataset.name!='studio.sqlite3':
    raise SystemExit('Invalid dataset path in backup.')
connection=metadata.get('connection')
if connection is not None:
    if not isinstance(connection,dict) or connection.get('source','juggluco') not in ('juggluco','nightscout'):
        raise SystemExit('Invalid source configuration in backup.')
    if set(connection)-{'source','mode','address','auth','serial','wireguardInterface'}:
        raise SystemExit('Unexpected source fields in backup; credentials must remain in the wallet.')
checked=[]
for entry in metadata['files']:
    relative=Path(entry['relative'])
    if relative.is_absolute() or '..' in relative.parts:raise SystemExit('Invalid source path in backup.')
    original=source.with_name(source.name+'.sources')/relative
    if original.is_symlink() or not original.is_file():raise SystemExit('Missing source file in backup.')
    if hashlib.sha256(original.read_bytes()).hexdigest()!=entry['sha256']:raise SystemExit('Source checksum mismatch.')
    checked.append((entry,original,relative))
with sqlite3.connect('file:'+str(source)+'?mode=ro',uri=True) as db:
    if db.execute('PRAGMA integrity_check').fetchone()[0]!='ok':raise SystemExit('Database integrity check failed.')
    rows=list(db.execute('SELECT path,sha256 FROM import_chunks'))
    if {(r[0],r[1]) for r in rows}!={(e['original'],e['sha256']) for e,_,_ in checked}:raise SystemExit('Manifest does not match database sources.')
    target.mkdir(mode=0o700,parents=True)
    restored_path=target/dataset
    restored_path.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    with sqlite3.connect(restored_path) as restored:
        db.backup(restored)
        for entry,original,relative in checked:
            destination=restored_path.parent/relative
            destination.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
            shutil.copyfile(original,destination)
            destination.chmod(0o600)
            restored.execute('UPDATE import_chunks SET path=? WHERE path=?',(str(destination),entry['original']))
        restored.commit()
        if restored.execute('PRAGMA integrity_check').fetchone()[0]!='ok':raise SystemExit('Restored integrity check failed; original data untouched.')
purge_database(restored_path)
restored_path.chmod(0o600)
if connection is not None:
    config=target/'connection.json'
    config.write_text(json.dumps(connection)+'\n')
    config.chmod(0o600)
print('Database and original exports restored to a new private directory. Original history untouched.')
