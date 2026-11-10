// SPDX-License-Identifier: GPL-3.0-or-later
#include "domain.h"
#include "importer.h"
#include "syncpolicy.h"
#include <QUrlQuery>
#include "profile_fixture.h"
#include "store.h"
#include "transport.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
using namespace Studio;
static QJsonObject input(){return {{"carbs",60},{"glucose",10},{"glucoseTime",double(now-60000)},{"historyComplete",true},{"carbsComplete",true},{"contextReviewed",true},{"reliable",true}};}
static QJsonArray episodes(const QString&kind,double deviation){QJsonArray a;for(int i=0;i<3;++i)a.append(QJsonObject{{"id",QString::number(i)},{"kind",kind},{"slot","all"},{"profileVersion",1},{"start",double(now-(i+4)*86400000LL)},{"end",double(now-(i+4)*86400000LL+14400000)},{"reviewed",true},{"complete",true},{"contextClear",true},{"basalStable",true},{"coverage",0.95},{"deviation",deviation}});return a;}
static QByteArray fixture(const char*name){QFile f(QStringLiteral(FIXTURE_DIR)+"/"+name);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
class StudioTest:public QObject{
    Q_OBJECT
private slots:
    void recentJugglucoSnapshotImportsCorrectionsAndPreservesOlderAndLocal() {
        QTemporaryDir dir;
        Store store(dir.path()+"/studio.sqlite3");
        const qint64 until=QDateTime::currentSecsSinceEpoch()*1000;
        const qint64 from=until-correctionHistoryMs;
        const QByteArray header="Source\tnr\tUnixTime\tValue\tLabel\n";
        auto row=[](int nr,qint64 time,int value){return "0\t"+QByteArray::number(nr)+"\t"+QByteArray::number(time/1000)+"\t"+QByteArray::number(value)+"\tSynthetic carbs\n";};
        auto run=store.beginImport(from-86400000,until,"fixture");
        QVERIFY(store.importChunk(run,"amounts",header+row(1,from-3600000,10)+row(2,from+60000,20),from-86400000,until));
        const auto local=store.addLocal(from+120000,5,"carbs","","synthetic");
        QByteArray response=header+row(9,from+60000,30)+row(10,from+180000,40);
        QStringList requests;
        auto sync=[&] {
            Importer importer(store,[&](const QString& path,auto done){
                requests<<path;
                const QUrlQuery query(QUrl("http://localhost"+path));
                QCOMPARE(query.queryItemValue("starttime").toLongLong(),from/1000);
                done(path.startsWith("/x/amounts")?response:QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n"),{});
            });
            importer.setBackgroundWrites(true);
            QSignalSpy done(&importer,&Importer::finished);
            importer.start(from,until,"fixture");
            QTRY_COMPARE(done.size(),1);
            QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        };
        sync();
        QCOMPARE(requests.size(),4);
        QCOMPARE(store.amounts(0,until).size(),4); // older + edited + backdated + local
        const auto revision=store.revision();
        sync();
        QCOMPARE(store.revision(),revision);
        response=header; // valid empty window deletes only recent remote records
        sync();
        QCOMPARE(store.amounts(0,until).size(),2);
        QCOMPARE(store.amounts(from,until).first().toObject()["id"].toString(),local);
        QVERIFY(!store.meta("lastSourceUntil").isEmpty());
    }
    void stream(){auto r=parseStream(fixture("stream.tsv"));QVERIFY(r.error.isEmpty());QCOMPARE(r.rows.size(),2);QCOMPARE(r.rows[0].mmol,6.0);QCOMPARE(r.rows[0].time,1760000000000LL);}
    void mgdl(){auto bytes=fixture("stream.tsv").replace("mmol/L","mg/dL").replace("\t6.0\t","\t108.0\t");auto r=parseStream(bytes);QVERIFY(r.error.isEmpty());QCOMPARE(r.rows[0].mmol,6.0);QCOMPARE(r.rows[0].original,108.0);}
    void truncation(){auto b=fixture("stream.tsv");b.chop(1);QVERIFY(!parseStream(b).error.isEmpty());b=fixture("amounts.tsv");b.append("0\t4\t1760000000\n");QVERIFY(!parseAmounts(b).error.isEmpty());}
    void invalidData(){auto b=fixture("stream.tsv").replace("\t6.0\t","\tnan\t");QVERIFY(!parseStream(b).error.isEmpty());auto a=fixture("amounts.tsv").replace("\t45\t","\t-1\t");QVERIFY(!parseAmounts(a).error.isEmpty());}
    void duplicateRealDoses(){auto a=parseAmounts(fixture("amounts.tsv"));QVERIFY(a.error.isEmpty());QCOMPARE(a.rows.size(),3);QVERIFY(a.rows[1].key!=a.rows[2].key);}
    void renumbering(){auto a=parseAmounts(fixture("amounts.tsv"));auto b=parseAmounts(fixture("amounts.tsv").replace("0\t1\t","0\t11\t").replace("0\t2\t","0\t12\t"));QCOMPARE(a.rows[0].key,b.rows[0].key);QCOMPARE(a.rows[1].key,b.rows[1].key);}
    void profileRequired(){QVERIFY(!validateProfile(emptyProfile()).isEmpty());QVERIFY2(validateProfile(testProfile()).isEmpty(),qPrintable(validateProfile(testProfile()).join('\n')));}
    void invalidCurve(){auto p=testProfile();auto a=p["actionModel"].toObject();a["curve"]=QJsonArray{QJsonObject{{"minute",0},{"fraction",0.5}},QJsonObject{{"minute",240},{"fraction",0.8}}};p["actionModel"]=a;QVERIFY(!validateProfile(p).isEmpty());}
    void schedule(){auto p=testProfile();p["slots"]=QJsonArray{QJsonObject{{"name","night"},{"startMinute",0},{"icr",10},{"isf",2}},QJsonObject{{"name","day"},{"startMinute",480},{"icr",12},{"isf",3}}};auto z=QTimeZone("Europe/Helsinki");QCOMPARE(slotAt(p,QDateTime(QDate(2026,10,25),QTime(7,59),z).toMSecsSinceEpoch()),"night");QCOMPARE(slotAt(p,QDateTime(QDate(2026,10,25),QTime(8,0),z).toMSecsSinceEpoch()),"day");}
    void bolusArithmetic(){auto r=calculateBolus(testProfile(),input(),{},now);QCOMPARE(r["status"].toString(),"research");QCOMPARE(r["meal"].toDouble(),6.0);QCOMPARE(r["correction"].toDouble(),2.0);QCOMPARE(r["units"].toDouble(),8.0);}
    void activeInsulin(){QJsonArray doses{QJsonObject{{"time",double(now-120*60000)},{"value",4},{"kind","rapid"},{"medicine","Synthetic rapid"}},QJsonObject{{"time",double(now-60*60000)},{"value",16},{"kind","basal"},{"medicine","Synthetic basal"}}};auto r=calculateBolus(testProfile(),input(),doses,now);QCOMPARE(r["iob"].toDouble(),2.0);QCOMPARE(r["units"].toDouble(),6.0);}
    void noClinicalDefaults(){auto p=testProfile();p["approved"]=false;QCOMPARE(calculateBolus(p,input(),{},now)["status"].toString(),"blocked");p=testProfile();p.remove("maxBolus");QCOMPARE(calculateBolus(p,input(),{},now)["status"].toString(),"blocked");}
    void bolusBlocks_data(){QTest::addColumn<QString>("key");QTest::addColumn<QJsonValue>("value");QTest::newRow("hypo")<<QString("glucose")<<QJsonValue(3.0);QTest::newRow("old")<<QString("glucoseTime")<<QJsonValue(double(now-3600000));QTest::newRow("future")<<QString("glucoseTime")<<QJsonValue(double(now+3600000));QTest::newRow("history")<<QString("historyComplete")<<QJsonValue(false);QTest::newRow("carbs")<<QString("carbsComplete")<<QJsonValue(false);QTest::newRow("reliability")<<QString("reliable")<<QJsonValue(false);QTest::newRow("context")<<QString("contextReviewed")<<QJsonValue(false);QTest::newRow("negative-carbs")<<QString("carbs")<<QJsonValue(-1);QTest::newRow("max-bolus")<<QString("carbs")<<QJsonValue(1000);}
    void bolusBlocks(){QFETCH(QString,key);QFETCH(QJsonValue,value);auto in=input();in[key]=value;QCOMPARE(calculateBolus(testProfile(),in,{},now)["status"].toString(),"blocked");}
    void unknownDose(){QJsonArray d{QJsonObject{{"time",double(now-60000)},{"value",4},{"kind","unknown"}}};QCOMPARE(calculateBolus(testProfile(),input(),d,now)["status"].toString(),"blocked");d[0]=QJsonObject{{"time",double(now+1000)},{"value",4},{"kind","rapid"},{"medicine","Synthetic rapid"}};QCOMPARE(calculateBolus(testProfile(),input(),d,now)["status"].toString(),"blocked");}
    void rounding(){auto in=input();in["carbs"]=61;auto r=calculateBolus(testProfile(),in,{},now);QCOMPARE(r["units"].toDouble(),8.0);QCOMPARE(r["unrounded"].toDouble(),8.1);}
    void doseZero(){auto in=input();in["carbs"]=0;in["glucose"]=5;auto r=calculateBolus(testProfile(),in,{},now);QCOMPARE(r["units"].toDouble(),0.0);QCOMPARE(r["unrounded"].toDouble(),-0.5);}
    void ruleDirection_data(){QTest::addColumn<QString>("kind");QTest::addColumn<double>("expected");QTest::newRow("ICR")<<QString("icr")<<9.5;QTest::newRow("ISF")<<QString("isf")<<1.9;QTest::newRow("basal")<<QString("basal")<<16.8;}
    void ruleDirection(){QFETCH(QString,kind);QFETCH(double,expected);auto r=recommend(testProfile(),kind,"all",episodes(kind,3),now);QCOMPARE(r["status"].toString(),"research");QVERIFY(std::abs(r["candidate"].toDouble()-expected)<1e-9);}
    void conflictingEpisodes(){auto a=episodes("icr",3);auto e=a[0].toObject();e["deviation"]=-3;a[0]=e;QCOMPARE(recommend(testProfile(),"icr","all",a,now)["status"].toString(),"blocked");}
    void correctedEpisode(){auto a=episodes("icr",3);auto e=a[0].toObject();e["stale"]=true;a[0]=e;auto r=recommend(testProfile(),"icr","all",a,now);QCOMPARE(r["status"].toString(),"blocked");QCOMPARE(r["excluded"].toArray().size(),1);}
    void hypoBlocksIncrease(){auto a=episodes("basal",3);auto e=a[0].toObject();e["hypoObserved"]=true;a[0]=e;QCOMPARE(recommend(testProfile(),"basal","all",a,now)["status"].toString(),"blocked");}
    void observationPeriod(){auto p=testProfile();p["effectiveFrom"]=double(now-86400000);QCOMPARE(recommend(p,"icr","all",episodes("icr",3),now)["status"].toString(),"blocked");}
    void gapsAndCoverage(){QList<Reading> rs;for(qint64 t=0;t<3600000;t+=300000){Reading r;r.key=QString::number(t);r.time=t;r.mmol=6;rs<<r;}auto m=metrics(rs,0,7200000,4,10,300);QCOMPARE(m["coverage"].toDouble(),0.5);QCOMPARE(m["inRange"].toDouble(),1.0);QCOMPARE(m["cv"].toDouble(),0.0);}
    void overlappingSensors(){QList<Reading> rs;Reading a;a.time=0;a.mmol=6;rs<<a<<a;a.time=300000;rs<<a;auto m=metrics(rs,0,600000,4,10,300);QCOMPARE(m["coverage"].toDouble(),1.0);}
    void storeRoundtrip(){QTemporaryDir dir;Store s(dir.path()+"/data.sqlite3");QVERIFY(s.good());auto rs=parseStream(fixture("stream.tsv")).rows;auto as=parseAmounts(fixture("amounts.tsv")).rows;QVERIFY(s.ingest(rs,as,1760000000000LL,1760001000000LL,true));QVERIFY(s.ingest(rs,as,1760000000000LL,1760001000000LL,true));QCOMPARE(s.readings(0,now).size(),2);QCOMPARE(s.amounts(0,now).size(),3);QVERIFY(s.mapLabel("Synthetic rapid","rapid","Synthetic rapid"));QVERIFY(s.annotate(as[1].key,{{"purpose","meal"}}));QCOMPARE(s.amounts(0,now)[1].toObject()["kind"].toString(),"rapid");}
    void snapshotRemovalAndLocal(){QTemporaryDir dir;Store s(dir.path()+"/d.sqlite3");auto as=parseAmounts(fixture("amounts.tsv")).rows;QVERIFY(s.ingest({},as,1760000000000LL,1760001000000LL,true));auto local=s.addLocal(1760000010000LL,10,"carbs","","meal");QVERIFY(!local.isEmpty());as.removeLast();QVERIFY(s.ingest({},as,1760000000000LL,1760001000000LL,true));QCOMPARE(s.amounts(0,now).size(),3);QVERIFY(!s.ingest({},{},1760000000000LL,1760001000000LL,true));QCOMPARE(s.amounts(0,now).size(),3);}
    void episodeInvalidation(){QTemporaryDir dir;Store s(dir.path()+"/d.sqlite3");auto rs=parseStream(fixture("stream.tsv")).rows;auto as=parseAmounts(fixture("amounts.tsv")).rows;s.ingest(rs,as,1760000000000LL,1760001000000LL,true);QJsonObject e{{"start",1760000000000.0},{"dependencyFrom",1760000000000.0},{"end",1760001000000.0}};QVERIFY(!s.saveEpisode(e).isEmpty());QVERIFY(!s.episodes()[0].toObject()["stale"].toBool());s.mapLabel("Synthetic rapid","rapid","Synthetic rapid");QVERIFY(s.episodes()[0].toObject()["stale"].toBool());}
    void profilesAndBackup(){QTemporaryDir dir;Store s(dir.path()+"/d.sqlite3");QCOMPARE(s.saveProfile(testProfile()),1);auto p=testProfile();p["basalDose"]=17;QCOMPARE(s.saveProfile(p),2);QCOMPARE(s.activeProfile()["basalDose"].toInt(),17);QVERIFY(s.backup(dir.path()+"/backup.sqlite3"));Store restored(dir.path()+"/backup.sqlite3");QVERIFY(restored.good());QCOMPARE(restored.activeProfile()["version"].toInt(),2);QVERIFY(!s.backup(dir.path()+"/backup.sqlite3"));}
    void diaryDeleteAndRestore(){QTemporaryDir dir;Store s(dir.path()+"/d.sqlite3");auto id=s.addLocal(now-60000,4,"rapid","Synthetic rapid","ateria");QVERIFY(s.setDeleted(id,true));QCOMPARE(s.amounts(0,now).size(),0);QCOMPARE(s.amounts(0,now,true).size(),1);QVERIFY(s.calculationAmounts(0,now)[0].toObject()["stale"].toBool());QVERIFY(s.setDeleted(id,false));QCOMPARE(s.amounts(0,now).size(),1);QVERIFY(!s.calculationAmounts(0,now)[0].toObject()["stale"].toBool());QVERIFY(!s.setDeleted("missing",true));}
    void importedDeletePersists(){QTemporaryDir dir;Store s(dir.path()+"/d.sqlite3");auto as=parseAmounts(fixture("amounts.tsv")).rows;s.ingest({},as,1760000000000LL,1760001000000LL,true);QVERIFY(s.setDeleted(as[1].key,true));s.ingest({},as,1760000000000LL,1760001000000LL,true);QCOMPARE(s.amounts(0,now).size(),2);QCOMPARE(s.amounts(0,now,true).size(),3);QVERIFY(s.backup(dir.path()+"/backup.sqlite3"));Store restored(dir.path()+"/backup.sqlite3");QCOMPARE(restored.amounts(0,now).size(),2);}
    void deletionBlocksDose(){auto doses=QJsonArray{QJsonObject{{"time",double(now-60000)},{"value",4},{"kind","rapid"},{"medicine","Synthetic rapid"},{"deleted",true},{"stale",true}}};QCOMPARE(calculateBolus(testProfile(),input(),doses,now)["status"].toString(),"blocked");}
    void networkTransport(){
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray request;
        connect(&server,&QTcpServer::newConnection,&server,[&]{auto*socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{request+=socket->readAll();if(request.contains("\r\n\r\n")){auto body=fixture("amounts.tsv");socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();}});});
        Transport t;Connection c;c.mode="network";c.address="http://127.0.0.1:"+QString::number(server.serverPort());c.secret="synthetic-secret";bool complete=false;QString error;QByteArray response;
        t.get(c,"/x/amounts?header",[&](QByteArray bytes,QString e){response=bytes;error=e;complete=true;});QTRY_VERIFY_WITH_TIMEOUT(complete,5000);QVERIFY(error.isEmpty());QVERIFY(!request.contains("synthetic-secret"));QVERIFY(request.toLower().contains("api-secret:"));QVERIFY(!parseAmounts(response).error.isEmpty()==false);
    }
};
QTEST_GUILESS_MAIN(StudioTest)
#include "test_studio.moc"
