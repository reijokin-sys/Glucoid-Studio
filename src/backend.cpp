// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "daychart.h"
#include "wireguard.h"
#include <QHostAddress>
#include <KWallet>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QFileDialog>
#include <QApplication>
#include <QJsonDocument>
#include <QTimeZone>
#include <QCryptographicHash>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QScopeGuard>
#include <QtConcurrent/QtConcurrentRun>
#include <QProcess>

using namespace Studio;
// General CGM reference values, not personal treatment targets. The upper mean
// bound is the estimated average glucose of HbA1c 7%; the lower bound is the
// consensus hypoglycaemia threshold. TIR, CV and coverage follow the 2019
// international CGM consensus; GMI follows the ADA general HbA1c target.
static constexpr double referenceMeanLow=3.9,referenceMeanHigh=8.6;
static constexpr double referenceGmiTarget=7.0,referenceTirTarget=0.70;
static constexpr double referenceCvTarget=36.0,referenceCoverageTarget=0.70;
static QString encoded(const QJsonObject&o){return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));}
static QJsonObject failure(const QString&why){return {{"status","blocked"},{"reasons",QJsonArray{why}},{"researchOnly",true}};}
namespace {
Connection readConnection(const QString &path) {
    QFile file(path); Connection result;
    if (!file.open(QIODevice::ReadOnly)) return result;
    const auto object=QJsonDocument::fromJson(file.readAll()).object();
    result.source=object["source"].toString("juggluco");
    if(result.source!="juggluco"&&result.source!="nightscout")result.source="juggluco";
    result.mode=object["mode"].toString("usb");result.address=object["address"].toString();
    result.auth=object["auth"].toString("apisecret");result.serial=object["serial"].toString();
    if (result.source=="juggluco" && result.mode!="usb") {
        result.mode="wireguard";
        result.wireguardInterface=object["wireguardInterface"].toString("wg0");
    } else result.wireguardInterface.clear();
    return result;
}
QString sourceDatabase(const QString &root,const Connection &connection) {
    return connection.source=="nightscout" ? nightscoutDatasetPath(root,connection.address) : root+"/studio.sqlite3";
}
QString walletKey(const Connection &connection) {
    return connection.source=="nightscout"
        ? QFileInfo(nightscoutDatasetPath({},connection.address)).dir().dirName() : QString("juggluco");
}
QJsonObject connectionObject(const Connection &connection) {
    return {{"source",connection.source},{"mode",connection.mode},{"address",connection.address},
        {"auth",connection.auth},{"serial",connection.serial},{"wireguardInterface",connection.wireguardInterface}};
}
bool saveConnectionFile(const QString &path,const Connection &connection) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    const auto bytes=QJsonDocument(connectionObject(connection)).toJson();
    return file.write(bytes)==bytes.size() && file.commit();
}
}
Backend::Backend(const QString&path,QObject*parent,Importer::Fetch fetch):QObject(parent),root(path),db(sourceDatabase(path,readConnection(path+"/connection.json"))),transport(this),jugglucoFetch(std::move(fetch)){
    config=readConnection(root+"/connection.json");
    m_status=db.good()?"Valmis. Tutkimustila — laskentamalleja ei ole kliinisesti validoitu.":db.error();refresh();
    connect(&dailyWatcher,&QFutureWatcher<QJsonObject>::finished,this,[this]{
        auto result=dailyWatcher.result();dailyRunning=false;
        const bool current=runningDailyKey==dailyRequestedKey && (result["status"]=="error"||result["sourceRevision"].toString()==db.revision());
        if(current){result["requestKey"]=runningDailyKey;dailyResult=result;}
        if(dailyPending||!current){dailyPending=false;dailyRequestedKey.clear();requestDailyAnalysis();}
        else emit changed();
    });
    connect(&automaticWatcher,&QFutureWatcher<QJsonObject>::finished,this,[this]{
        const auto result=automaticWatcher.result();automaticRunning=false;
        const bool current=runningAutomaticGeneration==automaticGeneration&&runningAutomaticKey==automaticRequestedKey&&(result["status"]=="error"||result["sourceRevision"].toString()==db.revision());
        if(current){automaticResult=result;automaticResult["requestKey"]=runningAutomaticKey;emit changed();}
        if(automaticPending||!current){automaticPending=false;automaticRequestedKey.clear();requestAutomaticAnalysis();}
        else emit changed();
    });
}
Backend::~Backend(){dailyWatcher.waitForFinished();automaticWatcher.waitForFinished();}
void Backend::refreshDailyAnalysis(){dailyEnabled=true;requestDailyAnalysis();}
void Backend::requestDailyAnalysis(){
    if(!dailyEnabled||!db.available())return;
    const QTimeZone zone(analysisSettings().value("timezone").toString().toUtf8());
    const auto now=QDateTime::currentMSecsSinceEpoch();
    const auto date=QDateTime::fromMSecsSinceEpoch(now,zone).date().toString(Qt::ISODate);
    const auto key=db.revision()+":"+QString::fromUtf8(zone.id())+":"+date;
    if(key==dailyRequestedKey)return;
    dailyRequestedKey=key;pendingDailyZone=zone;pendingDailyNow=now;
    dailyResult={{"status","computing"}};
    if(dailyRunning)dailyPending=true;else launchDailyAnalysis();
    emit changed();
}
void Backend::launchDailyAnalysis(){
    dailyRunning=true;runningDailyKey=dailyRequestedKey;
    const auto path=db.path();const auto zone=pendingDailyZone;const auto now=pendingDailyNow;
    dailyWatcher.setFuture(QtConcurrent::run([path,zone,now]{return dailyPeriodSnapshot(path,zone,now);}));
}
QVariantMap Backend::automaticSettings()const{
    auto settings=QJsonDocument::fromJson(db.meta("automaticMealSettings").toUtf8()).object();auto p=db.observationProfile();if(p.isEmpty())p=db.activeProfile();
    if(!settings.contains("targetLow"))settings["targetLow"]=p["targetLow"];
    if(!settings.contains("targetHigh"))settings["targetHigh"]=p["targetHigh"];
    if(!settings.contains("hypo"))settings["hypo"]=p["hypo"];
    settings["timezone"]=p.value("timezone").toString(analysisSettings().value("timezone").toString());
    settings["reportedBasalHistory"]=QJsonDocument::fromJson(db.meta("reportedBasalHistory").toUtf8()).array();
    if(!settings.contains("periodDays"))settings["periodDays"]=56;
    if(!settings.contains("groups")&&p["slots"].toArray().size()==4){QJsonArray groups;for(auto v:p["slots"].toArray()){auto s=v.toObject();groups<<QJsonObject{{"name",s["name"]},{"startMinute",s["startMinute"]}};}settings["groups"]=groups;}
    return settings.toVariantMap();
}
void Backend::startAutomaticAnalysis(bool refreshSource){
    automaticEnabled=true;requestAutomaticAnalysis();
    if(refreshSource&&!automaticSourceStarted&&!db.meta("connectionConfigured").isEmpty()){
        automaticSourceStarted=true;QTimer::singleShot(1000,this,[this]{if(!m_busy)sync();});
    }
}
void Backend::requestAutomaticAnalysis(){
    if(!automaticEnabled)return;
    auto settings=QJsonObject::fromVariantMap(automaticSettings());const auto zone=QTimeZone(settings["timezone"].toString().toUtf8());
    auto last=QDate::fromString(m_analysis.value("lastDate").toString(),Qt::ISODate);if(!last.isValid())last=QDateTime::currentDateTime(zone).date().addDays(-1);
    const int days=settings["periodDays"].toInt(56);const auto to=QDateTime(last.addDays(1),QTime(0,0),zone),from=to.addDays(-days);
    const QString key=QString::fromLatin1(QCryptographicHash::hash(db.revision().toUtf8()+QJsonDocument(db.profiles()).toJson(QJsonDocument::Compact)+QJsonDocument(settings).toJson(QJsonDocument::Compact)+QByteArray::number(to.toMSecsSinceEpoch()),QCryptographicHash::Sha256).toHex());
    if(key==automaticRequestedKey)return;
    automaticRequestedKey=key;++automaticGeneration;pendingAutomaticSettings=settings;pendingAutomaticFrom=from.toMSecsSinceEpoch();pendingAutomaticTo=to.toMSecsSinceEpoch();pendingAutomaticUntil=QDateTime::currentMSecsSinceEpoch();
    automaticResult={{"status","computing"}};
    if(automaticRunning)automaticPending=true;else launchAutomaticAnalysis();
    emit changed();
}
void Backend::launchAutomaticAnalysis(){
    automaticRunning=true;runningAutomaticGeneration=automaticGeneration;runningAutomaticKey=automaticRequestedKey;
    const auto path=db.path();const auto settings=pendingAutomaticSettings;const auto from=pendingAutomaticFrom,to=pendingAutomaticTo,until=pendingAutomaticUntil;
    automaticWatcher.setFuture(QtConcurrent::run([path,settings,from,to,until]{return automaticMealSnapshot(path,settings,from,to,until);}));
}
void Backend::setAutomaticPeriod(int days){
    if(days!=28&&days!=56&&days!=84)return;
    auto settings=QJsonDocument::fromJson(db.meta("automaticMealSettings").toUtf8()).object();settings["periodDays"]=days;
    if(!db.setMeta("automaticMealSettings",encoded(settings)))return;
    requestAutomaticAnalysis();emit changed();
}
bool Backend::saveAutomaticTargets(double low,double high,double hypo){
    if(!std::isfinite(low)||!std::isfinite(high)||!std::isfinite(hypo)||hypo<=0||low<hypo||high<=low){m_status="Tarkista tavoitealue ja matalan raja.";emit changed();return false;}
    auto settings=QJsonDocument::fromJson(db.meta("automaticMealSettings").toUtf8()).object();settings["targetLow"]=low;settings["targetHigh"]=high;settings["hypo"]=hypo;
    bool ok=db.setMeta("automaticMealSettings",encoded(settings));if(ok){m_status="Yö- ja tutkimusanalyysin rajat tallennettu.";requestAutomaticAnalysis();}emit changed();return ok;
}
void Backend::openResearchView(){QProcess::startDetached(QCoreApplication::applicationFilePath(),{"--research","--data-dir",root});}
QVariantMap Backend::profile()const{auto p=evaluationProfile();return (p.isEmpty()?emptyProfile():p).toVariantMap();}
QVariantList Backend::treatmentSlots()const{auto p=evaluationProfile();return p["slots"].toArray().toVariantList();}
QVariantMap Backend::connection()const{
    auto result=connectionObject(config).toVariantMap();result["configured"]=!db.meta("connectionConfigured").isEmpty();
    const auto warnings=config.source=="nightscout"?db.query("SELECT count(*) AS n FROM meta WHERE key GLOB 'nightscoutUnsupported:*'"):QJsonArray();
    result["unsupportedEvents"]=warnings.isEmpty()?0:warnings[0].toObject()["n"].toInt();return result;
}
QVariantMap Backend::savedConnection(const QString &source)const{
    if(source==config.source)return connection();
    if(source!="juggluco"&&source!="nightscout")return {};
    const QString file=root+"/connection-"+source+".json";
    if(QFile::exists(file))return connectionObject(readConnection(file)).toVariantMap();
    Connection empty;empty.source=source;if(source=="nightscout"){empty.mode="network";empty.auth="token";}
    return connectionObject(empty).toVariantMap();
}
QString Backend::secret(bool prompt){
    if(config.auth=="none")return {};
    const auto name=KWallet::Wallet::NetworkWallet();
    if(!prompt&&!KWallet::Wallet::isOpen(name))return {};
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(name,0,KWallet::Wallet::Synchronous));
    if(!wallet||!wallet->setFolder("GlucoidStudio"))return {};
    QString value;if(wallet->readPassword(walletKey(config),value)!=0)return {};return value;
}
bool Backend::writeSecret(const QString&value,const QString &key){
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(),0,KWallet::Wallet::Synchronous));
    if(!wallet)return false;
    if(!wallet->hasFolder("GlucoidStudio")&&!wallet->createFolder("GlucoidStudio"))return false;
    return wallet->setFolder("GlucoidStudio")&&wallet->writePassword(key.isEmpty()?walletKey(config):key,value)==0;
}
bool Backend::writeConnection(){return saveConnectionFile(root+"/connection.json",config);}
void Backend::saveConnection(const QString&mode,const QString&address,const QString&auth,const QString&token,const QString&serial){
    saveSourceConnection("juggluco",mode,address,auth,token,serial);
}
void Backend::clearSourceResults(){
    dailyRequestedKey.clear();runningDailyKey.clear();dailyResult={};dailyPending=false;
    automaticRequestedKey.clear();runningAutomaticKey.clear();automaticResult={};automaticPending=false;++automaticGeneration;
    responseRevision.clear();responseResult={};responseDraft={};lastRecommendation={};selectedProfileVersion=0;
    dateWindow.clear();analysisLastDate.clear();
}
bool Backend::saveSourceConnection(const QString &source,const QString &mode,const QString &address,
    const QString &auth,const QString &token,const QString &serial,const QString &wireguardInterface){
    auto fail=[this](const QString &why){m_status=why;emit changed();return false;};
    if(m_busy||dailyRunning||automaticRunning)return fail("Odota käynnissä olevan haun tai analyysin valmistumista.");
    if(source!="juggluco"&&source!="nightscout")return fail("Tuntematon tietolähde.");
    Connection next;next.source=source;next.mode=mode;next.address=address.trimmed();next.auth=auth;next.serial=serial;
    if(source=="nightscout"){
        const auto error=nightscoutAddressError(next.address);
        if(!error.isEmpty())return fail(error);
        if(mode!="network"||auth!="token")return fail("Nightscout käyttää HTTPS-verkkoyhteyttä ja lukutunnusta.");
        next.serial.clear();next.wireguardInterface.clear();
    }else{
        if((mode!="usb"&&mode!="network"&&mode!="wireguard")||!QStringList{"apisecret","token","none"}.contains(auth))return fail("Tuntematon yhteystapa.");
        const QUrl url(next.address);
        if(mode!="usb"&&(!url.isValid()||url.host().isEmpty()||!QStringList{"http","https"}.contains(url.scheme())||!url.userInfo().isEmpty()||url.hasQuery()||url.hasFragment()||(!url.path().isEmpty()&&url.path()!="/")))
            return fail("Anna Jugglucon juuriosoite ilman tunnuksia.");
        next.wireguardInterface.clear();
        if(mode!="usb") {
            next.mode="wireguard";next.wireguardInterface=wireguardInterface.trimmed();
            const auto error=wireguardNameError(next.wireguardInterface);
            if(!error.isEmpty())return fail(error);
            QHostAddress target;
            if(!target.setAddress(url.host()) || !target.scopeId().isEmpty())
                return fail("Anna puhelimen numeerinen WireGuard-tunneliosoite.");
        }
    }
    QLockFile lock(root+"/sync.lock");if(!lock.tryLock(0))return fail("Toinen tuonti on käynnissä.");
    if(auth!="none"&&!token.isEmpty()&&!writeSecret(token,walletKey(next)))return fail("Tunnuksen tallennus avainrenkaaseen epäonnistui.");
    const auto previous=config;const auto previousPath=db.path();
    if(!saveConnectionFile(root+"/connection-"+previous.source+".json",previous))return fail("Aiemman lähdeasetuksen säilyttäminen epäonnistui.");
    if(!db.switchDatabase(sourceDatabase(root,next)))return fail("Valitun lähteen paikallisen aineiston avaaminen epäonnistui.");
    config=next;
    if(!db.setMeta("connectionConfigured","1")||!saveConnectionFile(root+"/connection-"+source+".json",config)||!writeConnection()){
        config=previous;db.switchDatabase(previousPath);return fail("Lähdeasetuksen tallentaminen epäonnistui. Aiempi valinta säilyi.");
    }
    clearSourceResults();m_status="Tietolähde tallennettu. Vain valittu lähde hakee tietoja; aiemman lähteen historia säilyi.";
    refresh();return true;
}
bool Backend::reloadSource(){
    QFile active(root+"/connection.json");
    if(!active.open(QIODevice::ReadOnly))return false;
    const auto document=QJsonDocument::fromJson(active.readAll());
    const auto source=document.object()["source"].toString("juggluco");
    if(!document.isObject()||(source!="juggluco"&&source!="nightscout"))return false;
    const auto next=readConnection(root+"/connection.json");
    if(connectionObject(next)==connectionObject(config))return true;
    if(dailyRunning||automaticRunning){m_status="Lähdeasetus muuttui. Odota analyysin valmistumista ja päivitä uudelleen.";emit changed();return false;}
    if(!db.switchDatabase(sourceDatabase(root,next))){m_status="Valitun lähteen avaaminen epäonnistui.";emit changed();return false;}
    config=next;clearSourceResults();refresh();return true;
}
void Backend::refreshSourceChoice(){
    if(m_busy||dailyRunning||automaticRunning||!QFile::exists(root+"/connection.json"))return;
    QLockFile lock(root+"/sync.lock");
    if(lock.tryLock(0))reloadSource();
}
void Backend::importGlucoid(){
    auto c=glucoidConnection();if(c.address.isEmpty()){m_status="Glucoidin yhteysasetusta ei löytynyt.";emit changed();return;}
    saveConnection("usb",c.address,c.auth,c.secret,config.serial);
}
void Backend::setWindow(const QString&date,int days){dateWindow=date;dayWindow=std::clamp(days,1,30);refresh();}
void Backend::refresh(){
    if(!responseRevision.isEmpty()&&(responseRevision!=db.revision()||responseResult["profileVersion"]!=evaluationProfile()["version"])){responseResult={};responseDraft={};}
    const qint64 now=QDateTime::currentMSecsSinceEpoch();auto p=db.activeProfile();
    const auto settings=QJsonObject::fromVariantMap(analysisSettings());
    const auto zone=QTimeZone(settings["timezone"].toString().toUtf8());
    qint64 to=now,from=now-qint64(dayWindow)*86400000;const auto selected=QDate::fromString(dateWindow,Qt::ISODate);
    if(selected.isValid()){auto start=QDateTime(selected,QTime(0,0),zone);from=start.toMSecsSinceEpoch();to=start.addDays(dayWindow).toMSecsSinceEpoch();}
    auto data=db.readings(from,to);if(settings["fillHistory"].toBool()){
        const auto selected=selectSeries(data,db.readings(from,to,"history"),from,to,true,qRound(number(settings,"maxGapMinutes")*60));data.clear();
        for(auto v:selected){Reading r;r.time=v.from;r.mmol=v.value;r.sensor=v.sensor;r.key=QString::number(v.from);data<<r;}}
    double max=15;QVariantList segments;QVariantList segment;
    const auto rules=p["rules"].toObject();const int gap=qRound(number(settings,"maxGapMinutes")*60000);Q_UNUSED(rules)
    qint64 prev=0;QString sensor;
    for(const auto&r:data){if(!r.reliable)continue;if(prev&&(r.time-prev>gap||r.sensor!=sensor)){if(!segment.isEmpty())segments.append(QVariant(segment));segment.clear();}
        segment.append(QVariantMap{{"x",double(r.time)},{"y",r.mmol}});max=std::max(max,r.mmol+2);prev=r.time;sensor=r.sensor;}
    if(!segment.isEmpty())segments.append(QVariant(segment));
    m_chart={{"from",double(from)},{"to",double(to)},{"segments",segments},{"max",max},{"low",settings["low"].toVariant()},{"high",settings["high"].toVariant()}};
    m_diary.clear();m_deletedDiary.clear();QVariantList markers;
    for(const auto&v:db.amounts(from,to,true)){
        auto o=v.toObject().toVariantMap();o["timeText"]=QDateTime::fromMSecsSinceEpoch(qint64(o["time"].toDouble()),zone).toString("dd.MM.yyyy HH:mm");
        if(o["deleted"].toBool())m_deletedDiary.prepend(o);
        else{m_diary.prepend(o);markers.append(o);}
    }
    m_chart["markers"]=markers;
    m_summaries.clear();for(int days:{7,14,30}){auto stats=metrics(db.readings(now-qint64(days)*86400000,now),now-qint64(days)*86400000,now,number(p,"targetLow"),number(p,"targetHigh"),gap/1000);stats["days"]=days;m_summaries.append(stats.toVariantMap());}
    const auto latest=db.readings(now-86400000,now+1);m_latestText="Ei tuoretta mittausta";
    if(!latest.isEmpty()){const auto&r=latest.last();const int age=int((now-r.time)/60000);m_latestText=QString("%1 mmol/l · %2 min sitten").arg(r.mmol,0,'f',1).arg(age);}
    refreshAnalysis();emit changed();
}
void Backend::sync(){startSync(false);}
void Backend::syncAll(){startSync(true);}
void Backend::startSync(bool full){
    if(m_busy)return;
    syncLock=std::make_unique<QLockFile>(root+"/sync.lock");syncLock->setStaleLockTime(120000);
    if(!syncLock->tryLock(0)){m_status="Toinen tuonti on käynnissä.";emit changed();emit syncFinished(false);return;}
    if(!reloadSource()||db.meta("connectionConfigured").isEmpty()){
        syncLock->unlock();syncLock.reset();m_status="Määritä valitun lähteen yhteys.";emit changed();emit syncFinished(false);return;
    }
    // A held lock cannot be stale while its owning process is alive.
    db.run("UPDATE import_runs SET status='partial',error='Tuontiprosessi keskeytyi' WHERE status='running'");
    m_busy=true;config.secret=secret(qobject_cast<QApplication*>(QCoreApplication::instance())!=nullptr);m_status="Aloitetaan tuonti…";emit changed();
    if(config.source=="nightscout"){
        nightscoutImporter=std::make_unique<NightscoutImporter>(db,config.address,config.secret);
        connect(nightscoutImporter.get(),&NightscoutImporter::progress,this,[this](const QString &text){m_status=text;emit changed();});
        connect(nightscoutImporter.get(),&NightscoutImporter::finished,this,[this](bool ok,const QString &text){finishSync(ok?QString():text);if(ok)backup();m_status=text;emit changed();});
        // API v3 history includes source edits and tombstones. Full sync rewinds
        // cursors without deleting local history or changing the selected source.
        if(full&&(!db.setMeta("nightscoutCursor:entries","0")||!db.setMeta("nightscoutCursor:treatments","0"))){finishSync("Historiatarkistuksen aloitus epäonnistui.");return;}
        nightscoutImporter->start();return;
    }
    Importer::Fetch fetch=jugglucoFetch ? jugglucoFetch : Importer::Fetch([this](const QString&path,auto done){transport.get(config,path,std::move(done));});
    importer=std::make_unique<Importer>(db,std::move(fetch));
    connect(importer.get(),&Importer::progress,this,[this](const QString&text){m_status=text;emit changed();});
    connect(importer.get(),&Importer::finished,this,[this](bool ok,const QString&text){finishSync(ok?QString():text);m_status=text;if(ok)backup();emit changed();});
    const auto last=db.meta("lastSourceUntil").toLongLong(),to=QDateTime::currentSecsSinceEpoch()*1000;
    const qint64 from=full||!last?0:std::max(1000LL,last-7LL*86400000);
    importer->start(from,to,config.mode);
}
void Backend::finishSync(const QString&error){m_busy=false;config.secret.clear();if(syncLock){syncLock->unlock();syncLock.reset();}
    m_status=error.isEmpty()?"Historia päivitetty. Tutkimustila.":error;
    if(!error.isEmpty())db.setMeta("connectionError",error);
    refresh();emit syncFinished(error.isEmpty());
}
bool Backend::mapLabel(const QString&label,const QString&kind,const QString&medicine){bool ok=db.mapLabel(label,kind,medicine);m_status=ok?"Merkintäluokka tallennettu.":"Luokan tallennus epäonnistui. Insuliinille tarvitaan valmiste.";refresh();return ok;}
bool Backend::addLocal(const QString&date,double value,const QString&kind,const QString&medicine,const QString&purpose){
    auto p=db.activeProfile();const auto zone=QTimeZone(p.value("timezone").toString("Europe/Helsinki").toUtf8());auto dt=QDateTime::fromString(date,"yyyy-MM-dd HH:mm");if(dt.isValid())dt=QDateTime(dt.date(),dt.time(),zone);
    if(!dt.isValid()||dt.toMSecsSinceEpoch()>QDateTime::currentMSecsSinceEpoch()){m_status="Anna toteutuneen merkinnän paikallinen aika muodossa vvvv-kk-pp tt:mm.";emit changed();return false;}
    const bool ok=!db.addLocal(dt.toMSecsSinceEpoch(),value,kind,medicine,purpose).isEmpty();m_status=ok?"Toteutunut merkintä tallennettu paikallisesti.":"Merkintä ei kelpaa.";refresh();return ok;
}
bool Backend::annotate(const QString&id,const QString&purpose,const QString&note){bool ok=db.annotate(id,{{"purpose",purpose},{"note",note}});m_status=ok?"Täydennys tallennettu erikseen alkuperäisestä tiedosta.":"Täydennyksen tallennus epäonnistui.";refresh();return ok;}
bool Backend::removeAmount(const QString&id){
    const bool ok=db.setDeleted(id,true);lastRecommendation={};
    m_status=ok?"Merkintä poistettu paikallisesta päiväkirjasta. Sen voi palauttaa kohdasta Poistetut merkinnät.":"Merkinnän poistaminen epäonnistui.";refresh();return ok;
}
bool Backend::restoreAmount(const QString&id){
    const bool ok=db.setDeleted(id,false);lastRecommendation={};
    m_status=ok?"Merkintä palautettu päiväkirjaan.":"Merkinnän palauttaminen epäonnistui.";refresh();return ok;
}
QVariantMap Backend::saveProfile(const QVariantMap&map,bool approve){
    auto p=QJsonObject::fromVariantMap(map);p["researchOnly"]=true;p["schema"]=1;p["approved"]=approve;
    p.remove("responseApproved");p.remove("responseApprovedAt");
    if(approve){const auto errors=validateProfile(p);if(!errors.isEmpty())return QJsonObject{{"status","blocked"},{"reasons",QJsonArray::fromStringList(errors)}}.toVariantMap();}
    // Every saved profile starts a new observation period. No backdating.
    p["effectiveFrom"]=double(QDateTime::currentMSecsSinceEpoch());p["approvedAt"]=approve?QDateTime::currentDateTimeUtc().toString(Qt::ISODate):QString();
    const int id=db.saveProfile(p);if(!id)return failure("Profiilin tallennus epäonnistui.").toVariantMap();lastRecommendation={};m_status=approve?"Uusi tutkimusprofiili hyväksytty. Uusi seurantajakso alkaa nyt.":"Profiilin luonnos tallennettu. Annoslaskenta on estetty.";refresh();return QJsonObject{{"status","saved"},{"version",id}}.toVariantMap();
}
QVariantMap Backend::saveEpisode(const QVariantMap&map){
    if(m_busy)return failure("Odota tietojen tuonnin valmistumista ennen jakson tallennusta.").toVariantMap();
    if(!db.run("BEGIN IMMEDIATE"))return failure("Tietokanta on varattu. Yritä jakson tallennusta uudelleen.").toVariantMap();
    auto rollback=qScopeGuard([this]{db.run("ROLLBACK");});
    auto e=QJsonObject::fromVariantMap(map);const bool response=e["responseV2"].toBool();
    const auto p=evaluationProfile();auto errors=response?validateResponseProfile(p):(p["historical"].toBool()?validateHistoricalProfile(p):validateProfile(p));
    if(response?!responseProfileApproved(p):!p["approved"].toBool())errors<<"Hyväksy profiili "+QString(response?"vastearvioon ensin.":"lääkityslaskentaan ensin.");
    if(!errors.isEmpty())return QJsonObject{{"status","blocked"},{"reasons",QJsonArray::fromStringList(errors)},{"requiresProfile",true},{"researchOnly",true}}.toVariantMap();
    if((e.contains("sourceRevision")&&e["sourceRevision"].toString()!=db.revision())||(e.contains("profileVersion")&&e["profileVersion"]!=p["version"]))return failure("Lähdetiedot tai hoitoprofiili ovat muuttuneet. Tarkista jakso ja vahvistukset uudelleen.").toVariantMap();
    for(auto key:{"reviewed","complete","contextClear"})if(!e[key].toBool())return failure("Vahvista jakson tarkistus, kattavat annos-/HH-tiedot ja sekoittavien tekijöiden puuttuminen ennen tallennusta.").toVariantMap();
    const QString kind=e["kind"].toString();
    if(kind!="icr"&&kind!="isf"&&kind!="basal")return failure("Tuntematon jaksotyyppi.").toVariantMap();
    const auto zone=QTimeZone(p["timezone"].toString().toUtf8());auto dt=QDateTime::fromString(e["startText"].toString(),"yyyy-MM-dd HH:mm");dt=QDateTime(dt.date(),dt.time(),zone);
    if(!dt.isValid())return failure("Virheellinen jakson aloitusaika.").toVariantMap();
    const qint64 start=dt.toMSecsSinceEpoch();const auto rules=p["rules"].toObject();const QString key=kind=="icr"?"mealWindowMinutes":kind=="isf"?"correctionWindowMinutes":"basalWindowMinutes";
    const qint64 end=start+qRound64(number(rules,key)*60000);if(end>QDateTime::currentMSecsSinceEpoch()||start<number(p,"effectiveFrom"))return failure("Jakson pitää olla valmistunut nykyisen hoitoprofiilin aikana.").toVariantMap();
    if(p["historical"].toBool()&&(response?db.responseProfileAt(end):db.profileAt(end))["version"]!=p["version"])return failure("Jakso ylittää hoitoprofiilin vaihdon tai kuuluu eri profiiliin.").toVariantMap();
    const qint64 lookback=qRound64(number(p["actionModel"].toObject(),"durationMinutes")*60000);
    qint64 dependencyFrom=start-lookback;
    auto rs=db.readings(start,end+1);if(e["responseV2"].toBool()){const auto keys=db.artifactKeys(start,end+1);rs.removeIf([&](const Reading&r){return keys.contains(r.key);});}for(qsizetype i=1;i<rs.size();++i)if(rs[i].sensor!=rs[i-1].sensor&&rs[i].time-rs[i-1].time<60000)return failure("Arviojaksossa on päällekkäisiä sensoreita; mittauslähde on tarkistettava.").toVariantMap();if(rs.size()<2)return failure("Jaksossa ei ole riittävästi mittauksia.").toVariantMap();
    auto near=[&rs](qint64 t){return *std::min_element(rs.cbegin(),rs.cend(),[t](const auto&a,const auto&b){return std::abs(a.time-t)<std::abs(b.time-t);});};
    const auto first=near(start),last=near(end);const double gap=number(rules,"maxGapMinutes")*60000;
    if(std::abs(first.time-start)>gap||std::abs(last.time-end)>gap)return failure("Jakson alku- tai loppumittaus puuttuu.").toVariantMap();
    const auto events=db.calculationAmounts(start-lookback,end+1);QJsonArray linked;double carbs=0,rapid=0;bool bad=false;
    const auto ids=e["linkedIds"].toArray();QSet<QString> wanted;for(auto id:ids)wanted.insert(id.toString());
    QSet<QString> found;bool nonIsolated=false;
    for(auto v:events){auto a=v.toObject();const auto id=a["id"].toString(),k=a["kind"].toString();const auto time=qint64(a["time"].toDouble());
        if(k=="unknown")bad=true;
        if(a["stale"].toBool())bad=true;
        if(wanted.contains(id)){
            linked.append(a);found.insert(id);if(k=="carbs")carbs+=a["value"].toDouble();
            if(k=="rapid"){
                rapid+=a["value"].toDouble();
                const QString purpose=a["purpose"].toString();
                if((kind=="icr"&&purpose!="ateria")||(kind=="isf"&&purpose!="korjaus"))bad=true;
            }
            if(time<start-lookback||time>end)bad=true;
        }
        if(k=="rapid"||k=="carbs"){
            const bool atStart=time>=start-15*60000&&time<=start+15*60000;
            if(kind=="basal"||(kind=="isf"&&k=="carbs")||!wanted.contains(id)||!atStart)nonIsolated=true;
            if(k=="rapid"&&a["medicine"]!=p["rapidMedicine"])bad=true;
        }
    }
    if(found!=wanted)return failure("Liitetty merkintä on muuttunut tai ei kuulu jaksoon.").toVariantMap();
    if(bad||nonIsolated)return failure("Jaksossa on luokittelemattomia tai päällekkäisiä HH-/insuliinivaikutuksia. Valitse eristetty jakso.").toVariantMap();
    if((kind=="icr"&&(!(carbs>0)||!(rapid>0)))||(kind=="isf"&&(!(rapid>0)||carbs>0)))return failure("Liitä toteutuneet ateria-/korjausmerkinnät. Aterialle tarvitaan HH ja pikainsuliini.").toVariantMap();
    for(auto v:linked){const auto a=v.toObject();if(a["kind"]=="rapid"&&end-qint64(a["time"].toDouble())<lookback)return failure("Jakso ei kata pikainsuliinin koko vaikutusaikaa toteutuneesta pistoksesta alkaen. Tarkista alkuaika ja profiilin seurantajakson pituus.").toVariantMap();}
    if((kind=="basal"||e["responseV2"].toBool())&&(!e["basalStable"].toBool()||!e["basalHistoryComplete"].toBool()))return failure("Vahvista perusinsuliinin toteutuminen ja vakaa annostus profiilin koko tasaantumisjaksolta.").toVariantMap();
    if(kind=="basal"||e["responseV2"].toBool()){
        const qint64 stableFrom=start-qRound64(number(p,"basalStableDays")*86400000);
        dependencyFrom=std::min(dependencyFrom,stableFrom);
        const auto basalEvents=db.calculationAmounts(stableFrom,start+1);QMap<QDate,double> daily;QMap<QDate,int> counts;
        for(auto v:basalEvents){auto a=v.toObject();if(a["kind"]!="basal")continue;
            if(a["stale"].toBool())return failure("Tasaantumisjakson perusannos on poistettu tai jätetty pois arviosta. Tarkista perusannosten kirjaushistoria.").toVariantMap();
            if(a["medicine"]!=p["basalMedicine"])return failure("Tasaantumisjaksolla on eri perusinsuliinivalmiste.").toVariantMap();
            auto t=QDateTime::fromMSecsSinceEpoch(qint64(a["time"].toDouble()),zone);daily[t.date()]+=a["value"].toDouble();counts[t.date()]++;
        }
        int verified=0;QDate priorDate;
        for(auto it=daily.cbegin();it!=daily.cend();++it){
            if(priorDate.isValid()&&priorDate.daysTo(it.key())!=1)return failure("Tasaantumisjakson annoskirjauksissa on puuttuva päivä.").toVariantMap();
            if(counts[it.key()]!=1||std::abs(it.value()-number(p,"basalDose"))>1e-6)return failure("Perusinsuliinin annostus poikkeaa arvioitavasta kerran vuorokaudessa -profiilista.").toVariantMap();
            ++verified;priorDate=it.key();}
        if(verified<std::floor(number(p,"basalStableDays")))return failure("Tasaantumisjakson kirjattuja perusinsuliiniannoksia puuttuu.").toVariantMap();
    }
    if(e["responseV2"].toBool()){
        const auto policy=QJsonObject::fromVariantMap(responsePolicy());const auto errors=validateResponsePolicy(policy);if(!policy["approved"].toBool()||!errors.isEmpty())return failure("Hyväksy vastepolitiikka ja sen rajat ensin.").toVariantMap();
        if(!e["endStable"].toBool()||!e["basalStable"].toBool()||!e["basalHistoryComplete"].toBool())return failure("Vahvista päättynyt vakaa loppuvaste sekä vakaa ja kattavasti kirjattu perusinsuliini.").toVariantMap();
        const qint64 span=qRound64(number(policy,"endpointMinutes")*60000);qint64 baseline=start,doseTime=start;bool haveDose=false;for(auto v:linked){auto a=v.toObject();baseline=std::min(baseline,qint64(a["time"].toDouble()));if(a["kind"]=="rapid"){if(!haveDose)doseTime=qint64(a["time"].toDouble());else doseTime=std::min(doseTime,qint64(a["time"].toDouble()));haveDose=true;}}
        auto medianReading=[&](qint64 a,qint64 b,Reading&out){auto values=db.readings(a,b);const auto keys=db.artifactKeys(a,b);values.removeIf([&](const Reading&r){return keys.contains(r.key);});if(values.size()<2||metrics(values,a,b,NAN,NAN,qRound(number(p["rules"].toObject(),"maxGapMinutes")*60))["coverage"].toDouble()<number(policy,"minCoverage"))return false;for(auto r:values)if(!r.reliable)return false;std::sort(values.begin(),values.end(),[](auto a,auto b){return a.mmol<b.mmol;});out=values[values.size()/2];if(values.size()%2==0)out.mmol=(values[values.size()/2-1].mmol+out.mmol)/2;return true;};
        Reading firstMedian,lastMedian;if(!medianReading(baseline-span,baseline+1,firstMedian)||!medianReading(end-span,end+1,lastMedian))return failure("Alku-/loppumediaanin havaintoja puuttuu.").toVariantMap();
        qint64 mealTime=start;bool haveMeal=false;for(auto v:linked){auto a=v.toObject();if(a["kind"]=="carbs"){mealTime=haveMeal?std::min(mealTime,qint64(a["time"].toDouble())):qint64(a["time"].toDouble());haveMeal=true;}}e["mealTime"]=double(mealTime);
        e["responseFirst"]=firstMedian.mmol;e["responseLast"]=lastMedian.mmol;e["doseTime"]=double(doseTime);e["methodVersion"]=2;e["endpointMinutes"]=policy["endpointMinutes"];dependencyFrom=std::min(dependencyFrom,baseline-span);
    }
    bool hypo=false;for(auto r:rs)if(r.mmol<number(p,"hypo"))hypo=true;
    const auto s=slotProfile(p,slotAt(p,start));double deviation=last.mmol-first.mmol;
    const double mealBaseline=response?number(e,"responseFirst"):first.mmol;
    if(kind=="icr"&&(mealBaseline<number(p,"targetLow")||mealBaseline>number(p,"targetHigh")))return failure("HH-suhteen arvio tarvitsee tavoitealueella olevan lähtöglukoosin ilman korjaustarvetta.").toVariantMap();
    if(kind=="isf"&&std::isfinite(number(s,"isf")))deviation+=rapid*number(s,"isf");
    e["start"]=double(start);e["end"]=double(end);e["dependencyFrom"]=double(dependencyFrom);e["slot"]=slotAt(p,start);e["linked"]=linked;e["carbs"]=carbs;e["rapid"]=rapid;
    e["firstGlucose"]=response?e["responseFirst"]:QJsonValue(first.mmol);e["lastGlucose"]=response?e["responseLast"]:QJsonValue(last.mmol);
    // A missing prescribed ISF has no reference-deviation value; the observed delta remains intact.
    e["deviation"]=response&&kind=="isf"&&!std::isfinite(number(s,"isf"))?QJsonValue():QJsonValue(response?e["lastGlucose"].toDouble()-e["firstGlucose"].toDouble()+(kind=="isf"?rapid*number(s,"isf"):0):deviation);
    e["hypoObserved"]=hypo;e["profileVersion"]=p["version"];
    e["coverage"]=metrics(rs,start,end,number(p,"targetLow"),number(p,"targetHigh"),int(gap/1000))["coverage"];
    for(auto v:db.episodes()){auto prior=v.toObject();const bool reviewed=prior["reviewed"].toBool()&&prior["complete"].toBool()&&prior["contextClear"].toBool();if(reviewed&&(prior["methodVersion"].toInt()==2)==response&&prior["kind"]==kind&&prior["profileVersion"]==p["version"]&&!prior["stale"].toBool()&&number(prior,"start")<end&&number(prior,"end")>start)return failure("Tämä jakso on jo tarkistettu tai päällekkäinen aiemman jakson kanssa.").toVariantMap();}
    e["reviewer"]=p["reviewer"];
    const QString id=db.saveEpisode(e);if(id.isEmpty()||!db.run("COMMIT"))return failure("Jakson tallennus epäonnistui. Mitään ei tallennettu.").toVariantMap();rollback.dismiss();m_status="Tarkistettu jakso tallennettu. Muutosehdotus käyttää vain kelpuutettuja jaksoja.";refresh();return QJsonObject{{"status","saved"},{"id",id}}.toVariantMap();
}
QVariantMap Backend::assess(const QString&kind,const QString&slot){auto episodes=db.episodes();if(evaluationProfile()["historical"].toBool()){QJsonArray chosen;const double a=m_analysis.value("from").toDouble(),b=m_analysis.value("to").toDouble();for(auto v:episodes){auto e=v.toObject();if(number(e,"start")>=a&&number(e,"end")<=b)chosen<<e;}episodes=chosen;}lastRecommendation=recommend(evaluationProfile(),kind,slot,episodes,evaluationProfile()["historical"].toBool()?std::min(QDateTime::currentMSecsSinceEpoch(),qint64(m_analysis.value("to").toDouble())):QDateTime::currentMSecsSinceEpoch());recommendationRevision=db.revision();lastRecommendation["sourceRevision"]=recommendationRevision;db.saveCalculation("recommendation",lastRecommendation);return lastRecommendation.toVariantMap();}
QVariantMap Backend::acceptRecommendation(){
    if(lastRecommendation["status"]!="research"||recommendationRevision!=db.revision())return failure("Ehdotus puuttuu tai aineisto on muuttunut. Laske arvio uudelleen.").toVariantMap();
    auto p=db.activeProfile();if(selectedProfileVersion&&selectedProfileVersion!=p["version"].toInt())return failure("Historiaprofiili ei ole uusin. Nykyisiä asetuksia ei muutettu.").toVariantMap();if(lastRecommendation["profileVersion"]!=p["version"])return failure("Hoitoprofiili on muuttunut.").toVariantMap();
    const auto kind=lastRecommendation["kind"].toString();if(kind=="basal")p["basalDose"]=lastRecommendation["candidate"];
    else{QJsonArray periods;for(auto v:p["slots"].toArray()){auto s=v.toObject();if(s["name"]==lastRecommendation["slot"])s[kind]=lastRecommendation["candidate"];periods.append(s);}p["slots"]=periods;}
    p["acceptedProposal"]=lastRecommendation;
    if(p["historical"].toBool()){p["effectiveFromText"]=QDateTime::currentDateTime(QTimeZone(p["timezone"].toString().toUtf8())).toString("yyyy-MM-dd HH:mm");p["historicalConfirmed"]=true;return saveAnalysisProfile(p.toVariantMap(),true);}
    return saveProfile(p.toVariantMap(),true);
}
QVariantMap Backend::bolus(const QVariantMap&map){
    const qint64 now=QDateTime::currentMSecsSinceEpoch();auto p=db.activeProfile();auto in=QJsonObject::fromVariantMap(map);auto rs=db.readings(now-86400000,now+60001);
    if(rs.isEmpty())return failure("Glukoositietoa ei ole.").toVariantMap();
    const auto &r=rs.last();in["glucose"]=r.mmol;in["glucoseTime"]=double(r.time);in["reliable"]=r.reliable&&in["measurementConfirmed"].toBool();
    const double duration=number(p["actionModel"].toObject(),"durationMinutes");const qint64 lookback=std::isfinite(duration)?qRound64(duration*60000):86400000;
    for(const auto &sample:rs)if(sample.sensor!=r.sensor&&std::abs(sample.time-r.time)<=60000)return failure("Samanaikaisia mittauksia on useasta sensorista. Varmista oikea mittauslähde.").toVariantMap();
    auto result=calculateBolus(p,in,db.calculationAmounts(now-lookback,now+60001),now);result["sourceRevision"]=db.revision();db.saveCalculation("bolus",result);return result.toVariantMap();
}
QString Backend::backup(){
    const auto datasetRoot=QFileInfo(db.path()).absolutePath();
    const QString dest=datasetRoot+"/backups/studio-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz")+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)+".sqlite3";
    if(!db.backup(dest)){m_status="Varmuuskopiointi epäonnistui.";emit changed();return {};}
    QJsonArray files;bool ok=true;for(auto v:db.query("SELECT path,sha256 FROM import_chunks")){auto row=v.toObject();const QString source=row["path"].toString(),relative=QDir(datasetRoot).relativeFilePath(source),target=dest+".sources/"+relative;
        if(relative.startsWith("..")||QFileInfo(source).isSymLink()){ok=false;break;}QFile f(source);if(!f.open(QIODevice::ReadOnly)){ok=false;break;}const auto bytes=f.readAll();if(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=row["sha256"].toString()){ok=false;break;}
        if(!QDir().mkpath(QFileInfo(target).absolutePath())||!QFile::copy(source,target)){ok=false;break;}
        files<<QJsonObject{{"relative",relative},{"original",source},{"sha256",row["sha256"]}};
    }
    QSaveFile manifest(dest+".manifest.json");if(ok)ok=manifest.open(QIODevice::WriteOnly);if(ok){manifest.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);manifest.write(QJsonDocument(QJsonObject{{"schema",1},{"files",files},{"connection",connectionObject(config)},{"dataset",QDir(root).relativeFilePath(db.path())}}).toJson());ok=manifest.commit();}
    m_status=ok?"Eheä tietokanta ja alkuperäisviennit varmuuskopioitu paikallisesti.":"Tietokantakopio säilyi, mutta alkuperäisvientien varmistus jäi vajaaksi.";emit changed();return ok?dest:QString();
}

void Backend::seedDemo(){
    if(!db.meta("lastSync").isEmpty()){m_status="Esimerkkiaineisto sallitaan vain tyhjään tietokantaan.";emit changed();return;}
    const qint64 now=QDateTime::currentSecsSinceEpoch()*1000,from=now-30LL*86400000;QList<Reading> rs;QList<Amount> as;
    for(qint64 t=from;t<now;t+=5*60000){if((t-from)%(3*86400000)>86400000&&((t-from)/60000)%1440>600&&((t-from)/60000)%1440<650)continue;Reading r;r.time=t;r.sensor="synthetic";r.key="demo:"+QString::number(t);r.mmol=7+2*std::sin(double(t-from)/3600000)+0.6*std::sin(double(t-from)/150000);r.original=r.mmol;r.originalUnit="mmol/L";rs.append(r);}
    for(int d=0;d<30;++d){Amount a;a.source="synthetic";a.time=now-d*86400000LL-4*3600000;a.label="Esimerkki HH";a.value=45;a.key=fingerprint(a)+":0";as.append(a);a.label="Esimerkki pikainsuliini";a.value=4;a.key=fingerprint(a)+":0";as.append(a);}
    db.ingest(rs,as,from-1,now+1,true);db.mapLabel("Esimerkki HH","carbs","");db.mapLabel("Esimerkki pikainsuliini","rapid","Synteettinen esimerkki");
    db.setMeta("automaticMealSettings",encoded({{"targetLow",6},{"targetHigh",8},{"hypo",4},{"periodDays",56}}));
    m_status="Keinotekoinen esimerkkiaineisto ladattu. Ei henkilökohtaisia hoitoasetuksia.";refresh();
}

QJsonObject Backend::evaluationProfile()const{
    if(!selectedProfileVersion)return db.activeProfile();
    for(auto v:db.profiles()){auto r=v.toObject();if(r["version"].toInt()==selectedProfileVersion){auto p=QJsonDocument::fromJson(r["data"].toString().toUtf8()).object();p["version"]=r["version"];return p;}}return {};
}
QVariantList Backend::profileVersions()const{QVariantList out;for(auto v:db.profiles()){auto row=v.toObject(),p=QJsonDocument::fromJson(row["data"].toString().toUtf8()).object();out<<QVariantMap{{"version",row["version"].toInt()},{"name",QString("Versio %1 · %2").arg(row["version"].toInt()).arg(QDateTime::fromMSecsSinceEpoch(qint64(p["effectiveFrom"].toDouble())).toString("dd.MM.yyyy"))}};}return out;}
void Backend::selectProfile(int version){selectedProfileVersion=version;lastRecommendation={};responseResult={};responseDraft={};refresh();}
QVariantMap Backend::analysisSettings()const{
    auto p=QJsonDocument::fromJson(db.meta("analysisSettings").toUtf8()).object();
    if(!p.contains("timezone"))p["timezone"]="Europe/Helsinki";
    if(!p.contains("maxGapMinutes"))p["maxGapMinutes"]=20;
    if(!p.contains("fillHistory"))p["fillHistory"]=false;
    return p.toVariantMap();
}
bool Backend::saveAnalysisSettings(const QVariantMap&map){auto p=QJsonObject::fromVariantMap(map);auto zone=QTimeZone(p["timezone"].toString().toUtf8());
    const double lo=number(p,"low"),hi=number(p,"high"),gap=number(p,"maxGapMinutes");
    if(!zone.isValid()||!std::isfinite(gap)||gap<1||gap>60||((std::isfinite(lo)||std::isfinite(hi))&&!(lo>0&&hi>lo))){m_status="Tarkista aikavyöhyke, katkosraja ja tavoitealueen molemmat rajat.";emit changed();return false;}
    db.setMeta("analysisZone",p["timezone"].toString());bool ok=db.setMeta("analysisSettings",encoded(p));refresh();return ok;
}
void Backend::setAnalysisWindow(int days,const QString&last){if(days!=7&&days!=14&&days!=28&&days!=90)return;const auto d=QDate::fromString(last,Qt::ISODate);const auto zone=QTimeZone(analysisSettings().value("timezone").toString().toUtf8());if(!last.isEmpty()&&(!d.isValid()||d>=QDateTime::currentDateTime(zone).date())){m_status="Valitse valmistunut kalenteripäivä muodossa vvvv-kk-pp.";emit changed();return;}analysisDays=days;analysisLastDate=last;lastRecommendation={};responseResult={};responseDraft={};refreshAnalysis();emit changed();}
void Backend::refreshAnalysis(){
    const auto settings=QJsonObject::fromVariantMap(analysisSettings());const auto zone=QTimeZone(settings["timezone"].toString().toUtf8());
    auto last=QDate::fromString(analysisLastDate,Qt::ISODate);const auto today=QDateTime::currentDateTime(zone).date();if(!last.isValid()||last>=today)last=today.addDays(-1);
    const auto first=last.addDays(1-analysisDays),previous=first.addDays(-analysisDays);const qint64 from=QDateTime(previous,QTime(0,0),zone).toMSecsSinceEpoch(),to=QDateTime(last.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch();
    const auto stream=db.readings(from,to,"stream"),history=db.readings(from,to,"history");const int gap=qRound(number(settings,"maxGapMinutes")*60);
    auto data=selectSeries(stream,history,from,to,settings["fillHistory"].toBool(),gap);
    // Standard glucose summaries are independent of the research-only switches.
    const auto summaryStream=db.readings(from-1200000,to,"stream"),summaryHistory=db.readings(from-1200000,to,"history");
    auto result=summaryAnalysis(summaryStream,summaryHistory,first,analysisDays,zone,number(settings,"low"),number(settings,"high"));
    result["previous"]=summaryAnalysis(summaryStream,summaryHistory,previous,analysisDays,zone,number(settings,"low"),number(settings,"high"));
    // GMI is an estimate of HbA1c from the same time-weighted mean. The 14-day
    // and 70% coverage rule marks whether the estimate has a conventional basis.
    if(result.contains("mean")){
        const double mean=result["mean"].toDouble();
        result["gmi"]=gmiPercent(mean);result["gmiMmol"]=gmiMmolMol(mean);result["gmiAvailable"]=true;
        result["gmiAdequate"]=analysisDays>=14&&result["coverage"].toDouble()>=0.70;result["gmiDays"]=analysisDays;
    } else result["gmiAvailable"]=false;
    QJsonObject reference{{"meanLow",referenceMeanLow},{"meanHigh",referenceMeanHigh},
        {"gmiTarget",referenceGmiTarget},{"tirTarget",referenceTirTarget},
        {"cvTarget",referenceCvTarget},{"coverageTarget",referenceCoverageTarget}};
    reference["meanInRange"]=result.contains("mean")&&result["mean"].toDouble()>=referenceMeanLow&&result["mean"].toDouble()<=referenceMeanHigh;
    reference["gmiInRange"]=result.contains("gmi")&&result["gmi"].toDouble()<referenceGmiTarget&&result["gmiAdequate"].toBool();
    reference["tirInRange"]=result.contains("inRange")&&result["inRange"].toDouble()>=referenceTirTarget;
    reference["cvInRange"]=result.contains("cv")&&result["cv"].toDouble()<=referenceCvTarget;
    reference["coverageInRange"]=result["coverage"].toDouble()>=referenceCoverageTarget;
    reference["nightInRange"]=result.contains("nightMean")&&result["nightMean"].toDouble()>=referenceMeanLow&&result["nightMean"].toDouble()<=referenceMeanHigh;
    result["reference"]=reference;
    result["insulin"]=insulinSummary(db.amounts(qint64(result["from"].toDouble()),to,true),first,analysisDays,zone,db.completeDays());
    result["observation"]=observeProfile(db.observationProfile(),data,db.amounts(qint64(result["from"].toDouble()),to,true),first,analysisDays,zone,db.completeDays());
    result["lastImport"]=db.meta("lastCompleteImport").toDouble();
    const auto latest=db.query("SELECT max(time) AS t FROM glucose WHERE series_kind='stream'");if(!latest.isEmpty())result["lastReading"]=latest[0].toObject()["t"];
    const auto latestAmount=db.query("SELECT max(time) AS t FROM amounts WHERE active=1 AND local=0");if(!latestAmount.isEmpty())result["lastAmount"]=latestAmount[0].toObject()["t"];
    // Short current-day report is separate from complete-day aggregates.
    const qint64 dayStart=QDateTime(today,QTime(0,0),zone).toMSecsSinceEpoch(),now=QDateTime::currentMSecsSinceEpoch();
    result["today"]=summaryAnalysis(db.readings(dayStart-1200000,now),db.readings(dayStart-1200000,now,"history"),today,1,zone,number(settings,"low"),number(settings,"high"));
    // Front page: the current local calendar day, stream first with history gaps.
    const qint64 dayEnd=QDateTime(today.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch();
    m_currentDayChart=currentDayChartData(
        db.readings(dayStart-summaryPreRollMs,dayEnd,"stream"),
        db.readings(dayStart-summaryPreRollMs,dayEnd,"history"),
        today,zone,number(settings,"low"),number(settings,"high")).toVariantMap();
    m_analysis=result.toVariantMap();
    // Comparison chart is the previous local calendar day, with its own time axis.
    auto selected=QDate::fromString(dateWindow,Qt::ISODate);if(!selected.isValid())selected=last;
    const qint64 a=QDateTime(selected.addDays(-1),QTime(0,0),zone).toMSecsSinceEpoch(),b=QDateTime(selected,QTime(0,0),zone).toMSecsSinceEpoch();
    QVariantList segments,seg;double max=15;qint64 prior=0;for(auto v:selectSeries(db.readings(a,b),db.readings(a,b,"history"),a,b,settings["fillHistory"].toBool(),gap)){Reading r;r.time=v.from;r.mmol=v.value;if(prior&&r.time-prior>gap*1000){segments<<QVariant(seg);seg.clear();}seg<<QVariantMap{{"x",double(r.time)},{"y",r.mmol}};prior=r.time;max=std::max(max,r.mmol+2);}if(!seg.isEmpty())segments<<QVariant(seg);
    m_comparisonChart={{"from",double(a)},{"to",double(b)},{"segments",segments},{"markers",QVariantList{}},{"max",max},{"low",settings["low"].toVariant()},{"high",settings["high"].toVariant()}};
    // Daily comparison uses the same standard stream/history policy as the
    // front page, independent of the retained research-chart switches.
    const auto comparisonDay=QDate::fromString(dateWindow,Qt::ISODate);
    const auto anchor=comparisonDay.isValid()?comparisonDay:today;
    const auto comparisonFrom=anchor.addDays(-3).startOfDay(zone).toMSecsSinceEpoch();
    const auto comparisonTo=anchor.addDays(1).startOfDay(zone).toMSecsSinceEpoch();
    const auto comparisonStream=db.readings(comparisonFrom-summaryPreRollMs,comparisonTo,"stream");
    const auto comparisonHistory=db.readings(comparisonFrom-summaryPreRollMs,comparisonTo,"history");
    const auto comparisonAmounts=db.amounts(comparisonFrom,comparisonTo);
    m_dayComparisonCharts.clear();
    for(int offset=0;offset<4;++offset){
        auto chart=currentDayChartData(comparisonStream,comparisonHistory,
            anchor.addDays(-offset),zone,number(settings,"low"),number(settings,"high"));
        QJsonArray markers;
        const auto from=qint64(chart["from"].toDouble()),to=qint64(chart["to"].toDouble());
        for(auto value:comparisonAmounts){
            auto entry=value.toObject();const auto time=qint64(entry["time"].toDouble());
            if(time>=from&&time<to){
                entry["timeText"]=QDateTime::fromMSecsSinceEpoch(time,zone).toString("dd.MM.yyyy HH:mm");
                markers.append(entry);
            }
        }
        chart["markers"]=markers;m_dayComparisonCharts.append(chart.toVariantMap());
    }
    requestAutomaticAnalysis();
    refreshCurrentReading();
}
void Backend::refreshCurrentReading(){
    const auto conf=QJsonObject::fromVariantMap(analysisSettings());
    const QTimeZone zone(conf["timezone"].toString().toUtf8());
    const auto now=QDateTime::currentMSecsSinceEpoch();
    auto read=[&](qint64 from){
        return currentGlucose(db.readings(from,now+60000),db.readings(from,now+60000,"history"),
            now,zone,number(conf,"low"),number(conf,"high"),number(conf,"hypo")).toVariantMap();
    };
    auto value=read(now-24LL*3600000);
    if(!value["hasData"].toBool())value=read(now-30LL*86400000);
    if(m_currentReading!=value){m_currentReading=value;emit currentReadingChanged();}
    requestDailyAnalysis();
}
bool Backend::confirmDay(const QString&date,bool complete){bool ok=db.confirmDay(date,complete);refreshAnalysis();emit changed();return ok;}
bool Backend::excludeFromAnalysis(const QString&id,const QString&reason){bool ok=db.exclude(id,reason);lastRecommendation={};refresh();return ok;}
QVariantMap Backend::saveAnalysisProfile(const QVariantMap&map,bool approve){
    return saveHistoricalProfile(map,approve,false);
}
QVariantMap Backend::saveResponseProfile(const QVariantMap&map,bool approve){
    return saveHistoricalProfile(map,approve,true);
}
QVariantMap Backend::saveHistoricalProfile(const QVariantMap&map,bool approve,bool responseOnly){
    auto p=QJsonObject::fromVariantMap(map);p.remove("observationActive");p.remove("observationActivatedAt");p.remove("observationSourceVersion");p["historical"]=true;p["schema"]=1;p["researchOnly"]=true;
    p["approved"]=approve&&!responseOnly;p.remove("responseApproved");p.remove("responseApprovedAt");
    if(responseOnly){p["responseApproved"]=approve;p["responseApprovedAt"]=approve?QDateTime::currentDateTimeUtc().toString(Qt::ISODate):QString();}
    auto dt=QDateTime::fromString(p["effectiveFromText"].toString(),"yyyy-MM-dd HH:mm");const auto zone=QTimeZone(p["timezone"].toString().toUtf8());dt=QDateTime(dt.date(),dt.time(),zone);
    if(!dt.isValid()||dt.toMSecsSinceEpoch()>QDateTime::currentMSecsSinceEpoch())return failure("Määritä asetusten todellinen alkuaika muodossa vvvv-kk-pp tt:mm.").toVariantMap();
    if(approve&&!p["historicalConfirmed"].toBool())return failure("Vahvista asetusten voimassaolo valitusta ajasta alkaen.").toVariantMap();
    const auto current=db.activeProfile();if(!current.isEmpty()&&dt.toMSecsSinceEpoch()<number(current,"effectiveFrom"))return failure("Uuden version alkuaika ei saa edeltää viimeisintä versiota. Aiemmat profiilit säilyvät.").toVariantMap();
    p["effectiveFrom"]=double(dt.toMSecsSinceEpoch());p["approvedAt"]=approve&&!responseOnly?QDateTime::currentDateTimeUtc().toString(Qt::ISODate):QString();if(approve){const auto errors=responseOnly?validateResponseProfile(p):validateHistoricalProfile(p);if(!errors.isEmpty())return failure(errors.join('\n')).toVariantMap();}
    if(!db.run("BEGIN IMMEDIATE"))return failure("Tallennus epäonnistui.").toVariantMap();
    const int version=db.saveProfile(p);bool ok=version>0;
    if(ok)ok=db.run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)",{QDateTime::currentMSecsSinceEpoch(),"historical-profile-saved",encoded(QJsonObject{{"version",version},{"sourceVersion",p["version"]},{"scope",responseOnly?"response":"medication"},{"approved",approve}})});
    if(ok)ok=db.setMeta("revision",QUuid::createUuid().toString());
    if(ok)ok=db.run("COMMIT");
    if(!ok){db.run("ROLLBACK");return failure("Tallennus epäonnistui.").toVariantMap();}
    selectedProfileVersion=version;lastRecommendation={};responseResult={};responseDraft={};refresh();return QVariantMap{{"status","saved"},{"version",version}};
}
QVariantMap Backend::activateObservationProfile(const QVariantMap &map){
    auto p=QJsonObject::fromVariantMap(map);
    p["schema"]=1;p["researchOnly"]=true;p["historical"]=true;
    p["approved"]=false;p["approvedAt"]="";
    p.remove("responseApproved");p.remove("responseApprovedAt");
    const auto zone=QTimeZone(p["timezone"].toString().toUtf8());
    if(!p["effectiveFromText"].toString().isEmpty()){
        auto dt=QDateTime::fromString(p["effectiveFromText"].toString(),"yyyy-MM-dd HH:mm");
        dt=QDateTime(dt.date(),dt.time(),zone);
        if(!dt.isValid())return failure("Tarkista hoitoasetusten alkuaika.").toVariantMap();
        p["effectiveFrom"]=double(dt.toMSecsSinceEpoch());
    }
    const auto errors=validateObservationProfile(p);
    if(!errors.isEmpty())return failure(errors.join('\n')).toVariantMap();
    const auto latest=db.activeProfile();
    if(!latest.isEmpty()&&number(p,"effectiveFrom")<number(latest,"effectiveFrom"))return failure("Vanhempaa historiaprofiilia ei oteta nykyisen hoidon seurantaprofiiliksi.").toVariantMap();
    const auto prior=db.observationProfile();
    auto comparable=[](QJsonObject o){for(auto key:{"version","observationActive","observationActivatedAt","observationSourceVersion"})o.remove(key);return o;};
    if(!prior.isEmpty()&&comparable(prior)==comparable(p)){
        selectedProfileVersion=prior["version"].toInt();refresh();
        return QVariantMap{{"status","active"},{"version",selectedProfileVersion},{"reused",true}};
    }
    const auto oldVersion=p["version"];
    p["observationActive"]=true;p["observationSourceVersion"]=oldVersion;
    p["observationActivatedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if(!db.run("BEGIN IMMEDIATE"))return failure("Seurantaprofiilin tallennus epäonnistui.").toVariantMap();
    const int version=db.saveProfile(p);
    bool ok=version>0;
    if(ok)ok=db.setMeta("observationProfileVersion",QString::number(version));
    auto settings=QJsonObject::fromVariantMap(analysisSettings());
    settings["low"]=p["targetLow"];settings["high"]=p["targetHigh"];settings["timezone"]=p["timezone"];
    if(ok)ok=db.setMeta("analysisSettings",encoded(settings));
    if(ok)ok=db.setMeta("analysisZone",p["timezone"].toString());
    if(ok)ok=db.setMeta("revision",QUuid::createUuid().toString());
    if(ok)ok=db.run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)",{QDateTime::currentMSecsSinceEpoch(),"profile-observation-activated",encoded(QJsonObject{{"version",version},{"sourceVersion",oldVersion},{"scope","observation"},{"medicationApproved",false}})});
    if(ok)ok=db.run("COMMIT");
    if(!ok){db.run("ROLLBACK");return failure("Seurantaprofiilin tallennus epäonnistui.").toVariantMap();}
    selectedProfileVersion=version;responseResult={};responseDraft={};lastRecommendation={};
    m_status="Hoitoprofiili on käytössä seurannassa. Lääkityslaskennan hyväksyntä on erillinen.";refresh();
    return QVariantMap{{"status","active"},{"version",version},{"reused",false}};
}
bool Backend::importFolder(const QString&path){
    if(m_busy)return false;
    if(config.source!="juggluco"){m_status="TSV-kansiotuonti kuuluu Juggluco-lähteeseen. Valitse se ensin tietolähteeksi.";emit changed();return false;}
    QDir folder(path);if(!folder.isAbsolute()||!folder.exists()){m_status="Valitse paikallinen vientikansio.";emit changed();return false;}
    syncLock=std::make_unique<QLockFile>(root+"/sync.lock");if(!syncLock->tryLock(0)){m_status="Toinen tuonti on käynnissä.";emit changed();return false;}
    struct Piece{QString kind;QByteArray bytes;};QList<Piece> pieces;qint64 from=std::numeric_limits<qint64>::max(),to=0;QString error;
    for(auto kind:QStringList{"stream","history","scans","amounts"}){QString filename=folder.filePath(kind+"-full.tsv");if(!QFile::exists(filename))filename=folder.filePath(kind+".tsv");if(!QFile::exists(filename))continue;QFile f(filename);if(!f.open(QIODevice::ReadOnly)||f.size()>64*1024*1024){error="Vientitiedoston lukeminen epäonnistui tai se on liian suuri.";break;}auto bytes=f.readAll();if(kind=="amounts"){auto p=parseAmounts(bytes);error=p.error;for(auto r:p.rows){from=std::min(from,r.time);to=std::max(to,r.time+1000);}}else{auto p=parseStream(bytes,kind);error=p.error;for(auto r:p.rows){from=std::min(from,r.time);to=std::max(to,r.time+1000);}}if(!error.isEmpty())break;pieces<<Piece{kind,bytes};}
    if(pieces.isEmpty()||from>=to||!error.isEmpty()){syncLock->unlock();syncLock.reset();m_status=error.isEmpty()?"Kansiossa ei ole kelvollisia alkuperäisiä TSV-vientejä.":error;emit changed();return false;}
    const QString run=db.beginImport(from,to,"files-additive");bool ok=!run.isEmpty();for(auto p:pieces){if(!ok)break;
        // File boundaries cannot prove source deletions; every piece still has provenance.
        ok=db.importChunk(run,p.kind,p.bytes,from,to,false);
    }
    db.endImport(run,ok,ok?"Lisäävä tiedostotuonti; puuttuvia lähdetapahtumia ei poisteta.":db.error());syncLock->unlock();syncLock.reset();m_status=ok?"Tiedostot tuotu. Puuttuvia vanhoja merkintöjä ei poistettu.":db.error();refresh();return ok;
}
void Backend::chooseImportFolder(){auto path=QFileDialog::getExistingDirectory(nullptr,"Jugglucon alkuperäiset TSV-viennit");if(!path.isEmpty())importFolder(path);}
QString Backend::saveReport(){
    refresh();
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);QJsonObject report=QJsonObject::fromVariantMap(m_analysis);report["sourceRevision"]=db.revision();report["profile"]=evaluationProfile();report["observationProfile"]=db.observationProfile();report["recommendation"]=lastRecommendation;report["response"]=responseResult;report["automaticMeals"]=automaticResult;report["responsePolicy"]=QJsonObject::fromVariantMap(responsePolicy());report["artifactReviews"]=db.artifactReviews();report["responseProposal"]=responseDraft;report["exclusions"]=db.exclusions();
    if(!db.run("INSERT INTO analysis_runs VALUES(?,?,?,?,?)",{id,QDateTime::currentMSecsSinceEpoch(),db.revision(),encoded(QJsonObject::fromVariantMap(analysisSettings())),encoded(report)}))return {};
    QDir().mkpath(root+"/reports");const QString path=root+"/reports/"+id+".json";QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return {};f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));if(!f.commit())return {};m_status="Raportti tallennettu paikallisesti: "+path;emit changed();return path;
}

QVariantMap Backend::responsePolicy()const{auto p=QJsonDocument::fromJson(db.meta("responsePolicy").toUtf8()).object();return (p.isEmpty()?defaultResponsePolicy():p).toVariantMap();}
QVariantMap Backend::saveResponsePolicy(const QVariantMap&map,bool approve){auto p=QJsonObject::fromVariantMap(map);p["version"]=2;p["approved"]=approve;if(approve){const auto errors=validateResponsePolicy(p);if(!errors.isEmpty())return failure(errors.join('\n')).toVariantMap();}p["savedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);p["sourceRevision"]=db.revision();db.run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)",{QDateTime::currentMSecsSinceEpoch(),"response-policy-previous",db.meta("responsePolicy")});if(!db.setMeta("responsePolicy",encoded(p)))return failure("Tallennus epäonnistui.").toVariantMap();responseResult={};responseDraft={};emit changed();return QVariantMap{{"status","saved"}};}
QVariantMap Backend::saveResponseEpisode(const QVariantMap&map){auto input=map;input["responseV2"]=true;return saveEpisode(input);}
void Backend::analyseResponse(int referenceDays){
    if(referenceDays!=28&&referenceDays!=56&&referenceDays!=84){m_status="Valitse 28, 56 tai 84 päivän vertailu.";emit changed();return;}
    const auto p=evaluationProfile(),policy=QJsonObject::fromVariantMap(responsePolicy());auto errors=validateResponseProfile(p);errors<<validateResponsePolicy(policy);if(!responseProfileApproved(p)||!policy["approved"].toBool())errors<<"Hyväksy profiili vastearvioon ja vastepolitiikka.";if(!errors.isEmpty()){responseResult=failure(errors.join("\n"));responseDraft={};emit changed();return;}const qint64 from=qint64(m_analysis.value("from").toDouble()),to=qint64(m_analysis.value("to").toDouble());
    if(analysisDays!=7&&analysisDays!=14){responseResult=failure("Vastearvio käyttää 7 tai 14 päivän nykyjaksoa. Valitse 1 tai 2 viikkoa.");emit changed();return;}
    const auto zone=QTimeZone(p["timezone"].toString().toUtf8());const qint64 refFrom=QDateTime::fromMSecsSinceEpoch(from,zone).addDays(-referenceDays).toMSecsSinceEpoch();auto episodes=db.episodes();
    auto unsafe=[&](qint64 a,qint64 b){const auto keys=db.artifactKeys(a,b);for(auto r:db.readings(a,b))if(!keys.contains(r.key)&&r.mmol<number(p,"hypo"))return true;return false;};
    responseResult=responseEstimate(p,policy,episodes,from,to,unsafe(from,to),QDateTime::currentMSecsSinceEpoch());responseResult["sourceRevision"]=db.revision();responseResult["policy"]=policy;const auto now=QDateTime::currentMSecsSinceEpoch();const auto todayStart=QDateTime(QDateTime::fromMSecsSinceEpoch(now,zone).date(),QTime(0,0),zone).toMSecsSinceEpoch();responseResult["unsafe"]=responseResult["unsafe"].toBool()||unsafe(todayStart,now);responseResult["currentSafetyUntil"]=double(now);responseResult["artifactReviews"]=db.artifactReviews();
    responseResult["short"]=responseEstimate(p,policy,episodes,QDateTime::fromMSecsSinceEpoch(to,zone).addDays(-7).toMSecsSinceEpoch(),to,unsafe(from,to),QDateTime::currentMSecsSinceEpoch());
    responseResult["reference"]=responseEstimate(p,policy,episodes,refFrom,from,unsafe(refFrom,from),QDateTime::currentMSecsSinceEpoch());responseResult["comparison"]=responseComparison(responseResult,responseResult["reference"].toObject(),policy);
    QJsonArray trend;for(int offset=referenceDays;offset>0;offset-=14){const qint64 a=QDateTime::fromMSecsSinceEpoch(from,zone).addDays(-offset).toMSecsSinceEpoch(),b=QDateTime::fromMSecsSinceEpoch(from,zone).addDays(-std::max(0,offset-14)).toMSecsSinceEpoch();auto historical=db.responseProfileAt(a);if(!historical.isEmpty()&&db.responseProfileAt(b-1)["version"]==historical["version"]){auto item=responseEstimate(historical,policy,episodes,a,b,unsafe(a,b),QDateTime::currentMSecsSinceEpoch());item["comparable"]=historical["rapidMedicine"]==p["rapidMedicine"]&&historical["basalMedicine"]==p["basalMedicine"]&&historical["basalDose"]==p["basalDose"]&&historical["actionModel"]==p["actionModel"];trend<<item;}else trend<<QJsonObject{{"from",double(a)},{"to",double(b)},{"status","regimen_changed"}};}
    responseResult["trend"]=trend;
    auto compatible=[&](const QJsonObject&h){return h["rapidMedicine"]==p["rapidMedicine"]&&h["basalMedicine"]==p["basalMedicine"]&&h["basalDose"]==p["basalDose"]&&h["actionModel"]==p["actionModel"];};
    auto referenceProfile=db.responseProfileAt(refFrom);QJsonObject reference;
    if(!referenceProfile.isEmpty()&&db.responseProfileAt(from-1)["version"]==referenceProfile["version"]&&compatible(referenceProfile)){reference=responseEstimate(referenceProfile,policy,episodes,refFrom,from,unsafe(refFrom,from),QDateTime::currentMSecsSinceEpoch());responseResult["referenceSelection"]="entire_window";}
    else {for(int i=trend.size()-1;i>=0;--i){auto h=trend[i].toObject();if(h["comparable"].toBool()&&h["status"]=="evaluated"){reference=h;break;}}responseResult["referenceSelection"]="nearest_comparable_epoch";}
    responseResult["reference"]=reference;responseResult["comparison"]=responseComparison(responseResult,reference,policy);
    responseResult["referenceDays"]=referenceDays;responseRevision=db.revision();responseDraft={};db.saveCalculation("response-v2",responseResult);m_status="Vastearvio laskettu. Arviot ja käytössä olevat hoitoasetukset ovat erillisiä.";emit changed();
}
QVariantMap Backend::proposeResponse(int group){if(responseRevision!=db.revision())return failure("Lähdetiedot ovat muuttuneet; laske vaste uudelleen.").toVariantMap();responseDraft=responseProposal(evaluationProfile(),QJsonObject::fromVariantMap(responsePolicy()),responseResult,group,QDateTime::currentMSecsSinceEpoch());responseDraft["sourceRevision"]=db.revision();db.saveCalculation("response-proposal",responseDraft);return responseDraft.toVariantMap();}
QVariantMap Backend::acceptResponse(){auto p=db.activeProfile(),policy=QJsonObject::fromVariantMap(responsePolicy());if(responseRevision!=db.revision()||responseDraft["sourceRevision"].toString()!=db.revision()||responseDraft["profileVersion"]!=p["version"])return failure("Ehdotus tai aineisto on vanhentunut.").toVariantMap();const auto current=responseProposal(p,policy,responseResult,responseDraft["group"].toInt(),QDateTime::currentMSecsSinceEpoch());if(current["status"]!="proposal"||current["candidate"]!=responseDraft["candidate"])return failure("Ehdotus ei enää täytä ehtoja.").toVariantMap();auto next=applyResponseProposal(p,policy,responseDraft,QDateTime::currentMSecsSinceEpoch());if(next.isEmpty()||!validateHistoricalProfile(next).isEmpty())return failure("Uusi profiili ei kelpaa.").toVariantMap();if(!db.run("BEGIN IMMEDIATE"))return failure("Profiilin tallennus epäonnistui.").toVariantMap();const int version=db.saveProfile(next);bool ok=version>0;if(ok)ok=db.run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)",{QDateTime::currentMSecsSinceEpoch(),"response-proposal-accepted",encoded(QJsonObject{{"version",version},{"reviewer",p["reviewer"]},{"proposal",responseDraft}})});if(ok)ok=db.run("COMMIT");if(!ok){db.run("ROLLBACK");return failure("Profiilin tallennus epäonnistui.").toVariantMap();}selectedProfileVersion=version;responseDraft={};responseResult={};refresh();return QVariantMap{{"status","saved"},{"version",version}};}
QVariantList Backend::responseCandidates()const{
    const auto p=evaluationProfile();const auto zone=QTimeZone(p.value("timezone").toString("Europe/Helsinki").toUtf8());const auto from=qint64(m_analysis.value("from").toDouble()),to=qint64(m_analysis.value("to").toDouble());QVariantList out;const auto events=db.amounts(from-15*60000,to);QSet<QString> consumed;
    for(auto v:events){auto a=v.toObject();const auto kind=a["kind"].toString(),id=a["id"].toString();const qint64 at=qint64(a["time"].toDouble());if(at<from||a["analysisExcluded"].toBool())continue;
        QStringList ids;QString category;if(kind=="carbs"){category="icr";ids<<id;for(auto w:events){auto b=w.toObject();if(b["kind"]=="rapid"&&std::abs(qint64(b["time"].toDouble())-at)<=15*60000){ids<<b["id"].toString();consumed.insert(b["id"].toString());}}if(ids.size()<2)continue;}
        else if(kind=="rapid"&&a["purpose"]=="korjaus"&&!consumed.contains(id)){category="isf";ids<<id;bool meal=false;for(auto w:events){auto b=w.toObject();if(b["kind"]=="carbs"&&std::abs(qint64(b["time"].toDouble())-at)<=15*60000)meal=true;}if(meal)continue;}
        else continue;
        out<<QVariantMap{{"kind",category},{"startText",QDateTime::fromMSecsSinceEpoch(at,zone).toString("yyyy-MM-dd HH:mm")},{"linkedIds",ids}};
    }return out;
}

bool Backend::reviewGlucoseArtifact(const QString&from,const QString&to,const QString&reason,const QString&evidence,bool confirmed){const auto p=evaluationProfile();if(!confirmed||!responseProfileApproved(p)||!validateResponseProfile(p).isEmpty()||p["reviewer"].toString().trimmed().isEmpty()){m_status="Artefaktin arvio tarvitsee vahvistuksen ja hyväksytyn profiilin tarkistajan.";emit changed();return false;}const auto zone=QTimeZone(p["timezone"].toString().toUtf8());auto parse=[&](const QString&s){auto t=QDateTime::fromString(s,"yyyy-MM-dd HH:mm");return QDateTime(t.date(),t.time(),zone);};auto a=parse(from),b=parse(to);if(!a.isValid()||!b.isValid()||b<=a||b.toMSecsSinceEpoch()>QDateTime::currentMSecsSinceEpoch()){m_status="Tarkista toteutunut artefaktijakso.";emit changed();return false;}bool ok=db.reviewArtifact(a.toMSecsSinceEpoch(),b.toMSecsSinceEpoch(),reason,evidence,p["reviewer"].toString());m_status=ok?"Artefaktin tarkistus tallennettu. Riippuvat jaksot pitää tarkistaa uudelleen; alkuperäiset mittaukset säilyvät.":"Artefaktin tarkistusta ei tallennettu.";refresh();return ok;}
bool Backend::removeGlucoseArtifact(const QString&id){bool ok=db.deactivateArtifact(id);responseResult={};responseDraft={};refresh();return ok;}
