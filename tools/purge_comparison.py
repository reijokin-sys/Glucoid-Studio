#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Remove retired comparison records only; preserve and verify every other row.

No raw-data backup is made. Reports contain aggregate technical evidence only.
Use with Studio closed. Symlinks are never followed or modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3

KEYS = ('supplementDaysV1', 'supplementSelectionV1', 'supplementSettingsV1')
MARKER = 'retiredComparisonV1'

def fingerprint(db):
    result = {}
    for (name,) in db.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"):
        quoted = '"' + name.replace('"', '""') + '"'
        columns = db.execute('PRAGMA table_info(' + quoted + ')').fetchall()
        primary = sorted(((c[5], i+1) for i,c in enumerate(columns) if c[5]))
        order = ','.join(str(i) for _,i in primary) or ','.join(str(i+1) for i in range(len(columns)))
        where, args = '', ()
        if name == 'meta':
            where = ' WHERE key NOT IN (?,?,?,?)'
            args = (*KEYS, MARKER)
        elif name == 'audit':
            where = " WHERE kind NOT GLOB 'supplement-*'"
        digest = hashlib.sha256()
        for row in db.execute('SELECT * FROM ' + quoted + where + ' ORDER BY ' + order, args):
            digest.update(repr(row).encode()); digest.update(b'\n')
        result[name] = digest.hexdigest()
    return result

def purge_database(path):
    path = Path(path)
    if path.is_symlink():
        raise ValueError('Refusing a symbolic link')
    with sqlite3.connect(path.as_uri() + '?mode=rw', uri=True, timeout=30) as db:
        tables = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        if not {'meta', 'audit'} <= tables:
            return False
        has_meta = db.execute('SELECT 1 FROM meta WHERE key IN (?,?,?) LIMIT 1', KEYS).fetchone()
        has_audit = db.execute("SELECT 1 FROM audit WHERE kind GLOB 'supplement-*' LIMIT 1").fetchone()
        if not has_meta and not has_audit:
            return False
        if db.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
            raise ValueError('Integrity check failed before cleanup')
        before = fingerprint(db)
        db.execute('PRAGMA secure_delete=ON')
        db.execute('BEGIN IMMEDIATE')
        db.execute('DELETE FROM meta WHERE key IN (?,?,?)', KEYS)
        db.execute("DELETE FROM audit WHERE kind GLOB 'supplement-*'")
        db.execute('INSERT INTO meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value', (MARKER, '1'))
        if fingerprint(db) != before:
            raise ValueError('Unrelated data changed; transaction rolled back')
        db.commit()
        db.execute('VACUUM')
        db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        if db.execute('PRAGMA integrity_check').fetchone()[0] != 'ok' or fingerprint(db) != before:
            raise ValueError('Post-cleanup verification failed')
        assert not db.execute('SELECT 1 FROM meta WHERE key IN (?,?,?)', KEYS).fetchone()
        assert not db.execute("SELECT 1 FROM audit WHERE kind GLOB 'supplement-*'").fetchone()
    path.chmod(0o600)
    return True

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1]/'private')
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    counts = dict(databases_checked=0, databases_cleaned=0, unrelated_rows_preserved=True)
    for path in sorted(args.root.rglob('*')):
        if not path.is_file() or path.is_symlink() or path.suffix not in ('.sqlite3', '.sqlite', '.db'):
            continue
        counts['databases_checked'] += 1
        changed = purge_database(path.absolute())
        counts['databases_cleaned'] += changed
        if changed and counts['databases_cleaned'] % 10 == 0:
            print('Verified clean databases:', counts['databases_cleaned'], flush=True)
    reports = 0
    markers = tuple(k.encode() for k in KEYS) + (b'supplement-day', b'supplement-selection', b'supplement-window')
    for path in args.root.rglob('*'):
        if not path.is_file() or path.is_symlink() or path.suffix.lower() not in ('.json', '.md', '.txt', '.csv', '.sql'):
            continue
        reports += 1
        tail = b''
        with path.open('rb') as stream:
            while block := stream.read(1024*1024):
                combined = tail + block
                if any(marker in combined for marker in markers):
                    raise RuntimeError('A retained report still contains retired comparison fields; inspect locally before completion')
                tail = combined[-64:]
    counts['structured_reports_checked'] = reports
    counts['retired_report_fields_remaining'] = 0
    counts['integrity_checks_passed'] = True
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(counts, indent=2)+'\n')
    print(json.dumps(counts))

if __name__ == '__main__':
    main()
