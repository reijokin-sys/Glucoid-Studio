// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "nightscout.h"
#include <QFile>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopeGuard>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

using namespace Studio;
namespace {
constexpr qint64 instant=1760000000000LL;
QJsonObject glucose(QString id="g1",qint64 modified=instant,double value=108) {
    return {{"identifier",id},{"srvModified",double(modified)},{"date",double(instant)},
        {"type","sgv"},{"sgv",value},{"units","mg/dl"},{"device","synthetic"}};
}
QJsonObject treatment(QString id="t1",qint64 modified=instant) {
    return {{"identifier",id},{"srvModified",double(modified)},{"date",double(instant)},
        {"eventType","Meal Bolus"},{"carbs",20},{"insulin",2}};
}
QByteArray page(const QJsonArray &rows) {
    return QJsonDocument(QJsonObject{{"status",200},{"result",rows}}).toJson(QJsonDocument::Compact);
}
class Server : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslKey key;
    bool tls=false;
    QList<QByteArray> requests;
    std::function<QByteArray(QByteArray)> respond;
    QString address() const { return QString(tls?"https://localhost:":"http://127.0.0.1:")+QString::number(serverPort())+"/prefix"; }
    static QByteArray response(QByteArray body,int status=200) {
        return "HTTP/1.1 "+QByteArray::number(status)+" response\r\nContent-Length: "
            +QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;
    }
protected:
    void incomingConnection(qintptr descriptor) override {
        auto *socket=new QSslSocket(this);
        socket->setSocketDescriptor(descriptor);
        auto bytes=std::make_shared<QByteArray>();
        connect(socket,&QAbstractSocket::disconnected,socket,&QObject::deleteLater);
        connect(socket,&QIODevice::readyRead,this,[this,socket,bytes] {
            bytes->append(socket->readAll());
            if(!bytes->contains("\r\n\r\n"))return;
            requests.append(*bytes);
            socket->write(respond?respond(*bytes):response("{}",500));
            socket->disconnectFromHost();
        });
        if(tls){socket->setLocalCertificate(certificate);socket->setPrivateKey(key);socket->startServerEncryption();}
    }
};
QByteArray normalResponse(const QByteArray &request,const QJsonArray &entries,const QJsonArray &events) {
    if(request.startsWith("GET /prefix/api/v2/authorization/request/"))
        return Server::response("{\"token\":\"synthetic.jwt.signature\"}");
    const auto target=QString::fromUtf8(request.split(' ').value(1));
    const QUrl url(target);const QUrlQuery query(url);
    const auto cursor=url.path().section('/',-1).toLongLong();
    const int limit=query.queryItemValue("limit").toInt();
    QJsonArray output;
    const auto rows=target.contains("/entries/")?entries:events;
    for(const auto &value:rows){
        if(value.toObject()["srvModified"].toDouble()>cursor&&output.size()<limit)output.append(value);
    }
    return Server::response(page(output));
}
}

class NightscoutTest : public QObject {
    Q_OBJECT
private slots:
    void validationAndPrivateDatasets() {
        QVERIFY(nightscoutAddressError("https://nightscout.example/base").isEmpty());
        for(const auto &url:{"http://nightscout.example","https://user:credential@nightscout.example",
            "https://nightscout.example?token=placeholder","https://nightscout.example#fragment","file:///tmp/test"})
            QVERIFY(!nightscoutAddressError(url).isEmpty());
        QCOMPARE(nightscoutDatasetPath("root","https://nightscout.example/base"),
            nightscoutDatasetPath("root","https://nightscout.example/base/"));
        QCOMPARE(nightscoutDatasetPath("root","https://nightscout.example:443/base/"),
            nightscoutDatasetPath("root","https://nightscout.example/base/"));
        QVERIFY(nightscoutDatasetPath("root","https://one.example")!=nightscoutDatasetPath("root","https://two.example"));
    }
    void typedRecordsEditsTombstonesAndRollback() {
        QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");
        const auto run=store.beginImport(0,instant+1,"nightscout");QVERIFY(!run.isEmpty());
        QVERIFY(store.importNightscoutPage(run,"entries",page({glucose()}),instant-1));
        QCOMPARE(store.readings(0,instant+1).first().mmol,6.0);
        QVERIFY(store.importNightscoutPage(run,"treatments",page({treatment()}),instant-1));
        auto rows=store.amounts(0,instant+1);QCOMPARE(rows.size(),2);
        QSet<QString> kinds;for(auto v:rows)kinds.insert(v.toObject()["kind"].toString());
        QCOMPARE(kinds,QSet<QString>({"carbs","rapid"}));
        const auto revision=store.revision();
        QVERIFY(store.importNightscoutPage(run,"treatments",page({treatment()}),instant-1));
        QCOMPARE(store.revision(),revision);
        auto changed=treatment();changed["insulin"]=3;changed["srvModified"]=double(instant+1);
        QVERIFY(store.importNightscoutPage(run,"treatments",page({changed}),instant));
        QVERIFY(store.revision()!=revision);QCOMPARE(store.amounts(0,instant+1).size(),2);
        QCOMPARE(store.query("SELECT value FROM amounts WHERE kind='rapid'")[0].toObject()["value"].toDouble(),3.0);
        const auto cursor=store.meta("nightscoutCursor:treatments");
        auto bad=treatment("bad");bad["carbs"]=-1;
        QVERIFY(!store.importNightscoutPage(run,"treatments",page({treatment("not-committed"),bad}),instant+5));
        QCOMPARE(store.meta("nightscoutCursor:treatments"),cursor);
        QCOMPARE(store.amounts(0,instant+1).size(),2);
        const QJsonObject deleted{{"identifier","t1"},{"srvModified",double(instant+2)},{"isValid",false}};
        QVERIFY(store.importNightscoutPage(run,"treatments",page({deleted}),instant+1));
        QVERIFY(store.amounts(0,instant+1).isEmpty());
        QCOMPARE(store.query("SELECT count(*) AS n FROM amounts")[0].toObject()["n"].toInt(),2);
        const QJsonObject removedGlucose{{"identifier","g1"},{"srvModified",double(instant+2)},{"isValid",false}};
        QVERIFY(store.importNightscoutPage(run,"entries",page({removedGlucose}),instant+1));
        QVERIFY(!store.readings(0,instant+1).first().reliable);
    }
    void unitsMissingValuesAndUnsupportedDoses() {
        QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");
        const auto run=store.beginImport(0,instant+1,"nightscout");
        auto mmol=glucose();mmol["units"]="mmol/l";mmol["sgv"]=7;
        QVERIFY(store.importNightscoutPage(run,"entries",page({mmol}),instant-1));
        QCOMPARE(store.readings(0,instant+1).first().mmol,7.0);
        auto unknown=glucose("unknown");unknown["units"]="unknown";
        QVERIFY(!store.importNightscoutPage(run,"entries",page({unknown}),instant));
        auto carbsOnly=treatment("carbs");carbsOnly.remove("insulin");
        auto zero=treatment("zero");zero["insulin"]=0;
        auto pump=treatment("pump");pump["eventType"]="Temp Basal";pump["absolute"]=1.2;
        auto basal=treatment("basal");basal["eventType"]="Basal Insulin Dose";basal.remove("carbs");
        auto combo=treatment("combo");combo["eventType"]="Combo Bolus";combo["duration"]=60;
        QVERIFY(store.importNightscoutPage(run,"treatments",page({carbsOnly,zero,pump,basal,combo}),instant-1));
        QCOMPARE(store.query("SELECT count(*) AS n FROM amounts WHERE kind IN ('rapid','correction')")[0].toObject()["n"].toInt(),1);
        QCOMPARE(store.query("SELECT value FROM amounts WHERE kind='rapid'")[0].toObject()["value"].toDouble(),0.0);
        QCOMPARE(store.query("SELECT count(*) AS n FROM amounts WHERE kind='basal'")[0].toObject()["n"].toInt(),1);
        QCOMPARE(store.query("SELECT count(*) AS n FROM meta WHERE key GLOB 'nightscoutUnsupported:*'")[0].toObject()["n"].toInt(),2);
        QVERIFY(store.mappings().isEmpty());
    }
    void snapshotReconciliationPreservesLocalAndOutOfWindow() {
        QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");
        const auto run=store.beginImport(0,instant+1,"nightscout");
        QVERIFY(store.importNightscoutPage(run,"treatments",page({treatment()}),instant-1));
        const auto local=store.addLocal(instant,1,"carbs","","synthetic");QVERIFY(!local.isEmpty());
        QVERIFY(store.reconcileNightscout("treatments",0,{}));
        QCOMPARE(store.amounts(0,instant+1).size(),1);
        QCOMPARE(store.amounts(0,instant+1)[0].toObject()["id"].toString(),local);
        QVERIFY(store.importNightscoutPage(run,"entries",page({glucose()}),instant-1));
        QVERIFY(store.reconcileNightscout("entries",instant+1,{}));
        QVERIFY(store.readings(0,instant+1).first().reliable);
        QVERIFY(store.reconcileNightscout("entries",0,{}));
        QVERIFY(!store.readings(0,instant+1).first().reliable);
    }
    void pagedHistoryWithTimestampTiesAndAuthenticatedPrefix() {
        QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QJsonArray entries;
        for(int i=0;i<1005;++i)entries.append(glucose(QString::number(i),instant));
        entries.append(glucose("last",instant+1));
        server.respond=[&](QByteArray request){return normalResponse(request,entries,{treatment()});};
        NightscoutImporter importer(store,server.address(),"synthetic-access");
        QSignalSpy done(&importer,&NightscoutImporter::finished);importer.start();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(store.readings(0,instant+1).size(),1006);
        QCOMPARE(store.amounts(0,instant+1).size(),2);
        QVERIFY(server.requests.size()>=5);
        bool widened=false;
        for(const auto &request:server.requests){
            QVERIFY(request.startsWith("GET /prefix/api/"));
            if(request.contains("/api/v3/")){
                QVERIFY(request.contains("Authorization: Bearer synthetic.jwt.signature"));
                QVERIFY(!request.contains("synthetic-access"));
                QVERIFY(!request.contains("token="));
            }
            widened|=request.contains("limit=2000");
        }
        QVERIFY(widened);
        const auto revision=store.revision();
        NightscoutImporter again(store,server.address(),"synthetic-access");QSignalSpy repeated(&again,&NightscoutImporter::finished);
        again.start();QTRY_COMPARE_WITH_TIMEOUT(repeated.size(),1,10000);QVERIFY(repeated[0][0].toBool());
        QCOMPARE(store.revision(),revision);
    }
    void failureDoesNotAdvanceCursorAndNextImportRecovers() {
        QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost));bool broken=true;
        server.respond=[&](QByteArray request){
            if(request.contains("/treatments/")&&broken)return Server::response("not JSON");
            return normalResponse(request,{glucose()},{treatment()});
        };
        NightscoutImporter first(store,server.address(),"synthetic-access");QSignalSpy failed(&first,&NightscoutImporter::finished);
        first.start();QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,10000);QVERIFY(!failed[0][0].toBool());
        QVERIFY(store.meta("nightscoutCursor:treatments").isEmpty());
        QCOMPARE(store.readings(0,instant+1).size(),1);
        broken=false;
        NightscoutImporter second(store,server.address(),"synthetic-access");QSignalSpy done(&second,&NightscoutImporter::finished);
        second.start();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done[0][0].toBool());
        QCOMPARE(store.amounts(0,instant+1).size(),2);
    }
    void redirectsAndUnauthorizedResponsesDoNotLeakCredentials_data() {
        QTest::addColumn<int>("status");QTest::newRow("redirect")<<302;QTest::newRow("unauthorized")<<403;
    }
    void redirectsAndUnauthorizedResponsesDoNotLeakCredentials() {
        QFETCH(int,status);QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");Server server,other;
        QVERIFY(server.listen(QHostAddress::LocalHost));QVERIFY(other.listen(QHostAddress::LocalHost));
        server.respond=[&](QByteArray){
            if(status==302)return QByteArray("HTTP/1.1 302 Found\r\nLocation: ")+other.address().toUtf8()+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            return Server::response("sensitive-server-error-body",status);
        };
        NightscoutImporter importer(store,server.address(),"synthetic-access");QSignalSpy done(&importer,&NightscoutImporter::finished);
        importer.start();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(!done[0][0].toBool());
        QVERIFY(other.requests.isEmpty());QCOMPARE(server.requests.size(),1);
        QVERIFY(!done[0][1].toString().contains("synthetic-access"));
        QVERIFY(!done[0][1].toString().contains("sensitive-server-error-body"));
    }
    void httpsWithTestCaWorksWithoutWireGuardAndRejectsUntrustedCertificate() {
        QTemporaryDir dir;QProcess openssl;
        openssl.start(QStringLiteral(OPENSSL_TEST_EXECUTABLE),{"req","-x509","-newkey","rsa:2048","-nodes","-days","2","-keyout",dir.path()+"/key.pem",
            "-out",dir.path()+"/cert.pem","-subj","/CN=localhost","-addext","subjectAltName=DNS:localhost,IP:127.0.0.1"});
        QVERIFY(openssl.waitForFinished(10000));QCOMPARE(openssl.exitCode(),0);
        QFile certificateFile(dir.path()+"/cert.pem"),keyFile(dir.path()+"/key.pem");
        QVERIFY(certificateFile.open(QIODevice::ReadOnly));QVERIFY(keyFile.open(QIODevice::ReadOnly));
        Server server;server.tls=true;server.certificate=QSslCertificate(certificateFile.readAll());server.key=QSslKey(keyFile.readAll(),QSsl::Rsa);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond=[](QByteArray request){return normalResponse(request,{glucose()},{treatment()});};
        Store untrusted(dir.path()+"/untrusted.sqlite3");
        NightscoutImporter denied(untrusted,server.address(),"synthetic-access");QSignalSpy rejected(&denied,&NightscoutImporter::finished);
        denied.start();QTRY_COMPARE_WITH_TIMEOUT(rejected.size(),1,12000);QVERIFY(!rejected[0][0].toBool());
        QVERIFY(server.requests.isEmpty());
        const auto previous=QSslConfiguration::defaultConfiguration();
        auto restore=qScopeGuard([&]{QSslConfiguration::setDefaultConfiguration(previous);});
        auto trusted=previous;trusted.setCaCertificates(previous.caCertificates()+QList<QSslCertificate>{server.certificate});
        QSslConfiguration::setDefaultConfiguration(trusted);
        Store accepted(dir.path()+"/accepted.sqlite3");
        NightscoutImporter importer(accepted,server.address(),"synthetic-access");QSignalSpy done(&importer,&NightscoutImporter::finished);
        importer.start();QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(accepted.readings(0,instant+1).size(),1);QCOMPARE(accepted.amounts(0,instant+1).size(),2);
    }
    void qmlSelectsOneSourceAndPreservesJugglucoHistory() {
        QTemporaryDir dir;
        {Store store(dir.path()+"/studio.sqlite3");Reading r;r.key="juggluco-kept";r.time=instant;r.mmol=6;
            QVERIFY(store.ingest({r},{},instant-1,instant+1,false));}
        Backend backend(dir.path());
        QVERIFY(backend.saveSourceConnection("juggluco","network","http://127.0.0.1:1","none","",""));
        QCOMPARE(backend.connection()["mode"].toString(),QString("wireguard"));
        QCOMPARE(backend.connection()["wireguardInterface"].toString(),QString("wg0"));
        QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QStringLiteral(STUDIO_SOURCE_DIR)+"/qml/ConnectionEditor.qml"));
        std::unique_ptr<QObject> editor(component.createWithInitialProperties({{"backend",QVariant::fromValue(&backend)}}));
        QVERIFY2(editor,qPrintable(component.errorString()));
        editor->setProperty("width",640);
        auto *source=editor->findChild<QObject*>("connectionSource"),*address=editor->findChild<QObject*>("connectionAddress");
        auto *button=editor->findChild<QObject*>("saveSourceConnection");QVERIFY(source&&address&&button);
        source->setProperty("currentIndex",1);QVERIFY(QMetaObject::invokeMethod(source,"activated",Q_ARG(int,1)));
        address->setProperty("text","https://nightscout.example");
        QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        QCOMPARE(backend.connection()["source"].toString(),QString("nightscout"));
        {Store selected(nightscoutDatasetPath(dir.path(),"https://nightscout.example"),true);QVERIFY(selected.readings(0,instant+1).isEmpty());}
        QVERIFY(!backend.importFolder(dir.path()));
        source->setProperty("currentIndex",0);QVERIFY(QMetaObject::invokeMethod(source,"activated",Q_ARG(int,0)));
        QCOMPARE(address->property("text").toString(),QString("http://127.0.0.1:1"));
        QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        QCOMPARE(backend.connection()["source"].toString(),QString("juggluco"));
        {Store original(dir.path()+"/studio.sqlite3",true);QCOMPARE(original.readings(0,instant+1).size(),1);}
        QVERIFY(!QFile(dir.path()+"/connection.json").exists() || !backend.connection().contains("secret"));
    }
    void nightscoutBackupRestoresSourceAndRawImports() {
        QTemporaryDir dir;Backend backend(dir.path());
        const QString address="https://nightscout.example/base";
        QVERIFY(backend.saveSourceConnection("nightscout","network",address,"token","",""));
        QCOMPARE(backend.connection()["mode"].toString(),QString("network"));
        QVERIFY(backend.connection()["wireguardInterface"].toString().isEmpty());
        {
            Store store(nightscoutDatasetPath(dir.path(),address));
            const auto run=store.beginImport(0,instant+1,"nightscout");
            QVERIFY(store.importNightscoutPage(run,"entries",page({glucose()}),instant-1));
            QVERIFY(store.importNightscoutPage(run,"treatments",page({treatment()}),instant-1));
            store.endImport(run,true,"synthetic");
        }
        const auto backup=backend.backup();QVERIFY(!backup.isEmpty());
        const auto target=dir.path()+"/restored";
        QProcess restore;restore.start("python3",{QStringLiteral(STUDIO_SOURCE_DIR)+"/tools/restore-backup.py","--backup",backup,"--data-dir",target});
        QVERIFY(restore.waitForFinished(10000));QCOMPARE(restore.exitCode(),0);
        Backend restored(target);QCOMPARE(restored.connection()["source"].toString(),QString("nightscout"));
        Store verify(nightscoutDatasetPath(target,address),true);QVERIFY(verify.good());
        QCOMPARE(verify.readings(0,instant+1).size(),1);QCOMPARE(verify.amounts(0,instant+1).size(),2);
        for(const auto &value:verify.query("SELECT path FROM import_chunks"))QVERIFY(QFile::exists(value.toObject()["path"].toString()));
    }
    void mainUiWorksWithNightscoutDataset() {
        QTemporaryDir dir;Backend backend(dir.path());
        const QString address="https://nightscout.example";
        QVERIFY(backend.saveSourceConnection("nightscout","network",address,"token","",""));
        QCOMPARE(backend.connection()["mode"].toString(),QString("network"));
        QVERIFY(backend.connection()["wireguardInterface"].toString().isEmpty());
        const auto now=QDateTime::currentMSecsSinceEpoch();
        {
            Store store(nightscoutDatasetPath(dir.path(),address));
            const auto run=store.beginImport(0,now,"nightscout");auto current=glucose();current["date"]=double(now-60000);
            QVERIFY(store.importNightscoutPage(run,"entries",page({current}),instant-1));
        }
        backend.refresh();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("studio",&backend);
        engine.rootContext()->setContextProperty("demoMode",true);
        engine.rootContext()->setContextProperty("uiTestMode",true);
        engine.rootContext()->setContextProperty("uiTestWidth",900);
        engine.load(QUrl::fromLocalFile(QStringLiteral(STUDIO_SOURCE_DIR)+"/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(backend.dailyAnalysis()["status"].toString(),QString("ready"),15000);
        QCOMPARE(backend.connection()["source"].toString(),QString("nightscout"));
        QVERIFY(backend.currentReading()["hasData"].toBool());
        QCOMPARE(backend.currentReading()["value"].toDouble(),6.0);
    }
    void secondWindowFollowsSelectionAndInvalidSettingsNeverFallback() {
        QTemporaryDir dir;Backend first(dir.path());
        QVERIFY(first.saveSourceConnection("juggluco","network","http://127.0.0.1:1","none","",""));
        Backend second(dir.path());
        QVERIFY(first.saveSourceConnection("nightscout","network","https://nightscout.example","token","",""));
        second.refreshSourceChoice();QCOMPARE(second.connection()["source"].toString(),QString("nightscout"));
        QLockFile lock(dir.path()+"/sync.lock");QVERIFY(lock.tryLock());
        QVERIFY(!first.saveSourceConnection("juggluco","usb","","none","",""));
        QCOMPARE(first.connection()["source"].toString(),QString("nightscout"));lock.unlock();
        QFile file(dir.path()+"/connection.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write("not JSON");file.close();
        second.refreshSourceChoice();QCOMPARE(second.connection()["source"].toString(),QString("nightscout"));
        QSignalSpy done(&second,&Backend::syncFinished);second.sync();
        QCOMPARE(done.size(),1);QVERIFY(!done[0][0].toBool());
        QCOMPARE(second.connection()["source"].toString(),QString("nightscout"));
    }
};
QTEST_MAIN(NightscoutTest)
#include "test_nightscout.moc"
