#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Restore a verified Studio backup into a NEW directory; never overwrite history."""
import argparse, hashlib, json, os, sqlite3
from urllib.parse import urlsplit, urlunsplit, quote
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backup',type=Path,required=True)
parser.add_argument('--data-dir',type=Path,required=True)
parser.add_argument('--nightscout-address', help='New Nightscout HTTPS endpoint for a portable Nightscout database.')
args=parser.parse_args()
os.umask(0o077)
source=args.backup.resolve(strict=True)
target=args.data_dir.absolute()
if target.exists():
    raise SystemExit('Target already exists; choose a new directory. Nothing was overwritten.')
# Standalone file: validate before creating any destination.
with sqlite3.connect(source.as_uri()+'?mode=ro',uri=True) as db:
    if db.execute('PRAGMA integrity_check').fetchall()!=[('ok',)]:
        raise SystemExit('Database integrity check failed.')
    meta=dict(db.execute("SELECT key,value FROM meta WHERE key IN ('schema','portableBackupSource','portableBackupFormat')"))
    if meta.get('portableBackupFormat')!='1' or meta.get('schema')!='2' or meta.get('portableBackupSource') not in ('juggluco','nightscout'):
        raise SystemExit('Unsupported Studio database backup.')
    for table in ('glucose','amounts','import_chunks'):
        db.execute('SELECT * FROM '+table+' LIMIT 0')
    dataset=Path('studio.sqlite3')
    connection=None
    if meta['portableBackupSource']=='nightscout':
        url=urlsplit(args.nightscout_address or '')
        if url.scheme!='https' or not url.hostname or url.username or url.password or url.query or url.fragment:
            raise SystemExit('Provide --nightscout-address with the new HTTPS endpoint, without credentials.')
        host=url.netloc.removesuffix(':443')
        normalized=urlunsplit((url.scheme,host,quote(url.path.rstrip('/')+'/',safe='/%'),'', ''))
        digest=hashlib.sha256(normalized.encode()).hexdigest()
        dataset=Path('sources')/('nightscout-'+digest)/'studio.sqlite3'
        connection={'source':'nightscout','mode':'network','address':args.nightscout_address,'auth':'token'}
    target.mkdir(mode=0o700,parents=True)
    destination=target/dataset
    destination.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    with sqlite3.connect(destination) as restored:
        db.backup(restored)
        if restored.execute('PRAGMA integrity_check').fetchall()!=[('ok',)]:
            raise SystemExit('Restored integrity check failed.')
    destination.chmod(0o600)
    if connection:
        config=target/'connection.json'
        config.write_text(json.dumps(connection)+'\n')
        config.chmod(0o600)
print('Standalone database restored. Configure the source and credentials on this device separately.')
raise SystemExit(0)
