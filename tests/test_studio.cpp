#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "domain.h"
#include "importer.h"
#include "syncpolicy.h"
#include <QUrlQuery>
#include <QJsonDocument>
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
    void jugglucoClassesPreserveIdentitiesAndUserMetadata() {
        QTemporaryDir dir;
        FixtureStore store(dir.path()+"/data.sqlite3");
        constexpr qint64 at=1760000000000LL;
        const QByteArray raw="Source\tnr\tUnixTime\tValue\tLabel\n"
            "0\t1\t1760000000\t45\tCarbohydra\n"
            "0\t2\t1760000000\t4\tfast insulin\n"
            "0\t3\t1760000000\t10\tlong insulin\n"
            "0\t4\t1760000000\t7\tprot\n"
            "0\t5\t1760000000\t0\tzero\n"
            "0\t6\t1760000000\t9\tunconfigured\n"
            "1\t4\t1760000000\t1.2\tsecond source\n";
        auto sourceRow=[&](int position) {
            return QJsonObject{{"app","Juggluco"},{"identifier",QString("0fffffff-ffff-ffff-ffff-%1").arg(QString::number(position).leftJustified(12,'f'))},
                               {"date",double(at)},{"eventType","<none>"}};
        };
        auto carbs=sourceRow(0);carbs["carbs"]=90; // Juggluco applies its configured HH conversion.
        auto rapid=sourceRow(1);rapid["insulin"]=4;rapid["notes"]="Rapid-Acting";rapid["insulinType"]="fast insulin";
        auto basal=sourceRow(2);basal["insulin"]=10;basal["notes"]="Long-Acting";basal["insulinType"]="long insulin";
        auto other=sourceRow(3);other["notes"]="prot 7";
        auto second=rapid;second["identifier"]="1fff1234-5678-9abc-def0-3fffffffffff";second["insulin"]=1.2;second["insulinType"]="second source";
        QJsonArray rows{carbs,rapid,basal,other,second};
        auto json=[&] {return QJsonDocument(QJsonObject{{"status",200},{"result",rows}}).toJson();};
        const auto parsed=parseAmounts(raw);QVERIFY(parsed.error.isEmpty());
        const auto run=store.beginImport(at,at+1000,"fixture");
        QVERIFY(store.importChunk(run,"amounts",raw,at,at+1000));
        QVERIFY(store.mapLabel("Carbohydra","basal","Wrong old category"));
        QVERIFY(store.annotate(parsed.rows[1].key,{{"purpose","synthetic note"}}));
        QVERIFY(store.setDeleted(parsed.rows[1].key,true));
        QVERIFY(store.run("INSERT INTO analysis_exclusions VALUES(?,?,?)",{parsed.rows[2].key,"synthetic exclusion",at}));
        const auto local=store.addLocal(at,5,"carbs","","synthetic");QVERIFY(!local.isEmpty());
        QVERIFY2(store.importChunk(run,"amounts",raw,at,at+1000,true,json()),qPrintable(store.error()));
        const auto amounts=store.amounts(at,at+1000,true);QCOMPARE(amounts.size(),8);
        QHash<QString,QJsonObject> byId;
        for (const auto& v:amounts) byId.insert(v.toObject()["id"].toString(),v.toObject());
        QCOMPARE(byId[parsed.rows[0].key]["kind"].toString(),QString("carbs"));
        QCOMPARE(byId[parsed.rows[0].key]["value"].toDouble(),90.0);
        QCOMPARE(byId[parsed.rows[1].key]["kind"].toString(),QString("rapid"));
        QCOMPARE(byId[parsed.rows[1].key]["annotation"].toObject()["purpose"].toString(),QString("synthetic note"));
        QVERIFY(byId[parsed.rows[1].key]["deleted"].toBool());
        QVERIFY(byId[parsed.rows[1].key]["medicine"].toString().isEmpty()); // label is not a product name.
        QCOMPARE(byId[parsed.rows[2].key]["kind"].toString(),QString("basal"));
        QVERIFY(byId[parsed.rows[2].key]["analysisExcluded"].toBool());
        QCOMPARE(byId[parsed.rows[3].key]["kind"].toString(),QString("other")); // prot no longer hidden.
        QCOMPARE(byId[parsed.rows[4].key]["kind"].toString(),QString("unknown"));
        QCOMPARE(byId[parsed.rows[5].key]["kind"].toString(),QString("unknown"));
        QCOMPARE(byId[parsed.rows[6].key]["kind"].toString(),QString("rapid"));
        QVERIFY(byId.contains(local));
        const auto revision=store.revision();
        QVERIFY(store.importChunk(run,"amounts",raw,at,at+1000,true,json()));
        QCOMPARE(store.revision(),revision);
        // A concurrent insertion/renumbering must fail rather than misclassify old rows.
        auto wrong=rows;auto mismatch=rapid;mismatch["date"]=double(at+1000);wrong[1]=mismatch;
        QVERIFY(!store.importChunk(run,"amounts",raw,at,at+1000,true,QJsonDocument(QJsonObject{{"status",200},{"result",wrong}}).toJson()));
        QCOMPARE(store.revision(),revision);
        QCOMPARE(store.query("SELECT count(*) AS n FROM amounts")[0].toObject()["n"].toInt(),8);
    }
    void jugglucoClassesDoNotGuessFromNotesOrLabel() {
        const QByteArray raw="Source\tnr\tUnixTime\tValue\tLabel\n0\t1\t1760000000\t3\tCarbohydra\n";
        QJsonObject row{{"app","Juggluco"},{"identifier","0fffffff-ffff-ffff-ffff-0fffffffffff"},
                        {"date",1760000000000.0},{"eventType","<none>"},{"insulin",3},{"notes","comment Rapid-Acting"}};
        auto payload=[&] {return QJsonDocument(QJsonObject{{"status",200},{"result",QJsonArray{row}}}).toJson();};
        auto parsed=parseAmounts(raw);QString error;
        QVERIFY(applyJugglucoClasses(parsed,payload(),error));
        QCOMPARE(parsed.rows[0].kind,QString("unknown"));
        row["app"]="untrusted";parsed=parseAmounts(raw);
        QVERIFY(!applyJugglucoClasses(parsed,payload(),error));
        row["app"]="Juggluco";row["insulin"]=-3;parsed=parseAmounts(raw);
        QVERIFY(!applyJugglucoClasses(parsed,payload(),error));
        row["insulin"]=3;row["insulinUnits"]="ml";parsed=parseAmounts(raw);
        QVERIFY(!applyJugglucoClasses(parsed,payload(),error));
        row.remove("insulinUnits");row["insulinType"]="different label";parsed=parseAmounts(raw);
        QVERIFY(!applyJugglucoClasses(parsed,payload(),error));
    }
    void jugglucoSourceCatalogueWidensBeforeAcceptingSnapshot() {
        QTemporaryDir dir;FixtureStore store(dir.path()+"/data.sqlite3");
        QJsonArray rows;
        for(int i=0;i<1000;++i) rows.append(QJsonObject{{"app","Juggluco"},
            {"identifier",QString("0fffffff-ffff-ffff-ffff-%1").arg(QString::number(i).leftJustified(12,'f'))},
            {"date",1760000000000.0},{"carbs",1}});
        QStringList classRequests;
        Importer importer(store,[&](const QString& path,auto done) {
            if(path.startsWith("/api/v3/")) {classRequests<<path;done(QJsonDocument(QJsonObject{{"status",200},{"result",rows}}).toJson(),{});}
            else done(path.startsWith("/x/amounts")?QByteArray("Source\tnr\tUnixTime\tValue\tLabel\n"):QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n"),{});
        });
        QSignalSpy finished(&importer,&Importer::finished);
        importer.start(1760000000000LL,1760000001000LL,"fixture");QTRY_COMPARE(finished.size(),1);
        QVERIFY2(finished[0][0].toBool(),qPrintable(finished[0][1].toString()));
        QCOMPARE(classRequests,QStringList({"/api/v3/treatments?limit=1000","/api/v3/treatments?limit=2000"}));
    }
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
        QByteArray response=header+row(1,from-3600000,10)+row(9,from+60000,30)+row(10,from+180000,40);
        QStringList requests;
        QString lastError;
        bool lastOk=false;
        auto sync=[&] {
            Importer importer(store,[&](const QString& path,auto done){
            if (path.startsWith("/api/v3/treatments?")) { done(QByteArray(R"({"status":200,"result":[]})"), {}); return; }
                requests<<path;
                const QUrlQuery query(QUrl("http://localhost"+path));
                QCOMPARE(query.queryItemValue("starttime").toLongLong(),path.startsWith("/x/amounts") ? 1LL : from/1000);
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
        QCOMPARE(requests.size(),4); // catalogue request is handled before this list is populated.
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
    void normalUpdateDoesNotRetireHistoryOutsideCorrectionWindow() {
        QTemporaryDir dir;
        FixtureStore store(dir.path()+"/studio.sqlite3");
        const qint64 until=QDateTime::currentSecsSinceEpoch()*1000;
        const QByteArray header="Source\tnr\tUnixTime\tValue\tLabel\n";
        auto row=[](int nr,qint64 time,int value){return "0\t"+QByteArray::number(nr)+"\t"+QByteArray::number(time/1000)+"\t"+QByteArray::number(value)+"\tSynthetic carbs\n";};
        const qint64 old=until-30LL*86400000; // older than any correction window
        const auto seeded=parseAmounts(header+row(1,old,10));
        QVERIFY(seeded.error.isEmpty());
        QVERIFY(store.ingest({},seeded.rows,old,old+1000,false));
        // The next source export carries only a fresh row; the old row is gone from it.
        const qint64 recent=until-60000;
        const QByteArray exportBytes=header+row(2,recent,20);
        QString lastError;
        bool lastOk=false;
        auto sync=[&](qint64 from) {
            Importer importer(store,[&](const QString& path,auto done){
                if (path.startsWith("/api/v3/treatments?")) { done(QByteArray(R"({"status":200,"result":[]})"),{}); return; }
                done(path.startsWith("/x/amounts")?exportBytes:QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n"),{});
            });
            QSignalSpy finished(&importer,&Importer::finished);
            importer.start(from,until,"fixture");
            QTRY_COMPARE(finished.size(),1);
            lastOk=finished[0][0].toBool();
            lastError=lastOk?QString():finished[0][1].toString();
        };
        sync(until-86400000);
        QVERIFY2(lastOk,qPrintable(lastError));
        // A normal update keeps the older row: an incremental export's silence is not proof of deletion.
        QCOMPARE(store.amounts(0,until).size(),2);
        // Only an explicitly requested full-history check retires rows missing from the source.
        sync(0);
        QVERIFY2(lastOk,qPrintable(lastError));
        const auto remaining=store.amounts(0,until);
        QCOMPARE(remaining.size(),1);
        QCOMPARE(qint64(remaining[0].toObject()["time"].toDouble()),recent);
        QCOMPARE(store.query("SELECT count(*) AS n FROM amounts")[0].toObject()["n"].toInt(),2); // retired, not deleted
    }
    void stream(){auto r=parseStream(fixture("stream.tsv"));QVERIFY(r.error.isEmpty());QCOMPARE(r.rows.size(),2);QCOMPARE(r.rows[0].mmol,6.0);QCOMPARE(r.rows[0].time,1760000000000LL);}
    void mgdl(){auto bytes=fixture("stream.tsv").replace("mmol/L","mg/dL").replace("\t6.0\t","\t108.0\t");auto r=parseStream(bytes);QVERIFY(r.error.isEmpty());QCOMPARE(r.rows[0].mmol,6.0);QCOMPARE(r.rows[0].original,108.0);}
    void truncation(){auto b=fixture("stream.tsv");b.chop(1);QVERIFY(!parseStream(b).error.isEmpty());b=fixture("amounts.tsv");b.append("0\t4\t1760000000\n");QVERIFY(!parseAmounts(b).error.isEmpty());}
    void invalidData(){auto b=fixture("stream.tsv").replace("\t6.0\t","\tnan\t");QVERIFY(!parseStream(b).error.isEmpty());auto a=fixture("amounts.tsv").replace("\t45\t","\t-1\t");QVERIFY(!parseAmounts(a).error.isEmpty());}
    void duplicateRealDoses(){auto a=parseAmounts(fixture("amounts.tsv"));QVERIFY(a.error.isEmpty());QCOMPARE(a.rows.size(),3);QVERIFY(a.rows[1].key!=a.rows[2].key);}
    void renumbering(){auto a=parseAmounts(fixture("amounts.tsv"));auto b=parseAmounts(fixture("amounts.tsv").replace("0\t1\t","0\t11\t").replace("0\t2\t","0\t12\t"));QCOMPARE(a.rows[0].key,b.rows[0].key);QCOMPARE(a.rows[1].key,b.rows[1].key);}

    void insulinLabelProductIsOptionalAndPersists() {
        QTemporaryDir dir;
        const QString path = dir.path() + "/data.sqlite3";
        {
            FixtureStore store(path);
            for (const auto& kind : QStringList{"rapid", "correction", "basal"}) {
                QVERIFY(store.mapLabel(kind, kind, "Synthetic product"));
                QVERIFY(store.mapLabel(kind, kind, ""));
                QVERIFY(!store.mapLabel(kind, "invalid-kind", ""));
            }
        }
        FixtureStore reopened(path);
        const auto rows = reopened.query("SELECT label,kind,medicine FROM mappings ORDER BY label");
        QCOMPARE(rows.size(), 3);
        for (const auto& row : rows) {
            const auto value = row.toObject();
            QCOMPARE(value["label"].toString(), value["kind"].toString());
            QVERIFY(value["medicine"].toString().isEmpty());
        }
    }
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
