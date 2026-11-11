// SPDX-License-Identifier: GPL-3.0-or-later
#include "databasebackup.h"
#include "store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <QProcess>

using namespace Studio;

class DatabaseBackupTest : public QObject {
    Q_OBJECT
    static QString sha(const QByteArray& bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    static QString sourcePath(const QTemporaryDir& dir) {
        return dir.path() + "/imports/synthetic/one.tsv";
    }
    static void prepareLive(const QTemporaryDir& dir) {
        QDir().mkpath(QFileInfo(sourcePath(dir)).absolutePath());
        QFile source(sourcePath(dir));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("synthetic-source\n");
        source.close();
        Store live(dir.path() + "/studio.sqlite3");
        QVERIFY(live.good());
        QVERIFY(live.run("INSERT OR REPLACE INTO import_runs(id,started,finished,from_time,to_time,transport,status,error) VALUES(?,?,?,?,?,?,?,?)",
                         {"run-1", 0, 1000, 0, 1000, "synthetic", "success", ""}));
        QVERIFY(live.run("INSERT OR REPLACE INTO import_chunks VALUES(?,?,?,?,?,?,?,?,?)",
                         {"chunk-1", "run-1", "stream", 0, 1000, sourcePath(dir), sha("synthetic-source\n"), 1, "synthetic"}));
    }
  private slots:
    void portableCopyPreservesWalDataAndRestoresWithoutSources() {
        QTemporaryDir dir,target;
        prepareLive(dir);
        Store live(dir.path()+"/studio.sqlite3");
        QVERIFY(live.setMeta("syntheticWal","uncheckpointed"));
        const auto chosen=dir.path()+"/chosen.sqlite3";
        QString error;
        QVERIFY2(exportDatabaseBackup(live.path(),chosen,"juggluco",&error),qPrintable(error));
        QVERIFY(inspectBackup(chosen).complete);
        QVERIFY(!QFile::exists(chosen+".manifest.json"));
        QVERIFY(QFile::remove(sourcePath(dir))); // The original export is unavailable at restore time.
        const auto result=restoreBackup(chosen,target.path()+"/restored",false);
        QVERIFY2(result.ok,qPrintable(result.error));
        Store restored(result.dataPath);
        QCOMPARE(restored.meta("syntheticWal"),QString("uncheckpointed"));
        QCOMPARE(restored.query("SELECT * FROM import_chunks").size(),1);
        QVERIFY(!QDir(target.path()+"/restored/imports").exists());
        QVERIFY2(exportDatabaseBackup(restored.path(),target.path()+"/again.sqlite3","juggluco",&error),qPrintable(error));
    }
    void exportFailurePreservesExistingTargetAndProtectsLiveDatabase() {
        QTemporaryDir dir;
        prepareLive(dir);
        QString error;
        const auto live=dir.path()+"/studio.sqlite3",target=dir.path()+"/existing.sqlite3";
        QFile file(target);QVERIFY(file.open(QIODevice::WriteOnly));file.write("previous copy");file.close();
        QVERIFY(!exportDatabaseBackup(dir.path()+"/absent.sqlite3",target,"juggluco",&error));
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("previous copy"));file.close();
        QVERIFY(!exportDatabaseBackup(live,live,"juggluco",&error));
        QVERIFY(!exportDatabaseBackup(live,live+"-wal","juggluco",&error));
        QVERIFY(!exportDatabaseBackup(live,dir.path()+"/missing/copy.sqlite3","juggluco",&error));
        QVERIFY(exportDatabaseBackup(live,target,"juggluco",&error));
        QVERIFY(inspectBackup(target).complete);

    }
    void arbitrarySqliteFileIsNotAcceptedAsBackup() {
        QTemporaryDir dir;
        Store live(dir.path()+"/studio.sqlite3");
        QVERIFY(!inspectBackup(live.path()).complete);
    }
    void olderFormatIsRejected() {
        QTemporaryDir dir; Store live(dir.path()+"/studio.sqlite3");
        const auto path=dir.path()+"/older.sqlite3"; QString error;
        QVERIFY(exportDatabaseBackup(live.path(),path,"juggluco",&error));
        { Store old(path); QVERIFY(old.run("DELETE FROM meta WHERE key='portableBackupFormat'")); }
        QVERIFY(!inspectBackup(path).complete);
        QVERIFY(!restoreBackup(path,dir.path()+"/target",false).ok);
        QVERIFY(!QFile::exists(dir.path()+"/target/studio.sqlite3"));
        QProcess python; python.start("python3",{QStringLiteral(STUDIO_SOURCE_DIR)+"/tools/restore-backup.py","--backup",path,"--data-dir",dir.path()+"/python-target"});
        QVERIFY(python.waitForFinished());QVERIFY(python.exitCode()!=0);QVERIFY(!QFile::exists(dir.path()+"/python-target"));

    }
    void restoreRequestRoundTrip() {
        QTemporaryDir dir;
        const auto root = dir.path() + "/nested";
        QVERIFY(writeRestoreRequest(root, "/synthetic/backup.sqlite3"));
        QCOMPARE(readRestoreRequest(root), QString("/synthetic/backup.sqlite3"));
        clearRestoreRequest(root);
        QVERIFY(!QFileInfo::exists(restoreRequestPath(root)));
    }
};

QTEST_GUILESS_MAIN(DatabaseBackupTest)
#include "test_databasebackup.moc"
