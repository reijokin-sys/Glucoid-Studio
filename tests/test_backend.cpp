#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "databasebackup.h"
#include "syncpolicy.h"
#include <QUrlQuery>
#include <QTimer>
#include <QElapsedTimer>
#include "fixtures/glucoid_summary_fixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QTimeZone>
#include <cmath>
#include <QProcess>
#include <QFileInfo>

using namespace Studio;
class BackendTest:public QObject{
    Q_OBJECT
    static qint64 start(){auto dt=QDateTime::currentDateTime(QTimeZone("Europe/Helsinki")).addDays(-2);return QDateTime(dt.date(),QTime(8,0),dt.timeZone()).toMSecsSinceEpoch();}
    static QList<Reading> samples(qint64 at){QList<Reading> rows;for(int minute=-240;minute<=360;minute+=5){Reading r;r.key=QString::number(at)+":"+QString::number(minute);r.sensor="synthetic";r.time=at+minute*60000;r.mmol=minute<240?6:9;r.original=r.mmol;r.originalUnit="mmol/L";rows<<r;}return rows;}
private slots:
    void updateCompletionIsIndependentOfBackupAndRejectsFailedRetry() {
        QTemporaryDir dir;
        FixtureStore store(dir.path()+"/studio.sqlite3");
        QVERIFY(store.setMeta("connectionConfigured","1"));
        QVERIFY(store.setMeta("lastSourceUntil",QString::number(QDateTime::currentSecsSinceEpoch()*1000)));
        QFile file(dir.path()+"/connection.json");QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"source\":\"juggluco\",\"mode\":\"usb\",\"auth\":\"none\"}");file.close();
        bool broken=false,completeAtImportFinish=true;
        Backend backend(dir.path(),nullptr,[&](const QString& path,auto done){
            if(broken){done({},"synthetic failure");return;}
            done(path.startsWith("/x/amounts")?QByteArray("Source\tnr\tUnixTime\tValue\tLabel\n"):QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n"),{});
        });
        backend.enableBackgroundWork();
        connect(&backend,&Backend::syncFinished,&backend,[&](bool){completeAtImportFinish=backend.updateComplete();});
        QSignalSpy finished(&backend,&Backend::syncFinished);
        QVERIFY(!backend.updateComplete());
        backend.sync();
        QVERIFY(backend.updateInProgress());QVERIFY(!backend.updateComplete());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);
        QVERIFY(finished[0][0].toBool());QVERIFY(!completeAtImportFinish);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.updateInProgress(),10000);
        QVERIFY(backend.updateComplete());
        broken=true;backend.sync();
        QVERIFY(!backend.updateComplete());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),2,10000);
        QVERIFY(!finished[1][0].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.updateInProgress(),10000);
        QVERIFY(!backend.updateComplete());
    }
    void backgroundSummaryKeepsLatestSelectionAndBackupIsRestorable() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        backend.seedDemo();
        const qint64 now=QDateTime::currentSecsSinceEpoch()*1000;
        FixtureStore store(dir.path()+"/studio.sqlite3");
        const auto run=store.beginImport(now-60000,now,"fixture");
        const auto data=QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\nsynthetic-extra\t1\t")+QByteArray::number(now/1000-30)+"\t6\n";
        QVERIFY(store.importChunk(run,"stream",data,now-60000,now));
        backend.enableBackgroundWork();
        int beats=0; qint64 maximumGap=0;
        QElapsedTimer gap;gap.start();
        QTimer heartbeat;heartbeat.setInterval(5);
        connect(&heartbeat,&QTimer::timeout,&heartbeat,[&]{maximumGap=std::max(maximumGap,gap.restart());++beats;});
        heartbeat.start();
        QElapsedTimer call;call.start();
        backend.setAnalysisWindow(90,"");
        backend.setAnalysisWindow(7,""); // invalidate the first worker's result
        backend.backupTo(dir.path()+"/chosen.sqlite3");
        QVERIFY(call.elapsed()<250);
        QVERIFY(backend.backgroundBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.backgroundBusy(),30000);
        heartbeat.stop();
        QVERIFY(beats>2);
        QVERIFY2(maximumGap<500,qPrintable(QString::number(maximumGap)));
        QCOMPARE(backend.analysis()["days"].toInt(),7);
        Backend reference(dir.path());
        QCOMPARE(backend.analysis()["mean"],reference.analysis()["mean"]);
        QVERIFY(backend.backupStatus().contains("chosen.sqlite3"));
        const auto backup=dir.path()+"/chosen.sqlite3";
        QVERIFY(QFile::exists(backup));
        QVERIFY(!QFile::exists(backup+".manifest.json"));
        QProcess restore;
        restore.start("python3",{QStringLiteral(STUDIO_SOURCE_DIR)+"/tools/restore-backup.py","--backup",backup,"--data-dir",dir.path()+"/restored"});
        QVERIFY(restore.waitForFinished());QCOMPARE(restore.exitCode(),0);
        FixtureStore restored(dir.path()+"/restored/studio.sqlite3");
        QVERIFY(restored.good());
        QCOMPARE(restored.query("SELECT * FROM import_chunks").size(),store.query("SELECT * FROM import_chunks").size());
    }
    void backgroundBackupFailureDoesNotBlockSuccessfulUpdate() {
        QTemporaryDir dir;
        FixtureStore store(dir.path()+"/studio.sqlite3");
        QVERIFY(store.setMeta("connectionConfigured","1"));
        QFile file(dir.path()+"/connection.json");QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"source\":\"juggluco\",\"mode\":\"usb\",\"auth\":\"none\"}");file.close();
        Backend backend(dir.path(),nullptr,[](const QString& path,auto done){
            done(path.startsWith("/x/amounts")?QByteArray("Source\tnr\tUnixTime\tValue\tLabel\n"):QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n"),{});
        });
        backend.enableBackgroundWork();
        QSignalSpy finished(&backend,&Backend::syncFinished);backend.sync();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QVERIFY(finished[0][0].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.updateInProgress(),10000);
        QVERIFY(backend.updateComplete());
        backend.backupTo(dir.path()+"/missing/backup.sqlite3");
        QTRY_VERIFY_WITH_TIMEOUT(!backend.backgroundBusy(),10000);
        QVERIFY(backend.backupStatus().contains("epäonnistui"));
        QVERIFY(backend.updateComplete());
        QVERIFY(!QFile::exists(dir.path()+"/missing/backup.sqlite3"));
    }
    void jugglucoSyncWindow_data(){
        QTest::addColumn<int>("daysSinceSync");
        QTest::newRow("first-183-days")<<0;
        QTest::newRow("recent-72-hours")<<1;
        QTest::newRow("ten-day-outage")<<10;
    }
    void jugglucoSyncWindow(){
        QFETCH(int,daysSinceSync);
        QTemporaryDir dir;
        const qint64 now=QDateTime::currentSecsSinceEpoch()*1000;
        const qint64 last=now-qint64(daysSinceSync)*86400000;
        FixtureStore store(dir.path()+"/studio.sqlite3");
        QVERIFY(store.setMeta("connectionConfigured","1"));
        if(daysSinceSync)QVERIFY(store.setMeta("lastSourceUntil",QString::number(last)));
        QFile file(dir.path()+"/connection.json");QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"source\":\"juggluco\",\"mode\":\"usb\",\"auth\":\"none\"}");file.close();
        const qint64 expected=!daysSinceSync?now-initialImportHistoryMs:daysSinceSync<3?now-correctionHistoryMs:last;
        qint64 amountsFrom=0,minimumFrom=now;
        Backend backend(dir.path(),nullptr,[&](const QString& path,auto done){
            const QUrlQuery query(QUrl("http://localhost"+path));
            const qint64 from=query.queryItemValue("starttime").toLongLong()*1000;
            minimumFrom=std::min(minimumFrom,from);
            const bool amounts=path.startsWith("/x/amounts");
            if(amounts)amountsFrom=from;
            done(amounts?QByteArray("Source\tnr\tUnixTime\tValue\tLabel\n"):QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n"),{});
        });
        backend.enableBackgroundWork();
        QSignalSpy done(&backend,&Backend::syncFinished);
        backend.sync();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QVERIFY(done[0][0].toBool());
        QVERIFY(minimumFrom>=expected && minimumFrom<=expected+10000);
        QVERIFY(amountsFrom>=expected && amountsFrom<=expected+10000);
    }
    void standardSummaryIgnoresResearchSwitchesAndRetainsData(){
        const GlucoidFixture::SummaryPolicy f;
        QTemporaryDir dir;FixtureStore db(dir.path()+"/studio.sqlite3");
        const QJsonObject settings{{"timezone","UTC"},{"low",4},{"high",8},
            {"maxGapMinutes",60},{"fillHistory",false}};
        QVERIFY(db.setMeta("analysisSettings",QString::fromUtf8(QJsonDocument(settings).toJson())));
        QVERIFY(db.ingest(f.stream()+f.history(),{},f.start-86400000,f.start+600000,false));
        const auto before=db.query("SELECT * FROM glucose ORDER BY id");
        Backend backend(dir.path());
        for(int days:{7,14,28}){
            backend.setAnalysisWindow(days,f.first.addDays(days-1).toString(Qt::ISODate));
            const auto r=backend.analysis();
            QVERIFY(qAbs(r["mean"].toDouble()-9.6)<1e-10);
            QCOMPARE(r["coveredSeconds"].toDouble(),600.0);
            QCOMPARE(r["historySeconds"].toDouble(),540.0);
            QCOMPARE(r["historyFraction"].toDouble(),0.9);
            QVERIFY(r["fillHistory"].toBool());
            QVERIFY(qAbs(r["inRange"].toDouble()-.1)<1e-10);
            QCOMPARE(r["nightMean"].toDouble(),r["mean"].toDouble());
            QCOMPARE(r["coverage"].toDouble(),600.0/(days*86400));
        }
        QCOMPARE(db.query("SELECT * FROM glucose ORDER BY id"),before);
        QCOMPARE(db.meta("analysisSettings"),QString::fromUtf8(QJsonDocument(settings).toJson()));
    }
    void currentReadingUsesNewerHistoryAndCommonFreshness(){
        QTemporaryDir dir;FixtureStore db(dir.path()+"/studio.sqlite3");
        GlucoidFixture::SummaryPolicy f;const auto now=QDateTime::currentMSecsSinceEpoch();
        const auto stream=f.row("live",now-3600000,5);
        const auto history=f.row("stored",now-16*60000,7,"history");
        QVERIFY(db.ingest({stream,history},{},now-3600000,now,false));
        Backend backend(dir.path());
        QCOMPARE(backend.currentReading()["value"].toDouble(),7.0);
        QCOMPARE(backend.currentReading()["seriesKind"].toString(),QString("history"));
        QVERIFY(backend.currentReading()["stale"].toBool());
    }

    void currentReadingRejectsGapsAndSensorChanges(){
        for(bool sensorChange:{false,true}){
            QTemporaryDir dir;FixtureStore db(dir.path()+"/studio.sqlite3");
            QVERIFY(db.setMeta("analysisSettings",QString::fromUtf8(QJsonDocument(QJsonObject{{"timezone","UTC"},{"low",4},{"high",10},{"maxGapMinutes",20}}).toJson())));
            const auto now=QDateTime::currentMSecsSinceEpoch();
            Reading first;first.key="first";first.sensor="old";first.time=now-(sensorChange?5:60)*60000;first.mmol=5;
            Reading last=first;last.key="last";last.time=now;last.mmol=10;if(sensorChange)last.sensor="new";
            QVERIFY(db.ingest({first,last},{},now-7200000,now+60000,false));
            Backend b(dir.path());const auto current=b.currentReading();
            QCOMPARE(current["value"].toDouble(),10.0);
            QVERIFY(!current["rateKnown"].toBool());
            QCOMPARE(current["trend"].toString(),QString("-"));
            QCOMPARE(current["status"].toString(),QString("ok"));
        }
    }
    void currentReadingUsesLatestReliableValueAndTrend(){
        QTemporaryDir dir;FixtureStore db(dir.path()+"/studio.sqlite3");
        const qint64 now=QDateTime::currentMSecsSinceEpoch();
        QList<Reading> rows;
        for(int i=0;i<8;++i){Reading r;r.key="current-"+QString::number(i);r.sensor="synthetic";r.time=now-(35-i*5)*60000;r.mmol=6.0+i*0.1;r.original=r.mmol;r.originalUnit="mmol/L";rows<<r;}
        QVERIFY(db.ingest(rows,{},now-3600000,now+60000,false));
        Backend b(dir.path());
        const auto fresh=b.currentReading();
        QCOMPARE(fresh["hasData"].toBool(),true);
        QCOMPARE(fresh["value"].toDouble(),6.7);
        QVERIFY(fresh["rateKnown"].toBool());
        QVERIFY(qAbs(fresh["rate"].toDouble()-0.02)<1e-9);
        QCOMPARE(fresh["trend"].toString(),QString::fromUtf8("→"));
        QCOMPARE(fresh["ageMinutes"].toInt(),0);
        QVERIFY(!fresh["stale"].toBool());
        QTemporaryDir oldDir;FixtureStore oldDb(oldDir.path()+"/studio.sqlite3");
        const auto at=start();QVERIFY(oldDb.ingest(samples(at),{},at-240*60000,at+361*60000,false));
        Backend old(oldDir.path());const auto stale=old.currentReading();
        QCOMPARE(stale["hasData"].toBool(),true);QCOMPARE(stale["value"].toDouble(),9.0);QVERIFY(stale["stale"].toBool());QCOMPARE(stale["status"].toString(),QString("stale"));
    }
    void nutritionRemovalPreservesSavedDataAndIgnoresProt(){
        QTemporaryDir dir;FixtureStore s(dir.path()+"/studio.sqlite3");const auto at=start();
        const auto meal=s.addLocal(at,40,"carbs","","ateria");
        Amount a;a.key="protein-source";a.source="synthetic";a.time=at+60000;a.value=50;a.label="prot";
        QVERIFY(s.ingest({}, {a},at,at+120000,false));
        const QString saved=QString::fromUtf8(QJsonDocument(QJsonObject{{meal,QJsonObject{{"protein",25},{"fat","usual"},{"note","synthetic"}}}}).toJson());
        QVERIFY(s.setMeta("nutritionNotesV1",saved));
        const auto amounts=s.query("SELECT * FROM amounts"),audit=s.query("SELECT * FROM audit");
        Backend b(dir.path());b.refresh();
        QCOMPARE(s.meta("nutritionNotesV1"),saved);
        QCOMPARE(s.query("SELECT * FROM amounts"),amounts);QCOMPARE(s.query("SELECT * FROM audit"),audit);
        for(const auto *name:{"nutritionMeals","nutritionNotes","proteinCarbFactor"})
            QCOMPARE(b.metaObject()->indexOfProperty(name),-1);
        QCOMPARE(b.metaObject()->indexOfMethod("saveNutrition(QString,double,QString,bool,QString,bool)"),-1);
        QCOMPARE(s.amounts(at,at+120000).size(),1); // HH survives; prot remains ignored.
        QCOMPARE(s.amounts(at,at+120000)[0].toObject()["id"].toString(),meal);
        QCOMPARE(s.query("SELECT id FROM amounts WHERE id='protein-source'").size(),1);
    }
    void connectionModeValidation(){
        QTemporaryDir dir;Backend b(dir.path());
        b.saveConnection("network","http://10.0.0.1:17580","none","","");
        QCOMPARE(b.connection().value("mode").toString(),QString("wireguard"));
        b.saveConnection("http","http://10.0.0.1:17580","none","","");
        QCOMPARE(b.connection().value("mode").toString(),QString("wireguard"));
    }

    void fileImportPreservesProvenance(){QTemporaryDir dir,source;QFile g(source.path()+"/stream.tsv");QVERIFY(g.open(QIODevice::WriteOnly));g.write("Sensorid\tnr\tUnixTime\tmmol/L\nsynthetic\t1\t1760000000\t6\n");g.close();QFile a(source.path()+"/amounts.tsv");QVERIFY(a.open(QIODevice::WriteOnly));a.write("Source\tnr\tUnixTime\tValue\tLabel\n0\t1\t1760000000\t4\tSynthetic rapid\n");a.close();Backend b(dir.path());QVERIFY(b.importFolder(source.path()));FixtureStore s(dir.path()+"/studio.sqlite3");QCOMPARE(s.query("SELECT * FROM import_chunks").size(),2);QCOMPARE(s.imports()[0].toObject()["transport"].toString(),"files-additive");QVERIFY(b.importFolder(source.path()));QCOMPARE(s.amounts(0,1800000000000LL).size(),1);
        const auto backup=b.backupTo(dir.path()+"/chosen.sqlite3");QVERIFY(!backup.isEmpty());QVERIFY(!QFile::exists(backup+".manifest.json"));
        QProcess restore;restore.start("python3",{QStringLiteral(STUDIO_SOURCE_DIR)+"/tools/restore-backup.py","--backup",backup,"--data-dir",dir.path()+"/restored"});QVERIFY(restore.waitForFinished());QVERIFY2(restore.exitCode()==0,restore.readAllStandardError().constData());
        FixtureStore restored(dir.path()+"/restored/studio.sqlite3");QVERIFY(restored.good());QCOMPARE(restored.readings(0,1800000000000LL).size(),1);QCOMPARE(restored.amounts(0,1800000000000LL).size(),1);QVERIFY(restored.importChunk(restored.beginImport(0,1000,"fixture"),"stream","Sensorid\tnr\tUnixTime\tmmol/L\n",0,1000));QVERIFY(!b.backupTo(dir.path()+"/after-restore.sqlite3").isEmpty());
    }

    void manualCopiesAreNeverPruned() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        QStringList paths;
        for(int i=0;i<4;++i){
            const auto path=dir.path()+"/copy-"+QString::number(i)+".sqlite3";
            QCOMPARE(backend.backupTo(path),path);paths.append(path);
        }
        for(const auto& path:paths) QVERIFY(QFile::exists(path));
        QVERIFY(backend.scheduleRestore(paths.last()));
        QCOMPARE(Studio::readRestoreRequest(dir.path()),paths.last());
    }
    void automaticBackupsNeverRunEvenWithOldEnabledSetting() {
        QTemporaryDir dir;
        FixtureStore store(dir.path() + "/studio.sqlite3");
        QVERIFY(store.setMeta("backupEnabled", "1"));
        QVERIFY(store.setMeta("lastAutoBackup", "1"));
        QVERIFY(store.setMeta("connectionConfigured", "1"));
        QFile file(dir.path() + "/connection.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"source\":\"juggluco\",\"mode\":\"usb\",\"auth\":\"none\"}");file.close();
        Backend backend(dir.path(), nullptr, [](const QString& path, auto done) {
            done(path.startsWith("/x/amounts") ? QByteArray("Source\tnr\tUnixTime\tValue\tLabel\n")
                                               : QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n"), {});
        });
        backend.enableBackgroundWork();
        QSignalSpy finished(&backend, &Backend::syncFinished);backend.sync();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.updateInProgress(),10000);
        QVERIFY(backend.updateComplete());
        QCOMPARE(QDir(dir.path()+"/backups").entryList({"*.sqlite3"},QDir::Files).size(),0);
        QVERIFY(backend.backupStatus().isEmpty());
    }



};
QTEST_GUILESS_MAIN(BackendTest)
#include "test_backend.moc"
