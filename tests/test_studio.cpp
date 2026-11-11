#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "domain.h"
#include "importer.h"
#include "syncpolicy.h"
#include <QUrlQuery>
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
inline constexpr qint64 now=1800000000000LL;
static QByteArray fixture(const char*name){QFile f(QStringLiteral(FIXTURE_DIR)+"/"+name);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
class StudioTest:public QObject{
    Q_OBJECT
private slots:
    void recentJugglucoSnapshotImportsCorrectionsAndPreservesOlderAndLocal() {
        QTemporaryDir dir;
        FixtureStore store(dir.path()+"/studio.sqlite3");
        const qint64 until=QDateTime::currentSecsSinceEpoch()*1000;
        const qint64 from=until-correctionHistoryMs;
        const QByteArray header="Source\tnr\tUnixTime\tValue\tLabel\n";
        auto row=[](int nr,qint64 time,int value){return "0\t"+QByteArray::number(nr)+"\t"+QByteArray::number(time/1000)+"\t"+QByteArray::number(value)+"\tSynthetic carbs\n";};
        auto run=store.beginImport(from-86400000,until,"fixture");
        QVERIFY(store.importChunk(run,"amounts",header+row(1,from-3600000,10)+row(2,from+60000,20),from-86400000,until));
        const auto local=store.addLocal(from+120000,5,"carbs","","synthetic");
        QByteArray response=header+row(9,from+60000,30)+row(10,from+180000,40);
        QStringList requests;
        QString lastError;
        bool lastOk=false;
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
            lastError=done[0][1].toString();
            lastOk=done[0][0].toBool();
        };
        sync();
        QVERIFY2(lastOk,qPrintable(lastError));
        QCOMPARE(requests.size(),4);
        QCOMPARE(store.amounts(0,until).size(),4); // older + edited + backdated + local
        const auto revision=store.revision();
        sync();
        QVERIFY2(lastOk,qPrintable(lastError));
        QCOMPARE(store.revision(),revision);
        response=header; // authoritative empty export must not delete imported rows
        sync();
        QVERIFY2(!lastOk,"An empty authoritative amounts export must fail");
        QVERIFY2(lastError.contains(QString::fromUtf8("tyhjää")),qPrintable(lastError));
        const auto remaining=store.amounts(0,until);
        QCOMPARE(remaining.size(),4);
        bool localPreserved=false;
        for(const auto &value:remaining) if(value.toObject()["id"].toString()==local) localPreserved=true;
        QVERIFY(localPreserved);
        QVERIFY(!store.meta("lastSourceUntil").isEmpty());
    }
    void stream(){auto r=parseStream(fixture("stream.tsv"));QVERIFY(r.error.isEmpty());QCOMPARE(r.rows.size(),2);QCOMPARE(r.rows[0].mmol,6.0);QCOMPARE(r.rows[0].time,1760000000000LL);}
    void mgdl(){auto bytes=fixture("stream.tsv").replace("mmol/L","mg/dL").replace("\t6.0\t","\t108.0\t");auto r=parseStream(bytes);QVERIFY(r.error.isEmpty());QCOMPARE(r.rows[0].mmol,6.0);QCOMPARE(r.rows[0].original,108.0);}
    void truncation(){auto b=fixture("stream.tsv");b.chop(1);QVERIFY(!parseStream(b).error.isEmpty());b=fixture("amounts.tsv");b.append("0\t4\t1760000000\n");QVERIFY(!parseAmounts(b).error.isEmpty());}
    void invalidData(){auto b=fixture("stream.tsv").replace("\t6.0\t","\tnan\t");QVERIFY(!parseStream(b).error.isEmpty());auto a=fixture("amounts.tsv").replace("\t45\t","\t-1\t");QVERIFY(!parseAmounts(a).error.isEmpty());}
    void duplicateRealDoses(){auto a=parseAmounts(fixture("amounts.tsv"));QVERIFY(a.error.isEmpty());QCOMPARE(a.rows.size(),3);QVERIFY(a.rows[1].key!=a.rows[2].key);}
    void renumbering(){auto a=parseAmounts(fixture("amounts.tsv"));auto b=parseAmounts(fixture("amounts.tsv").replace("0\t1\t","0\t11\t").replace("0\t2\t","0\t12\t"));QCOMPARE(a.rows[0].key,b.rows[0].key);QCOMPARE(a.rows[1].key,b.rows[1].key);}

    void storeRoundtrip(){QTemporaryDir dir;FixtureStore s(dir.path()+"/data.sqlite3");QVERIFY(s.good());auto rs=parseStream(fixture("stream.tsv")).rows;auto as=parseAmounts(fixture("amounts.tsv")).rows;QVERIFY(s.ingest(rs,as,1760000000000LL,1760001000000LL,true));QVERIFY(s.ingest(rs,as,1760000000000LL,1760001000000LL,true));QCOMPARE(s.readings(0,now).size(),2);QCOMPARE(s.amounts(0,now).size(),3);QVERIFY(s.mapLabel("Synthetic rapid","rapid","Synthetic rapid"));QVERIFY(s.annotate(as[1].key,{{"purpose","meal"}}));QCOMPARE(s.amounts(0,now)[1].toObject()["kind"].toString(),"rapid");}
    void snapshotRemovalAndLocal(){QTemporaryDir dir;FixtureStore s(dir.path()+"/d.sqlite3");auto as=parseAmounts(fixture("amounts.tsv")).rows;QVERIFY(s.ingest({},as,1760000000000LL,1760001000000LL,true));auto local=s.addLocal(1760000010000LL,10,"carbs","","meal");QVERIFY(!local.isEmpty());as.removeLast();QVERIFY(s.ingest({},as,1760000000000LL,1760001000000LL,true));QCOMPARE(s.amounts(0,now).size(),3);QVERIFY(!s.ingest({},{},1760000000000LL,1760001000000LL,true));QCOMPARE(s.amounts(0,now).size(),3);}

    void diaryDeleteAndRestore(){QTemporaryDir dir;FixtureStore s(dir.path()+"/d.sqlite3");auto id=s.addLocal(now-60000,4,"rapid","Synthetic rapid","ateria");QVERIFY(s.setDeleted(id,true));QCOMPARE(s.amounts(0,now).size(),0);QCOMPARE(s.amounts(0,now,true).size(),1);QVERIFY(s.setDeleted(id,false));QCOMPARE(s.amounts(0,now).size(),1);QVERIFY(!s.setDeleted("missing",true));}
    void importedDeletePersists(){QTemporaryDir dir;FixtureStore s(dir.path()+"/d.sqlite3");auto as=parseAmounts(fixture("amounts.tsv")).rows;s.ingest({},as,1760000000000LL,1760001000000LL,true);QVERIFY(s.setDeleted(as[1].key,true));s.ingest({},as,1760000000000LL,1760001000000LL,true);QCOMPARE(s.amounts(0,now).size(),2);QCOMPARE(s.amounts(0,now,true).size(),3);QVERIFY(s.backup(dir.path()+"/backup.sqlite3"));FixtureStore restored(dir.path()+"/backup.sqlite3");QCOMPARE(restored.amounts(0,now).size(),2);}

    void networkTransport(){
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray request;
        connect(&server,&QTcpServer::newConnection,&server,[&]{auto*socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{request+=socket->readAll();if(request.contains("\r\n\r\n")){auto body=fixture("amounts.tsv");socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();}});});
        Transport t;Connection c;c.mode="network";c.address="http://127.0.0.1:"+QString::number(server.serverPort());c.secret="synthetic-secret";bool complete=false;QString error;QByteArray response;
        t.get(c,"/x/amounts?header",[&](QByteArray bytes,QString e){response=bytes;error=e;complete=true;});QTRY_VERIFY_WITH_TIMEOUT(complete,5000);QVERIFY(error.isEmpty());QVERIFY(!request.contains("synthetic-secret"));QVERIFY(request.toLower().contains("api-secret:"));QVERIFY(!parseAmounts(response).error.isEmpty()==false);
    }
};
QTEST_GUILESS_MAIN(StudioTest)
#include "test_studio.moc"
