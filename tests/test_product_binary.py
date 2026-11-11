#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual product with an isolated D-Bus session and synthetic data."""
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
retired = ['research', 'legacy', 'automatic-report', 'activate-observation-profile',
           'smoke-test', 'verify-existing-ui', 'chart-stress-test']
help_result = subprocess.run([binary, '--help'], env=env, capture_output=True, text=True, check=True)
assert all('--'+option not in help_result.stdout for option in retired)
symbols = subprocess.check_output(['nm', '-C', binary], text=True)
for symbol in ['Backend::saveReport(', 'Backend::openResearchView(', 'Backend::bolus(',
               'Backend::analyseResponse(', 'Studio::calculateBolus(', 'Studio::recommend(',
               'Studio::automaticMealAnalysis(', 'Studio::insulinSummary(', 'Studio::targetDoseModel(']:
    assert symbol not in symbols, symbol
assert 'Backend::backup(' in symbols
assert 'Backend::maybeAutoBackup(' not in symbols
for retired_symbol in ['Studio::Store::backup(', 'Studio::listBackups(', 'readManifest(', 'updateChunkPath(', 'purgeRetired(']:
    assert retired_symbol not in symbols, retired_symbol
assert 'Studio::pruneBackups(' not in symbols
assert 'Studio::dailyPeriodAnalysis(' in symbols
with tempfile.TemporaryDirectory(prefix='studio-product-synthetic-') as directory:
    data = Path(directory)/'data'
    for option in retired:
        result = subprocess.run([binary, '--data-dir', data, '--'+option], env=env, capture_output=True, text=True)
        assert result.returncode != 0, option
        assert not data.exists(), option
    process = subprocess.Popen(['dbus-run-session', '--', binary, '--demo', '--data-dir', data],
                               env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True)
    try:
        # One fixed rendering/initialization window, no repeated UI polling.
        output, errors = process.communicate(timeout=5)
        raise AssertionError('Product exited before the synthetic UI check: '+errors)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        output, errors = process.communicate(timeout=5)
    for error in ['failed to load component', 'is not a type', 'ReferenceError:', 'TypeError:', 'Cannot assign']:
        assert error not in errors, errors
    with sqlite3.connect(data/'studio.sqlite3') as database:
        assert database.execute('SELECT count(*) FROM glucose').fetchone()[0] > 0
        assert database.execute('SELECT count(*) FROM amounts').fetchone()[0] > 0
        assert database.execute('SELECT count(*) FROM analysis_runs').fetchone()[0] == 0
    assert not (data/'automatic-report.json').exists()
    assert not list((data/'backups').glob('*.sqlite3'))
    backup = Path(directory)/'chosen.sqlite3'
    result = subprocess.run([binary, '--data-dir', data, '--backup-only', '--backup-file', backup], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert backup.is_file() and not Path(str(backup)+'.manifest.json').exists()
    # Headless maintenance cannot silently overwrite an existing file.
    before = backup.read_bytes()
    result = subprocess.run([binary, '--data-dir', data, '--backup-only', '--backup-file', backup], env=env, capture_output=True, text=True)
    assert result.returncode != 0 and backup.read_bytes() == before
print('Actual binary: supported CLI, removed model symbols, and compiled synthetic UI passed.')
