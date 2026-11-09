// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysis.h"
#include "fixtures/glucoid_night_fixture.h"
#include "importer.h"
#include "profile_fixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QUrlQuery>
#include <QSignalSpy>
#include <sqlite3.h>
using namespace Studio;
static qint64 utc(const char*t){return QDateTime::fromString(t,Qt::ISODate).toMSecsSinceEpoch();}
static QByteArray headerFor(const QString&kind){return kind=="amounts"?"Source\tnr\tUnixTime\tValue\tLabel\n":"Sensorid\tnr\tUnixTime\tmmol/L\n";}
static QByteArray exportRow(const QString&kind,qint64 time){auto out=headerFor(kind);auto seconds=QByteArray::number(time/1000);return out+(kind=="amounts"?QByteArray("0"):QByteArray("synthetic"))+"\t"+seconds+"\t"+seconds+(kind=="amounts"?"\t4\tSynthetic rapid\n":"\t6\n");}
class AnalysisTest:public QObject{
    Q_OBJECT
private slots:
    void separatesSeries(){auto s=parseStream(exportRow("stream",1760000000000LL));auto h=parseStream(exportRow("history",1760000000000LL),"history");QVERIFY(s.error.isEmpty());QVERIFY(h.error.isEmpty());QVERIFY(s.rows[0].key!=h.rows[0].key);QTemporaryDir d;Store store(d.path()+"/s.sqlite3");QVERIFY(store.ingest(s.rows+h.rows,{},1760000000000LL,1760000060000LL,false));QCOMPARE(store.readings(0,1800000000000LL).size(),1);QCOMPARE(store.readings(0,1800000000000LL,"history").size(),1);}
    void noGapInterpolation(){Reading r;r.time=utc("2026-10-01T00:00:00Z");r.mmol=6;r.sensor="synthetic";auto data=intervals({r},r.time,r.time+3600000);QCOMPARE(data.size(),1);QCOMPARE(data[0].to-data[0].from,60000);}
    void fillNeverDoubleCounts(){Reading s;s.time=0;s.mmol=6;s.sensor="s";Reading h=s;h.seriesKind="history";h.mmol=9;auto data=selectSeries({s},{h},0,300000,true);QCOMPARE(data.size(),2);QCOMPARE(data[0].from,0);QCOMPARE(data[0].to,60000);QCOMPARE(data[1].from,60000);QCOMPARE(data[1].to,300000);}
    void equalDayWeights(){QTimeZone z("UTC");QDate date(2026,10,1);qint64 a=QDateTime(date,QTime(0,0),z).toMSecsSinceEpoch();QList<Interval> v;for(int i=0;i<15;i++)v<<Interval{a+i*60000,a+(i+1)*60000,6,"stream"};v<<Interval{a+86400000,a+86400000+900000,10,"history"};auto r=analyse(v,date,2,z,4,10);auto b=r["bins"].toArray()[0].toObject();QCOMPARE(b["days"].toInt(),2);QCOMPARE(b["mean"].toDouble(),8.0);QCOMPARE(b["median"].toDouble(),8.0);QCOMPARE(b["p25"].toDouble(),7.0);QCOMPARE(r["coverage"].toDouble(),1800000.0/(2*86400000));}
    void autumnHourCountsOncePerDay(){const auto a=utc("2026-10-25T00:00:00Z"),b=utc("2026-10-25T01:00:00Z"),c=utc("2026-10-26T01:00:00Z");auto r=analyse({{a,a+900000,4,"stream"},{b,b+900000,10,"stream"},{c,c+900000,14,"stream"}},QDate(2026,10,25),2,QTimeZone("Europe/Helsinki"),4,10);auto bin=r["bins"].toArray()[12].toObject();QCOMPARE(bin["days"].toInt(),2);QCOMPARE(bin["mean"].toDouble(),10.5);QCOMPARE(r["daily"].toArray()[0].toObject()["hours"].toInt(),25);}
    void springMissingHourIsMissing(){QTimeZone z("Europe/Helsinki");QDate date(2026,3,29);auto a=QDateTime(date,QTime(0,0),z).toMSecsSinceEpoch(),b=QDateTime(date.addDays(1),QTime(0,0),z).toMSecsSinceEpoch();auto r=analyse({{a,b,6,"stream"}},date,1,z,4,10);QCOMPARE(r["coverage"].toDouble(),1.0);QCOMPARE(r["daily"].toArray()[0].toObject()["hours"].toInt(),23);QCOMPARE(r["bins"].toArray()[12].toObject()["days"].toInt(),0);QCOMPARE(r["inRange"].toDouble(),1.0);}
    void missingTargetsNotNormal(){QTimeZone z("UTC");QDate d(2026,10,1);qint64 a=QDateTime(d,QTime(0,0),z).toMSecsSinceEpoch();auto r=analyse({{a,a+60000,6,"stream"}},d,1,z,NAN,NAN);QVERIFY(!r["targetsSet"].toBool());QVERIFY(!r.contains("inRange"));QVERIFY(r["coverage"].toDouble()<.001);QCOMPARE(r["bins"].toArray()[1].toObject()["days"].toInt(),0);}
    void gmiUsesPublishedFormula(){
        QVERIFY(qAbs(gmiPercent(7.0)-6.327)<1e-4);
        QVERIFY(qAbs(gmiMmolMol(7.0)-45.65109)<1e-3);
        QVERIFY(qAbs(gmiPercent(10.0)-(3.31+0.02392*180.182))<1e-9);
        QVERIFY(qAbs(gmiMmolMol(10.0)-(12.71+4.70587*10.0))<1e-9);
    }
    void nightMeanUsesSelectedWindowAndDuration(){
        const auto fixture = GlucoidFixture::nightWindows();
        const auto &clipped = fixture.clipped;
        const auto result = analyse(clipped.intervals, fixture.day, clipped.days, fixture.zone, 4, 10);
        QCOMPARE(result["nightMean"].toDouble(), clipped.mean);
        QCOMPARE(result["nightCoveredSeconds"].toDouble(), fixture.clippedCoveredSeconds);
        QCOMPARE(result["nightCoverage"].toDouble(), clipped.coverage);
        QVERIFY(result["mean"].toDouble() != result["nightMean"].toDouble());
        const auto &extended = fixture.extended;
        const auto expanded = analyse(extended.intervals, fixture.day, extended.days, fixture.zone, 4, 10);
        QCOMPARE(expanded["nightMean"].toDouble(), extended.mean);
        QCOMPARE(expanded["nightCoverage"].toDouble(), extended.coverage);
    }
    void noNightDataHasNoMean(){
        const QTimeZone zone("UTC");const QDate day(2026,10,2);
        const auto at=QDateTime(day,QTime(12,0),zone).toMSecsSinceEpoch();
        for(const auto &data:QList<QList<Interval>>{{},{{at,at+3600000,6,"stream"}}}){
            const auto result=analyse(data,day,1,zone,4,10);
            QVERIFY(!result.contains("nightMean"));
            QCOMPARE(result["nightCoverage"].toDouble(),0.0);
        }
    }
    void nightMeanRespectsLocalTimeAndDst(){
        const QTimeZone zone("Europe/Helsinki");
        for(const QDate day:{QDate(2026,3,29),QDate(2026,10,25)}){
            const auto from=QDateTime(day,QTime(0,0),zone).toMSecsSinceEpoch();
            const auto to=QDateTime(day,QTime(6,0),zone).toMSecsSinceEpoch();
            const auto result=analyse({{from,to,7,"stream"}},day,1,zone,4,10);
            QCOMPARE(result["nightMean"].toDouble(),7.0);
            QCOMPARE(result["nightCoverage"].toDouble(),1.0);
            QCOMPARE(result["nightCoveredSeconds"].toDouble(),(day.month()==3?5:7)*3600.0);
        }
    }
    void currentTddNeedsConfirmation(){QJsonObject a{{"time",double(utc("2026-10-01T08:00:00Z"))},{"kind","rapid"},{"value",4}},b=a;b["kind"]="basal";b["value"]=16;auto r=insulinSummary({a,b},QDate(2026,10,1),1,QTimeZone("UTC"),{});QCOMPARE(r["completeDays"].toInt(),0);QVERIFY(!r.contains("meanTotal"));r=insulinSummary({a,b},QDate(2026,10,1),1,QTimeZone("UTC"),{"2026-10-01"});QCOMPARE(r["meanTotal"].toDouble(),20.0);b["analysisExcluded"]=true;r=insulinSummary({a,b},QDate(2026,10,1),1,QTimeZone("UTC"),{"2026-10-01"});QCOMPARE(r["completeDays"].toInt(),0);}
    void historicalDoesNotRequireLiveSettings(){auto p=testProfile();for(auto key:{"maxGlucoseAgeMinutes","maxBolus","doseIncrement","rounding"})p.remove(key);auto a=p["actionModel"].toObject();a.remove("curve");p["actionModel"]=a;QVERIFY(!validateProfile(p).isEmpty());QVERIFY2(validateHistoricalProfile(p).isEmpty(),qPrintable(validateHistoricalProfile(p).join('\n')));p["historical"]=true;QJsonArray episodes;for(int i=0;i<3;++i)episodes<<QJsonObject{{"id",QString::number(i)},{"kind","icr"},{"slot","all"},{"profileVersion",1},{"start",double(now-(i+4)*86400000LL)},{"end",double(now-(i+4)*86400000LL+14400000)},{"reviewed",true},{"complete",true},{"contextClear",true},{"coverage",1},{"deviation",3}};QCOMPARE(recommend(p,"icr","all",episodes,now)["candidate"].toDouble(),9.5);}
    void migrationPreservesLegacy(){QTemporaryDir d;auto file=d.path()+"/old.sqlite3";sqlite3*old=nullptr;QVERIFY(sqlite3_open(file.toUtf8(),&old)==SQLITE_OK);QVERIFY(sqlite3_exec(old,"CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);INSERT INTO meta VALUES('schema','1');CREATE TABLE glucose(id TEXT PRIMARY KEY,sensor TEXT NOT NULL,time INTEGER NOT NULL,mmol REAL NOT NULL,original REAL NOT NULL,unit TEXT NOT NULL,reliable INTEGER NOT NULL);INSERT INTO glucose VALUES('old','synthetic',1760000000000,6,6,'mmol/L',1);",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(old);Store s(file);QVERIFY2(s.good(),qPrintable(s.error()));QCOMPARE(s.meta("schema"),"2");QCOMPARE(s.readings(0,1800000000000LL).size(),1);QVERIFY(!QDir(d.path()+"/backups").entryList({"pre-schema2-*.sqlite3"},QDir::Files).isEmpty());}
    void completeEmptySnapshotRemovesSource(){QTemporaryDir d;Store s(d.path()+"/s.sqlite3");auto run=s.beginImport(0,1800000000000LL,"synthetic");QVERIFY(s.importChunk(run,"amounts",exportRow("amounts",1760000000000LL),1000,1800000000000LL));QCOMPARE(s.amounts(0,1800000000000LL).size(),1);QVERIFY(s.importChunk(run,"amounts",headerFor("amounts"),1000,1800000000000LL));QCOMPARE(s.amounts(0,1800000000000LL).size(),0);QCOMPARE(s.query("SELECT * FROM import_chunks").size(),2);}
    void additiveFileKeepsAbsentSource(){QTemporaryDir d;Store s(d.path()+"/s.sqlite3");auto run=s.beginImport(0,1800000000000LL,"synthetic");QVERIFY(s.importChunk(run,"amounts",exportRow("amounts",1760000000000LL),1000,1800000000000LL));QVERIFY(s.importChunk(run,"amounts",headerFor("amounts"),1000,1800000000000LL,false));QCOMPARE(s.amounts(0,1800000000000LL).size(),1);}
    void provenanceFailureRollsBack(){QTemporaryDir d;Store s(d.path()+"/s.sqlite3");QVERIFY(!s.importChunk("nonexistent-run","amounts",exportRow("amounts",1760000000000LL),1000,1800000000000LL));QCOMPARE(s.amounts(0,1800000000000LL).size(),0);}
    void dayConfirmationInvalidatesOnChange(){QTemporaryDir d;Store s(d.path()+"/s.sqlite3");auto id=s.addLocal(utc("2026-10-01T08:00:00Z"),4,"rapid","synthetic","ateria");QVERIFY(s.confirmDay("2026-10-01",true));QVERIFY(s.completeDays().contains("2026-10-01"));s.setMeta("analysisZone","UTC");QVERIFY(!s.completeDays().contains("2026-10-01"));s.setMeta("analysisZone","Europe/Helsinki");QVERIFY(s.completeDays().contains("2026-10-01"));QVERIFY(s.annotate(id,{{"purpose","korjaus"}}));QVERIFY(!s.completeDays().contains("2026-10-01"));}
    void repeatedImportAvoidsTinyHistoryTail(){
        for(qint64 extra:{1000LL,29000LL,3599000LL}){
            QTemporaryDir dir;Store store(dir.path()+"/s.sqlite3");
            const qint64 start=1760000000000LL,stop=start+7LL*86400000+extra;
            QMap<QString,QList<QPair<qint64,qint64>>> windows;
            Importer importer(store,[&](const QString &path,auto done){
                const QUrl url("http://localhost"+path);QUrlQuery query(url);
                const auto kind=url.path().section('/',-1);
                const qint64 a=query.queryItemValue("starttime").toLongLong()*1000;
                const qint64 b=(query.queryItemValue("endtime").toLongLong()+1)*1000;
                if(kind=="history" && b-a<3600000){done({},"HTTP 500");return;}
                if(kind!="amounts")windows[kind].append({a,b});
                done(headerFor(kind),{});
            });
            QSignalSpy finished(&importer,&Importer::finished);importer.start(start,stop,"synthetic");
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);
            QVERIFY(finished[0][0].toBool());
            for(const auto &kind:QStringList{"stream","history","scans"}){
                qint64 previous=start;
                for(const auto &window:windows[kind]){
                    QCOMPARE(window.first,previous);QVERIFY(window.second-window.first>=3600000);
                    QVERIFY(window.second-window.first<=7LL*86400000);previous=window.second;
                }
                QCOMPARE(previous,stop);
            }
        }
    }
    void adaptiveImportAndFailure(){QTemporaryDir d;Store s(d.path()+"/s.sqlite3");int split=0;Importer importer(s,[&](const QString&path,auto done){QUrl url("http://localhost"+path);QUrlQuery q(url);qint64 a=q.queryItemValue("starttime").toLongLong()*1000,b=(q.queryItemValue("endtime").toLongLong()+1)*1000;auto kind=url.path().section('/',-1);if(kind=="stream"&&b-a>3LL*86400000){++split;done({},"HTTP 413");}else done(exportRow(kind,a+60000),{});});QSignalSpy finished(&importer,&Importer::finished);importer.start(1760000000000LL,1760000000000LL+7LL*86400000,"synthetic");QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QVERIFY(finished[0][0].toBool());QVERIFY(split>0);QCOMPARE(s.imports()[0].toObject()["status"].toString(),"complete");QVERIFY(!s.meta("lastCompleteImport").isEmpty());
        const auto before=s.meta("lastCompleteImport");int n=0,attempts=0;Importer broken(s,[&](const QString&path,auto done){++attempts;if(n>=1){done({},"Connection lost");return;}++n;auto kind=QUrl("http://localhost"+path).path().section('/',-1);done(exportRow(kind,1760000060000LL),{});});QSignalSpy failed(&broken,&Importer::finished);broken.start(1760000000000LL,1760000000000LL+7LL*86400000,"synthetic");QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,15000);QVERIFY(!failed[0][0].toBool());QVERIFY(attempts>=4);QCOMPARE(s.meta("lastCompleteImport"),before);QCOMPARE(s.imports()[0].toObject()["status"].toString(),"partial");QVERIFY(!s.readings(0,1800000000000LL).isEmpty());}
    void transientImportFailureIsRetried(){QTemporaryDir d;Store s(d.path()+"/s.sqlite3");int failed=0,historyCalls=0;Importer importer(s,[&](const QString&path,auto done){QUrl url("http://localhost"+path);QUrlQuery q(url);qint64 a=q.queryItemValue("starttime").toLongLong()*1000;auto kind=url.path().section('/',-1);if(kind=="history"&&++historyCalls==1){++failed;done({},"Verkkohaku aikakatkaistiin.");return;}done(exportRow(kind,a+60000),{});});QSignalSpy progress(&importer,&Importer::progress);QSignalSpy finished(&importer,&Importer::finished);importer.start(1760000000000LL,1760000000000LL+7LL*86400000,"synthetic");QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,15000);QVERIFY2(finished[0][0].toBool(),qPrintable(finished[0][1].toString()));QCOMPARE(failed,1);bool retryShown=false;for(const auto&entry:progress)retryShown=retryShown||entry[0].toString().contains("uudelleenyritys 1/3");QVERIFY(retryShown);QCOMPARE(s.imports()[0].toObject()["status"].toString(),"complete");QVERIFY(!s.meta("lastCompleteImport").isEmpty());QVERIFY(!s.readings(0,1800000000000LL).isEmpty());}
};
QTEST_GUILESS_MAIN(AnalysisTest)
#include "test_analysis.moc"
