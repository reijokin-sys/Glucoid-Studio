#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport.h"
#include "importer.h"
#include "store.h"
#include "glucoseanalysis.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QFile>
#include <QTimer>
#include <iostream>
#include <unistd.h>
using namespace Studio;
static constexpr qint64 from=1760000000000LL, until=1760003600000LL;
static QByteArray glucose="Sensorid\tnr\tUnixTime\tmmol/L\nLAB\t1\t1760000100\t6\n";
static QByteArray amounts="Source\tnr\tUnixTime\tValue\tLabel\n0\t1\t1760000100\t10\tLAB'); DROP TABLE glucose;--\n";
static QByteArray testCredential(){return QByteArray("LAB_ONLY_NOT_A_REAL_CREDENTIAL");}
struct Reply {QByteArray body;QString error; qint64 elapsed=0;};
static Reply get(Transport&t,const Connection&c,const QString&path="/x/stream?header"){
    Reply r;QEventLoop loop;QElapsedTimer clock;clock.start();bool done=false;
    QTimer guard;guard.setSingleShot(true);QObject::connect(&guard,&QTimer::timeout,&loop,[&]{r.error="harness deadline";loop.quit();});guard.start(45000);
    t.get(c,path,[&](QByteArray bytes,QString error){r.body=bytes;r.error=error;done=true;loop.quit();});
    if(!done)loop.exec();
    r.elapsed=clock.elapsed();return r;
}
class Server:public QTcpServer {
public:
    QList<QByteArray> requests;
    std::function<QByteArray(const QByteArray&)> respond;
    Server(){connect(this,&QTcpServer::newConnection,this,[this]{while(hasPendingConnections()){
        auto*s=nextPendingConnection();auto bytes=std::make_shared<QByteArray>();auto sent=std::make_shared<bool>(false);
        connect(s,&QTcpSocket::readyRead,s,[this,s,bytes,sent]{*bytes+=s->readAll();if(*sent||!bytes->contains("\r\n\r\n"))return;*sent=true;requests.append(*bytes);
            auto response=respond?respond(*bytes):QByteArray();if(response.isEmpty())return;s->write(response);s->disconnectFromHost();});
        connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);
    }});}
    static QByteArray response(const QByteArray&body,int status=200){return "HTTP/1.0 "+QByteArray::number(status)+" Test\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;}
    Connection connection()const{Connection c;c.mode="network";c.address="http://127.0.0.1:"+QString::number(serverPort());c.secret=QString::fromLatin1(testCredential());return c;}
};
class SecurityTest:public QObject{
 Q_OBJECT
private slots:
 void requiredTunnelRejectsUnsafeInputs_data(){
    QTest::addColumn<QString>("interface");QTest::addColumn<QString>("address");
    QTest::newRow("missing-device")<<QString("missing-lab")<<QString("http://127.0.0.1:1");
    QTest::newRow("not-wireguard")<<QString("lo")<<QString("http://127.0.0.1:1");
    QTest::newRow("empty-device")<<QString()<<QString("http://127.0.0.1:1");
    QTest::newRow("dns-not-numeric")<<QString("wg0")<<QString("http://localhost:1");
 }
 void requiredTunnelRejectsUnsafeInputs(){
    QFETCH(QString,interface);QFETCH(QString,address);Transport t;Connection c;c.mode="wireguard";c.address=address;c.wireguardInterface=interface;c.secret=QString::fromLatin1(testCredential());
    const auto r=get(t,c);QVERIFY(!r.error.isEmpty());QVERIFY(r.elapsed<1000);QVERIFY(r.body.isEmpty());QVERIFY(!t.busy);QVERIFY(!r.error.contains(c.secret));
 }
 void normalImportAndRepeat(){
    QTemporaryDir dir;FixtureStore db(dir.path()+"/test.sqlite3");QVERIFY(db.good());Server server;QVERIFY(server.listen(QHostAddress::LocalHost));
    server.respond=[](const QByteArray&r){return Server::response(r.startsWith("GET /x/amounts")?amounts:glucose);};
    Transport t;auto c=server.connection();Importer importer(db,[&](const QString&p,auto done){t.get(c,p,done);});QSignalSpy spy(&importer,&Importer::finished);
    importer.start(from,until,"synthetic-network");QVERIFY(spy.wait(10000));QVERIFY(spy.last()[0].toBool());
    QCOMPARE(db.readings(from,until).size(),1);QCOMPARE(db.amounts(from,until).size(),1);QCOMPARE(server.requests.size(),4);
    const auto revision=db.revision();importer.start(from,until,"synthetic-network");QVERIFY(spy.wait(10000));QVERIFY(spy.last()[0].toBool());
    QCOMPARE(db.readings(from,until).size(),1);QCOMPARE(db.amounts(from,until).size(),1);QCOMPARE(db.revision(),revision);QCOMPARE(server.requests.size(),8);
    for(const auto&r:server.requests){QVERIFY(!r.contains(testCredential()));QVERIFY(r.contains("api-secret: "));}
    QCOMPARE(db.query("SELECT count(*) AS n FROM glucose")[0].toObject()["n"].toInt(),3); // distinct stream/history/scans provenance
    QCOMPARE(db.amounts(from,until)[0].toObject()["label"].toString(),QString::fromUtf8("LAB'); DROP TABLE glucose;--"));
 }
 void unauthorized_data(){QTest::addColumn<bool>("missing");QTest::newRow("missing")<<true;QTest::newRow("wrong")<<false;}
 void unauthorized(){
    QFETCH(bool,missing);QTemporaryDir dir;FixtureStore db(dir.path()+"/test.sqlite3");Server server;QVERIFY(server.listen(QHostAddress::LocalHost));
    server.respond=[](auto){return Server::response({},403);};auto c=server.connection();if(missing)c.secret.clear();else c.secret="LAB_WRONG";
    Transport t;auto r=get(t,c);QVERIFY(!r.error.isEmpty());QVERIFY(r.body.isEmpty());QCOMPARE(server.requests.size(),missing?0:1);
    QCOMPARE(db.readings(from,until).size(),0);QVERIFY(!r.error.contains(c.secret)||c.secret.isEmpty());
 }
 void framing_data(){
    QTest::addColumn<QByteArray>("wire");
    QTest::newRow("truncated")<<QByteArray("HTTP/1.0 200 OK\r\nContent-Length: 100\r\n\r\nx");
    QTest::newRow("too-long")<<QByteArray("HTTP/1.0 200 OK\r\nContent-Length: 1\r\n\r\nxx");
    QTest::newRow("chunked")<<QByteArray("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\n\r\n");
 }
 void framing(){QFETCH(QByteArray,wire);Server server;QVERIFY(server.listen(QHostAddress::LocalHost));server.respond=[wire](auto){return wire;};Transport t;auto r=get(t,server.connection());QVERIFY(!r.error.isEmpty());QVERIFY(r.body.isEmpty());QVERIFY(!t.busy);}
 void redirect(){
    Server sink,server;QVERIFY(sink.listen(QHostAddress::LocalHost));QVERIFY(server.listen(QHostAddress::LocalHost));
    server.respond=[&](auto){return "HTTP/1.0 302 Found\r\nLocation: http://127.0.0.1:"+QByteArray::number(sink.serverPort())+"/x/stream\r\nContent-Length: 0\r\n\r\n";};
    Transport t;auto r=get(t,server.connection());QVERIFY(!r.error.isEmpty());QCOMPARE(sink.requests.size(),0);
 }
 void size_data(){QTest::addColumn<int>("bodySize");QTest::newRow("body-at-limit")<<64*1024*1024;QTest::newRow("body-over-limit")<<(64*1024*1024+1);}
 void size(){
    QFETCH(int,bodySize);Server server;QVERIFY(server.listen(QHostAddress::LocalHost));server.respond=[bodySize](auto){return Server::response(QByteArray(bodySize,'x'));};
    Transport t;auto r=get(t,server.connection());
    // Transport caps the entire HTTP response, including its headers.
    QVERIFY(!r.error.isEmpty());QVERIFY(r.body.isEmpty());QVERIFY(!t.busy);
    if(bodySize==64*1024*1024)qInfo("FINDING B07: body at advertised 64MiB limit rejected because headers count toward transport cap");
 }
 void slow(){Server server;QVERIFY(server.listen(QHostAddress::LocalHost));Transport t;auto r=get(t,server.connection());QVERIFY(!r.error.isEmpty());QVERIFY(r.elapsed>=18000&&r.elapsed<25000);QVERIFY(!t.busy);}
 void malformedAndRecovery(){
    QTemporaryDir dir;FixtureStore db(dir.path()+"/test.sqlite3");Server server;QVERIFY(server.listen(QHostAddress::LocalHost));Transport t;auto c=server.connection();
    const QList<QByteArray> bad={QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\nLAB\t1\tbad\t6\n"),QByteArray("Sensorid\tnr\tUnixTime\tunit\nLAB\t1\t1760000100\t6\n"),QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\nLAB\t1\t1760000100\tnan\n")};
    for(const auto&bytes:bad){server.respond=[bytes](auto){return Server::response(bytes);};auto r=get(t,c);QVERIFY(r.error.isEmpty());
        const auto id=db.beginImport(from,until,"synthetic");QVERIFY(!db.importChunk(id,"stream",r.body,from,until));QCOMPARE(db.readings(from,until).size(),0);}
    server.respond=[](const QByteArray&r){return Server::response(r.startsWith("GET /x/amounts")?amounts:glucose);};
    Importer importer(db,[&](const QString&p,auto done){t.get(c,p,done);});QSignalSpy spy(&importer,&Importer::finished);importer.start(from,until,"synthetic");QVERIFY(spy.wait(10000));QVERIFY(spy.last()[0].toBool());QCOMPARE(db.readings(from,until).size(),1);
 }
 void networkErrorRecoveryAndAtomicPiece(){
    QTemporaryDir dir;FixtureStore db(dir.path()+"/test.sqlite3");Server server;QVERIFY(server.listen(QHostAddress::LocalHost));Transport t;auto c=server.connection();
    server.respond=[](auto){return QByteArray("HTTP/1.0 200 OK\r\nContent-Length: 100\r\n\r\nx");};
    auto bad=get(t,c);QVERIFY(!bad.error.isEmpty());QVERIFY(bad.body.isEmpty());QVERIFY(!t.busy);
    server.respond=[](auto){return Server::response(glucose);};auto recovered=get(t,c);QVERIFY(recovered.error.isEmpty());QCOMPARE(recovered.body,glucose);
    auto id=db.beginImport(from,until,"synthetic");QVERIFY(db.importChunk(id,"stream",recovered.body,from,until));
    auto mixed=glucose;mixed.replace("\t1\t","\t2\t");mixed+="LAB\t3\tbad-time\t6\n";
    id=db.beginImport(from,until,"synthetic");QVERIFY(!db.importChunk(id,"stream",mixed,from,until));
    QCOMPARE(db.readings(from,until).size(),1);QCOMPARE(db.query("SELECT count(*) AS n FROM import_chunks")[0].toObject()["n"].toInt(),1);
 }
 void credibleFalseSourceAndStale(){
    QTemporaryDir dir;FixtureStore db(dir.path()+"/test.sqlite3");auto bytes=glucose;bytes.replace("\t6\n","\t7\n");const auto id=db.beginImport(from,until,"synthetic");QVERIFY(db.importChunk(id,"stream",bytes,from,until));
    QCOMPARE(db.readings(from,until)[0].mmol,7.0);QCOMPARE(db.readings(from,until)[0].seriesKind,QString("stream"));
    auto current=currentGlucose(db.readings(from,until),{},until+3600000,QTimeZone("UTC"),4,10);QVERIFY(current["stale"].toBool());
    qInfo("EXPECTED-LIMIT B10: plausible false source data accepted; authenticated transport cannot establish measurement truth");
 }
};
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);
 if(app.arguments().contains("--probe")){
    auto i=app.arguments().indexOf("--probe");if(i+1>=argc)return 2;Connection c;c.mode="network";c.address=app.arguments()[i+1];c.secret=QString::fromLatin1(testCredential());
    if(app.arguments().contains("--require-wireguard")) {
        auto index=app.arguments().indexOf("--require-wireguard");if(index+1>=argc)return 2;
        c.mode="wireguard";c.wireguardInterface=app.arguments()[index+1];
    }
    if(app.arguments().contains("--test-proxy"))QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,"127.0.0.1",9));
    if(app.arguments().contains("--test-ca")) {
        auto index=app.arguments().indexOf("--test-ca");if(index+1>=argc)return 2;
        QFile certificate(app.arguments()[index+1]);if(!certificate.open(QIODevice::ReadOnly))return 2;
        auto configuration=QSslConfiguration::defaultConfiguration();configuration.setCaCertificates(QSslCertificate::fromData(certificate.readAll()));QSslConfiguration::setDefaultConfiguration(configuration);
    }
    Transport t;auto r=get(t,c);
    QJsonObject o{{"ok",r.error.isEmpty()},{"privileged",geteuid()==0},{"bytes",double(r.body.size())},{"elapsed_ms",double(r.elapsed)},{"credential_in_error",r.error.contains(c.secret)},{"body_sha256",QString::fromLatin1(QCryptographicHash::hash(r.body,QCryptographicHash::Sha256).toHex())}};
    std::cout<<QJsonDocument(o).toJson(QJsonDocument::Compact).constData()<<std::endl;return r.error.isEmpty()?0:1;
 }
 SecurityTest test;return QTest::qExec(&test,argc,argv);
}
#include "test_security.moc"
