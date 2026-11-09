#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from purge_comparison import fingerprint,purge_database,KEYS

class PurgeTest(unittest.TestCase):
    def test_preserves_other_records_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'studio.sqlite3'
            with sqlite3.connect(path) as db:
                db.executescript('CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT); CREATE TABLE audit(id INTEGER PRIMARY KEY,kind TEXT,data TEXT); CREATE TABLE glucose(id INTEGER PRIMARY KEY,value REAL); CREATE TABLE import_chunks(path TEXT,sha256 TEXT);')
                db.executemany('INSERT INTO meta VALUES(?,?)',[(k,'synthetic-erased-payload') for k in KEYS]+[('keep','untouched')])
                db.executemany('INSERT INTO audit VALUES(?,?,?)',[(1,'supplement-day','synthetic-erased-payload'),(2,'keep','untouched')])
                db.execute('INSERT INTO glucose VALUES(1,6)');db.commit();before=fingerprint(db)
            self.assertTrue(purge_database(path))
            with sqlite3.connect(path) as db:self.assertEqual(fingerprint(db),before)
            self.assertNotIn(b'synthetic-erased-payload',path.read_bytes())
            self.assertFalse(purge_database(path))
            with sqlite3.connect(path) as db:db.execute('INSERT INTO meta VALUES(?,?)',(KEYS[0],'synthetic-erased-payload'))
            path.with_name(path.name+'.manifest.json').write_text(json.dumps({'schema':1,'files':[]}))
            target=Path(folder)/'restored'
            subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'tools/restore-backup.py'),'--backup',str(path),'--data-dir',str(target)],check=True,capture_output=True)
            with sqlite3.connect(target/'studio.sqlite3') as db:
                self.assertFalse(db.execute('SELECT 1 FROM meta WHERE key=?',(KEYS[0],)).fetchone())
                self.assertEqual(fingerprint(db),before)

if __name__=='__main__':unittest.main()
