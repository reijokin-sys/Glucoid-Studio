// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "profile_fixture.h"
#include "response_fixture.h"
#include "observation_fixture.h"
#include "fixtures/glucoid_summary_fixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QTimeZone>
#include <cmath>
#include <QProcess>
#include <QFileInfo>

class BackendTest:public QObject{
    Q_OBJECT
    static qint64 start(){auto dt=QDateTime::currentDateTime(QTimeZone("Europe/Helsinki")).addDays(-2);return QDateTime(dt.date(),QTime(8,0),dt.timeZone()).toMSecsSinceEpoch();}
    static QJsonObject prepare(Store&s){auto p=testProfile();p["effectiveFrom"]=double(QDateTime::currentMSecsSinceEpoch()-30LL*86400000);s.saveProfile(p);return p;}
    static QList<Reading> samples(qint64 at){QList<Reading> rows;for(int minute=-240;minute<=360;minute+=5){Reading r;r.key=QString::number(at)+":"+QString::number(minute);r.sensor="synthetic";r.time=at+minute*60000;r.mmol=minute<240?6:9;r.original=r.mmol;r.originalUnit="mmol/L";rows<<r;}return rows;}
    static QVariantMap episode(qint64 at,const QString&kind,const QStringList&ids){return {{"kind",kind},{"startText",QDateTime::fromMSecsSinceEpoch(at,QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm")},{"linkedIds",ids},{"reviewed",true},{"complete",true},{"contextClear",true},{"basalStable",true},{"basalHistoryComplete",true}};}
private slots:
    void standardSummaryIgnoresResearchSwitchesAndRetainsData(){
        const GlucoidFixture::SummaryPolicy f;
        QTemporaryDir dir;Store db(dir.path()+"/studio.sqlite3");
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
        QTemporaryDir dir;Store db(dir.path()+"/studio.sqlite3");
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
            QTemporaryDir dir;Store db(dir.path()+"/studio.sqlite3");prepare(db);
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
        QTemporaryDir dir;Store db(dir.path()+"/studio.sqlite3");prepare(db);
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
        QTemporaryDir oldDir;Store oldDb(oldDir.path()+"/studio.sqlite3");prepare(oldDb);
        const auto at=start();QVERIFY(oldDb.ingest(samples(at),{},at-240*60000,at+361*60000,false));
        Backend old(oldDir.path());const auto stale=old.currentReading();
        QCOMPARE(stale["hasData"].toBool(),true);QCOMPARE(stale["value"].toDouble(),9.0);QVERIFY(stale["stale"].toBool());QCOMPARE(stale["status"].toString(),QString("stale"));
    }
    void nutritionRemovalPreservesSavedDataAndIgnoresProt(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");const auto at=start();
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
    void uncheckedEpisodeIsNotSaved_data(){
        QTest::addColumn<bool>("response");QTest::addColumn<QString>("flag");QTest::addColumn<bool>("missing");
        for(bool response:{false,true})for(auto flag:QStringList{"reviewed","complete","contextClear"})for(bool missing:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(response?"v2":"v1",flag,missing?"missing":"false")))<<response<<flag<<missing;
    }
    void uncheckedEpisodeIsNotSaved(){
        QFETCH(bool,response);QFETCH(QString,flag);QFETCH(bool,missing);
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();QVERIFY(s.ingest(samples(at),{},at-240*60000,at+361*60000,false));
        auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");
        for(int day=0;day<3;++day)s.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
        Backend b(dir.path());QCOMPARE(b.saveResponsePolicy(::policy().toVariantMap(),true)["status"].toString(),"saved");
        auto valid=episode(at,"icr",{c,u});valid["endStable"]=true;auto input=valid;if(missing)input.remove(flag);else input[flag]=false;
        const auto revision=s.revision();auto result=response?b.saveResponseEpisode(input):b.saveEpisode(input);
        QCOMPARE(result["status"].toString(),"blocked");QVERIFY(s.episodes().isEmpty());QCOMPARE(s.revision(),revision);
        result=response?b.saveResponseEpisode(valid):b.saveEpisode(valid);QCOMPARE(result["status"].toString(),"saved");QCOMPARE(s.episodes().size(),1);
        QCOMPARE((response?b.saveResponseEpisode(valid):b.saveEpisode(valid))["status"].toString(),"blocked");QCOMPARE(s.episodes().size(),1);
    }
    void episodeRevisionFailureIsAtomic(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();QVERIFY(s.ingest(samples(at),{},at-240*60000,at+361*60000,false));
        auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");Backend b(dir.path());
        const auto revision=s.revision();const auto audit=s.query("SELECT * FROM audit");
        QVERIFY(s.run("CREATE TRIGGER fail_episode_revision BEFORE UPDATE ON meta WHEN NEW.key='revision' BEGIN SELECT RAISE(ABORT,'synthetic revision failure'); END"));
        QCOMPARE(b.saveEpisode(episode(at,"icr",{c,u}))["status"].toString(),"blocked");QVERIFY(s.episodes().isEmpty());QCOMPARE(s.revision(),revision);QCOMPARE(s.query("SELECT * FROM audit"),audit);
    }
    void responseAndLegacyReviewsAreIndependentInEitherOrder(){
        for(bool responseFirst:{false,true}){
            QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);
            auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");for(int day=0;day<3;++day)s.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
            Backend b(dir.path());b.saveResponsePolicy(::policy().toVariantMap(),true);auto input=episode(at,"icr",{c,u});input["endStable"]=true;
            QCOMPARE((responseFirst?b.saveResponseEpisode(input):b.saveEpisode(input))["status"].toString(),"saved");
            QCOMPARE((responseFirst?b.saveEpisode(input):b.saveResponseEpisode(input))["status"].toString(),"saved");QCOMPARE(s.episodes().size(),2);
        }
    }
    void overlappingEpisodeCannotBypassDuplicateCheckWithAnotherSlot(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");auto p=prepare(s);auto early=p["slots"].toArray()[0].toObject(),late=early;early["name"]="early";late["name"]="late";late["startMinute"]=480;p["slots"]=QJsonArray{early,late};auto rules=p["rules"].toObject();rules["mealWindowMinutes"]=300;p["rules"]=rules;s.saveProfile(p);
        const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");Backend b(dir.path());
        QCOMPARE(b.saveEpisode(episode(at-5*60000,"icr",{c,u}))["status"].toString(),"saved");QCOMPARE(b.saveEpisode(episode(at,"icr",{c,u}))["status"].toString(),"blocked");QCOMPARE(s.episodes().size(),1);
    }
    void responseMealUsesReviewedMedianForTargetCheck(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();auto rows=samples(at);for(auto &r:rows)if(r.time==at){r.mmol=12;r.original=12;}s.ingest(rows,{},at-240*60000,at+361*60000,false);
        auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");for(int day=0;day<3;++day)s.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
        Backend b(dir.path());b.saveResponsePolicy(::policy().toVariantMap(),true);auto input=episode(at,"icr",{c,u});input["endStable"]=true;
        QCOMPARE(b.saveResponseEpisode(input)["status"].toString(),"saved");QCOMPARE(s.episodes()[0].toObject()["firstGlucose"].toDouble(),6.0);
    }
    void responseBasalHistoryExclusionsAndChangesAreTracked(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);
        auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");QString basal;for(int day=0;day<3;++day)basal=s.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
        Backend b(dir.path());b.saveResponsePolicy(::policy().toVariantMap(),true);auto input=episode(at,"icr",{c,u});input["endStable"]=true;
        QVERIFY(s.exclude(basal,"synthetic exclusion"));QCOMPARE(b.saveResponseEpisode(input)["status"].toString(),"blocked");QVERIFY(s.episodes().isEmpty());QVERIFY(s.exclude(basal,""));
        QCOMPARE(b.saveResponseEpisode(input)["status"].toString(),"saved");QVERIFY(!s.episodes()[0].toObject()["stale"].toBool());
        QVERIFY(s.annotate(basal,{{"note","synthetic source correction"}}));QVERIFY(s.episodes()[0].toObject()["stale"].toBool());
    }
    void reviewedEpisodeRejectsStaleUiContext(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);
        auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");Backend b(dir.path());auto input=episode(at,"icr",{c,u});input["sourceRevision"]=s.revision();input["profileVersion"]=1;
        s.addLocal(at+86400000,1,"other","","muu");QCOMPARE(b.saveEpisode(input)["status"].toString(),"blocked");QVERIFY(s.episodes().isEmpty());
        input["sourceRevision"]=s.revision();input["profileVersion"]=2;QCOMPARE(b.saveEpisode(input)["status"].toString(),"blocked");QVERIFY(s.episodes().isEmpty());input["profileVersion"]=1;QCOMPARE(b.saveEpisode(input)["status"].toString(),"saved");
    }
    void previousUncheckedRowDoesNotBlockConfirmedReview(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);
        auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");auto old=QJsonObject::fromVariantMap(episode(at,"icr",{c,u}));old["reviewed"]=false;old["start"]=double(at);old["end"]=double(at+240*60000);old["dependencyFrom"]=double(at-240*60000);old["profileVersion"]=1;old["slot"]="all";QVERIFY(!s.saveEpisode(old).isEmpty());
        Backend b(dir.path());QCOMPARE(b.saveEpisode(episode(at,"icr",{c,u}))["status"].toString(),"saved");QCOMPARE(s.episodes().size(),2);QVERIFY(!s.episodes()[0].toObject()["reviewed"].toBool());QVERIFY(s.episodes()[1].toObject()["reviewed"].toBool());
    }
    void followupCoversActualDoseTime_data(){QTest::addColumn<bool>("response");QTest::newRow("v1")<<false;QTest::newRow("v2")<<true;}
    void followupCoversActualDoseTime(){
        QFETCH(bool,response);QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");auto p=prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);
        auto c=s.addLocal(at,40,"carbs","","ateria"),u=s.addLocal(at+10*60000,4,"rapid","Synthetic rapid","ateria");for(int day=0;day<3;++day)s.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
        Backend b(dir.path());b.saveResponsePolicy(::policy().toVariantMap(),true);auto input=episode(at,"icr",{c,u});input["endStable"]=true;QCOMPARE((response?b.saveResponseEpisode(input):b.saveEpisode(input))["status"].toString(),"blocked");QVERIFY(s.episodes().isEmpty());
        auto rules=p["rules"].toObject();rules["mealWindowMinutes"]=250;p["rules"]=rules;s.saveProfile(p);QCOMPARE((response?b.saveResponseEpisode(input):b.saveEpisode(input))["status"].toString(),"saved");
    }
    void responseOnlyApprovalPersistsAndDoesNotAuthorizeMedication(){
        QTemporaryDir dir;Backend b(dir.path());auto p=::profile();p["target"]=QJsonValue();
        auto periods=p["slots"].toArray();auto slot=periods[0].toObject();slot["isf"]=QJsonValue();periods[0]=slot;p["slots"]=periods;
        p["effectiveFromText"]=QDateTime::fromMSecsSinceEpoch(qint64(p["effectiveFrom"].toDouble()),QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm");p["historicalConfirmed"]=true;
        QCOMPARE(b.activateObservationProfile(p.toVariantMap())["status"].toString(),"active");
        auto approved=b.saveResponseProfile(p.toVariantMap(),true);QCOMPARE(approved["status"].toString(),"saved");QCOMPARE(approved["version"].toInt(),2);
        Store s(dir.path()+"/studio.sqlite3");auto saved=s.activeProfile();QVERIFY(saved["responseApproved"].toBool());QVERIFY(!saved["approved"].toBool());QVERIFY(saved["approvedAt"].toString().isEmpty());QVERIFY(!saved["responseApprovedAt"].toString().isEmpty());
        QVERIFY(saved["slots"].toArray()[0].toObject()["isf"].isNull());QVERIFY(saved["target"].isNull());QCOMPARE(s.observationProfile()["version"].toInt(),1);QCOMPARE(s.profiles().size(),2);
        QVERIFY(s.profileAt(beginning()).isEmpty());QCOMPARE(s.responseProfileAt(beginning())["version"].toInt(),2);
        for(auto v: ::episodes()){auto e=v.toObject();e["profileVersion"]=2;e["dependencyFrom"]=e["start"];QVERIFY(!s.saveEpisode(e).isEmpty());}
        Backend reloaded(dir.path());QVERIFY(reloaded.profile()["responseApproved"].toBool());QCOMPARE(reloaded.saveResponsePolicy(::policy().toVariantMap(),true)["status"].toString(),"saved");reloaded.setAnalysisWindow(14,"2026-10-14");reloaded.analyseResponse(28);
        auto response=reloaded.response();QCOMPARE(response["status"].toString(),"evaluated");QVERIFY(!response["medicationApproved"].toBool());for(auto v:response["groups"].toList())QCOMPARE(v.toMap()["isfStatus"].toString(),"estimated");
        QCOMPARE(reloaded.proposeResponse(0)["status"].toString(),"blocked");QCOMPARE(reloaded.acceptResponse()["status"].toString(),"blocked");QCOMPARE(reloaded.assess("isf","all")["status"].toString(),"blocked");QCOMPARE(reloaded.bolus({})["status"].toString(),"blocked");
        auto reportPath=reloaded.saveReport();QFile reportFile(reportPath);QVERIFY(reportFile.open(QIODevice::ReadOnly));auto report=QJsonDocument::fromJson(reportFile.readAll()).object();QVERIFY(report["response"].toObject()["medicationApproved"].isBool());QVERIFY(!report["response"].toObject()["medicationApproved"].toBool());
        auto audit=s.query("SELECT data FROM audit WHERE kind='historical-profile-saved'");QCOMPARE(audit.size(),1);auto event=QJsonDocument::fromJson(audit[0].toObject()["data"].toString().toUtf8()).object();QCOMPARE(event["scope"].toString(),"response");QVERIFY(event["approved"].toBool());
        auto input=reloaded.profile();QCOMPARE(reloaded.saveAnalysisProfile(input,true)["status"].toString(),"blocked");QCOMPARE(s.profiles().size(),2);
        QCOMPARE(reloaded.saveAnalysisProfile(input,false)["status"].toString(),"saved");QVERIFY(!reloaded.profile()["responseApproved"].toBool());reloaded.analyseResponse(28);QCOMPARE(reloaded.response()["status"].toString(),"blocked");
        QCOMPARE(reloaded.saveResponseProfile(input,false)["status"].toString(),"saved");QVERIFY(!reloaded.profile()["responseApproved"].toBool());
        QCOMPARE(reloaded.activateObservationProfile(input)["status"].toString(),"active");QVERIFY(!reloaded.profile()["responseApproved"].toBool());
        QCOMPARE(reloaded.saveProfile(input,false)["status"].toString(),"saved");QVERIFY(!reloaded.profile()["responseApproved"].toBool());
    }
    void responseApprovalFailureRollsBackAndRequiresConfirmation(){
        QTemporaryDir dir;Backend b(dir.path());auto p=::profile();p["effectiveFromText"]="2026-07-01 00:00";
        QCOMPARE(b.saveResponseProfile(p.toVariantMap(),true)["status"].toString(),"blocked");p["historicalConfirmed"]=true;
        Store s(dir.path()+"/studio.sqlite3");const auto revision=s.revision();QVERIFY(s.run("CREATE TRIGGER fail_profile_audit BEFORE INSERT ON audit WHEN NEW.kind='historical-profile-saved' BEGIN SELECT RAISE(ABORT,'synthetic audit failure'); END"));
        QCOMPARE(b.saveResponseProfile(p.toVariantMap(),true)["status"].toString(),"blocked");QVERIFY(s.profiles().isEmpty());QCOMPARE(s.revision(),revision);QVERIFY(b.profile().value("version").isNull());
    }
    void correctionEpisodeCanBeReviewedWithUnknownPrescribedIsf(){
        QTemporaryDir dir;Backend b(dir.path());auto p=::profile();auto periods=p["slots"].toArray();auto slot=periods[0].toObject();slot["isf"]=QJsonValue();periods[0]=slot;p["slots"]=periods;p["target"]=QJsonValue();p["historicalConfirmed"]=true;p["effectiveFromText"]="2026-07-01 00:00";
        QCOMPARE(b.saveResponseProfile(p.toVariantMap(),true)["status"].toString(),"saved");QCOMPARE(b.saveResponsePolicy(::policy().toVariantMap(),true)["status"].toString(),"saved");
        Store s(dir.path()+"/studio.sqlite3");const auto at=start();auto rows=samples(at);for(auto &r:rows)r.mmol=r.time<at+180*60000?12:8;QVERIFY(s.ingest(rows,{},at-240*60000,at+361*60000,false));
        auto insulin=s.addLocal(at,2,"rapid","Synthetic rapid","korjaus");for(int day=0;day<3;++day)s.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
        auto input=episode(at,"isf",{insulin});input["endStable"]=true;QCOMPARE(b.saveEpisode(input)["status"].toString(),"blocked");
        input["basalHistoryComplete"]=false;QCOMPARE(b.saveResponseEpisode(input)["status"].toString(),"blocked");input["basalHistoryComplete"]=true;
        auto result=b.saveResponseEpisode(input);QVERIFY2(result["status"].toString()=="saved",qPrintable(result["reasons"].toStringList().join('\n')));auto e=s.episodes()[0].toObject();QCOMPARE(e["firstGlucose"].toDouble(),12.0);QCOMPARE(e["lastGlucose"].toDouble(),8.0);QVERIFY(e["deviation"].isNull());QCOMPARE(e["rapid"].toDouble(),2.0);
        auto from=QDateTime::fromMSecsSinceEpoch(at,QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm"),to=QDateTime::fromMSecsSinceEpoch(at+600000,QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm");QVERIFY(!b.reviewGlucoseArtifact(from,to,"synthetic","test reference",false));QVERIFY(b.reviewGlucoseArtifact(from,to,"synthetic","test reference",true));
        p["effectiveFromText"]=QDateTime::fromMSecsSinceEpoch(at+2*3600000,QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm");QCOMPARE(b.saveResponseProfile(p.toVariantMap(),true)["status"].toString(),"saved");b.selectProfile(1);QCOMPARE(b.saveResponseEpisode(input)["status"].toString(),"blocked");
    }
    void observationDayConfirmationUsesProfileTimezone(){
        QTemporaryDir dir;Backend b(dir.path());auto p=observationFixture();p["timezone"]="UTC";p["effectiveFrom"]=double(QDateTime(QDate(2026,10,1),QTime(0,0),QTimeZone::UTC).toMSecsSinceEpoch());QCOMPARE(b.activateObservationProfile(p.toVariantMap())["status"].toString(),"active");Store s(dir.path()+"/studio.sqlite3");QCOMPARE(s.meta("analysisZone"),QString("UTC"));auto at=QDateTime(QDate(2026,10,20),QTime(23,30),QTimeZone::UTC).toMSecsSinceEpoch();auto id=s.addLocal(at,16,"basal","Synthetic basal","perus");QVERIFY(s.confirmDay("2026-10-20",true));QVERIFY(s.completeDays().contains("2026-10-20"));QVERIFY(s.annotate(id,{{"purpose","perus"},{"note","synthetic correction"}}));QVERIFY(!s.completeDays().contains("2026-10-20"));
    }

    void observationActivationPersistsWithoutMedicationDefaults(){
        QTemporaryDir dir;Backend b(dir.path());auto p=observationFixture();auto settings=b.analysisSettings();settings["maxGapMinutes"]=12;settings["fillHistory"]=true;QVERIFY(b.saveAnalysisSettings(settings));
        auto r=b.activateObservationProfile(p.toVariantMap());QCOMPARE(r["status"].toString(),"active");QCOMPARE(r["version"].toInt(),1);Store store(dir.path()+"/studio.sqlite3");auto active=store.observationProfile();QVERIFY(active["observationActive"].toBool());QVERIFY(!active["approved"].toBool());QVERIFY(active["slots"].toArray()[0].toObject()["isf"].isNull());QVERIFY(active["target"].isNull());QCOMPARE(active["effectiveFrom"],p["effectiveFrom"]);QVERIFY(store.meta("responsePolicy").isEmpty());QVERIFY(!store.meta("revision").isEmpty());QCOMPARE(b.analysisSettings()["low"].toDouble(),p["targetLow"].toDouble());QCOMPARE(b.analysisSettings()["maxGapMinutes"].toInt(),12);QVERIFY(b.analysisSettings()["fillHistory"].toBool());
        Backend reloaded(dir.path());QCOMPARE(reloaded.observationProfile()["version"].toInt(),1);QVERIFY(reloaded.observation()["active"].toBool());QCOMPARE(reloaded.bolus({})["status"].toString(),"blocked");QCOMPARE(reloaded.assess("icr","all")["status"].toString(),"blocked");QCOMPARE(reloaded.activateObservationProfile(reloaded.profile())["version"].toInt(),1);QCOMPARE(store.profiles().size(),1);
    }
    void newDraftDoesNotReplaceObservedRegimen(){
        QTemporaryDir dir;Backend b(dir.path());auto p=observationFixture();QCOMPARE(b.activateObservationProfile(p.toVariantMap())["status"].toString(),"active");auto draft=QJsonObject::fromVariantMap(b.profile());draft["basalDose"]=17;QCOMPARE(b.saveAnalysisProfile(draft.toVariantMap(),false)["status"].toString(),"saved");QCOMPARE(b.profile()["version"].toInt(),2);QCOMPARE(b.observationProfile()["version"].toInt(),1);QCOMPARE(b.observationProfile()["basalDose"].toInt(),16);auto path=b.saveReport();QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));auto report=QJsonDocument::fromJson(f.readAll()).object();QCOMPARE(report["observationProfile"].toObject()["version"].toInt(),1);QCOMPARE(report["observation"].toObject()["profileVersion"].toInt(),1);QCOMPARE(b.activateObservationProfile(b.profile())["version"].toInt(),3);QCOMPARE(b.observationProfile()["basalDose"].toInt(),17);
    }
    void invalidObservationActivationLeavesWorkingProfile(){
        QTemporaryDir dir;Backend b(dir.path());QCOMPARE(b.activateObservationProfile(observationFixture().toVariantMap())["status"].toString(),"active");auto p=b.profile();p["basalDose"]=QVariant();QCOMPARE(b.activateObservationProfile(p)["status"].toString(),"blocked");Store store(dir.path()+"/studio.sqlite3");QCOMPARE(store.profiles().size(),1);QCOMPARE(store.observationProfile()["version"].toInt(),1);
    }

    void responseAcceptanceChangesOnlyOneGroup(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");auto z=QTimeZone("Europe/Helsinki");auto today=QDateTime::currentDateTime(z).date();auto from=QDateTime(today.addDays(-14),QTime(0,0),z).toMSecsSinceEpoch();auto to=QDateTime(today,QTime(0,0),z).toMSecsSinceEpoch();auto p=::profile();p["effectiveFrom"]=double(from-90LL*86400000);s.saveProfile(p);
        QList<Reading> rows;for(auto t=from;t<to;t+=300000){Reading r;r.key=QString("synthetic:%1").arg(t);r.time=t;r.sensor="synthetic";r.mmol=6;r.original=6;r.originalUnit="mmol/L";rows<<r;}QVERIFY(s.ingest(rows,{},from,to,false));
        auto dates=QDate(2026,10,1).daysTo(today.addDays(-14));for(auto v : ::episodes()){auto e=v.toObject();for(auto key:{"start","end","doseTime","mealTime"})if(e.contains(key))e[key]=double(QDateTime::fromMSecsSinceEpoch(qint64(e[key].toDouble()),z).addDays(dates).toMSecsSinceEpoch());e["dependencyFrom"]=e["start"];s.saveEpisode(e);}
        Backend b(dir.path());QCOMPARE(b.saveResponsePolicy(::policy().toVariantMap(),true)["status"].toString(),"saved");b.analyseResponse(28);QCOMPARE(b.proposeResponse(0)["status"].toString(),"proposal");
        s.setMeta("revision","changed-after-proposal");QCOMPARE(b.acceptResponse()["status"].toString(),"blocked");b.analyseResponse(28);QCOMPARE(b.proposeResponse(0)["status"].toString(),"proposal");
        auto low=rows[0];low.key="unreliable-low";low.time=to+1000;low.mmol=3;low.reliable=false;QVERIFY(s.ingest({low},{},to,to+60000,false));b.analyseResponse(28);QCOMPARE(b.proposeResponse(0)["status"].toString(),"blocked");QVERIFY(s.reviewArtifact(to,to+60000,"synthetic artifact","synthetic reference","tester"));b.analyseResponse(28);QCOMPARE(b.proposeResponse(0)["status"].toString(),"proposal");
        QCOMPARE(b.acceptResponse()["status"].toString(),"saved");auto next=s.activeProfile();QCOMPARE(next["version"].toInt(),2);QCOMPARE(next["basalDose"],p["basalDose"]);QCOMPARE(slotProfile(next,slotAt(next,QDateTime(today,QTime(2,0),z).toMSecsSinceEpoch()))["icr"].toInt(),20);QVERIFY(next.contains("acceptedResponseProposal"));QCOMPARE(b.acceptResponse()["status"].toString(),"blocked");
    }

    void artifactReviewPreservesDataAndTracksSource(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");const auto at=start();auto rows=samples(at);QVERIFY(s.ingest(rows,{},at-240*60000,at+361*60000,false));
        s.saveEpisode({{"start",double(at)},{"dependencyFrom",double(at)},{"end",double(at+600000)}});const auto count=s.readings(0,at+86400000).size();QVERIFY(!s.episodes()[0].toObject()["stale"].toBool());
        QVERIFY(!s.reviewArtifact(at,at+600000,"","synthetic reference","tester"));QVERIFY(!s.reviewArtifact(at,at+600000,"sensor artifact","","tester"));QVERIFY(!s.reviewArtifact(at,at+600000,"sensor artifact","synthetic reference",""));
        QVERIFY(s.reviewArtifact(at,at+600000,"sensor artifact","synthetic reference","tester"));QCOMPARE(s.artifactKeys(at,at+600000).size(),2);QCOMPARE(s.readings(0,at+86400000).size(),count);QVERIFY(s.episodes()[0].toObject()["stale"].toBool());
        auto review=s.artifactReviews()[0].toObject();QVERIFY(review["current"].toBool());auto updated=rows[48];updated.mmol=7;QVERIFY(s.ingest({updated},{},at,at+600000,false));QVERIFY(s.artifactKeys(at,at+600000).isEmpty());QVERIFY(!s.artifactReviews()[0].toObject()["current"].toBool());
        QVERIFY(s.deactivateArtifact(review["id"].toString()));QCOMPARE(s.artifactReviews().size(),1);QCOMPARE(s.artifactReviews()[0].toObject()["active"].toInt(),0);
    }
    void artifactReviewNeedsExplicitConfirmation(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);Backend b(dir.path());auto from=QDateTime::fromMSecsSinceEpoch(at,QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm"),to=QDateTime::fromMSecsSinceEpoch(at+600000,QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm");QVERIFY(!b.reviewGlucoseArtifact(from,to,"sensor","reference",false));QVERIFY(s.artifactReviews().isEmpty());QVERIFY(b.reviewGlucoseArtifact(from,to,"sensor","reference",true));QCOMPARE(s.artifactReviews().size(),1);}

    void mealEpisode(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);auto carbs=s.addLocal(at,40,"carbs","","ateria");auto insulin=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");Backend b(dir.path());auto r=b.saveEpisode(episode(at,"icr",{carbs,insulin}));QCOMPARE(r["status"].toString(),"saved");QCOMPARE(s.episodes().size(),1);QCOMPARE(s.episodes()[0].toObject()["deviation"].toDouble(),3.0);auto duplicate=b.saveEpisode(episode(at,"icr",{carbs,insulin}));QCOMPARE(duplicate["status"].toString(),"blocked");}
    void purposeRequired(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);auto carbs=s.addLocal(at,40,"carbs","","ateria");auto insulin=s.addLocal(at,4,"rapid","Synthetic rapid","");Backend b(dir.path());QCOMPARE(b.saveEpisode(episode(at,"icr",{carbs,insulin}))["status"].toString(),"blocked");}
    void missingCarbs(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);auto insulin=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");Backend b(dir.path());QCOMPARE(b.saveEpisode(episode(at,"icr",{insulin}))["status"].toString(),"blocked");}
    void overlappingInsulin(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);auto carbs=s.addLocal(at,40,"carbs","","ateria");auto insulin=s.addLocal(at,4,"rapid","Synthetic rapid","ateria");s.addLocal(at+60*60000,1,"rapid","Synthetic rapid","korjaus");Backend b(dir.path());QCOMPARE(b.saveEpisode(episode(at,"icr",{carbs,insulin}))["status"].toString(),"blocked");}
    void basalHistoryRequired(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto at=start();s.ingest(samples(at),{},at-240*60000,at+361*60000,false);Backend b(dir.path());QCOMPARE(b.saveEpisode(episode(at,"basal",{}))["status"].toString(),"blocked");for(int day=0;day<3;++day)s.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");QCOMPARE(b.saveEpisode(episode(at,"basal",{}))["status"].toString(),"saved");}
    void profileVersionAndDraft(){QTemporaryDir dir;Backend b(dir.path());auto p=testProfile();auto r=b.saveProfile(p.toVariantMap(),true);QCOMPARE(r["status"].toString(),"saved");QCOMPARE(b.profile()["version"].toInt(),1);p.remove("maxBolus");QCOMPARE(b.saveProfile(p.toVariantMap(),true)["status"].toString(),"blocked");QCOMPARE(b.profile()["version"].toInt(),1);QCOMPARE(b.saveProfile(p.toVariantMap(),false)["status"].toString(),"saved");QCOMPARE(b.profile()["version"].toInt(),2);QVERIFY(!b.profile()["approved"].toBool());}
    void removeAndRestoreInView(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");const auto time=QDateTime::currentMSecsSinceEpoch()-60000;auto id=s.addLocal(time,40,"carbs","","ateria");Backend b(dir.path());QCOMPARE(b.diary().size(),1);QVERIFY(b.removeAmount(id));QCOMPARE(b.diary().size(),0);QCOMPARE(b.deletedDiary().size(),1);QVERIFY(b.restoreAmount(id));QCOMPARE(b.diary().size(),1);QCOMPARE(b.deletedDiary().size(),0);}
    void measuredBolusAndUnconfirmedHistory(){QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const qint64 t=QDateTime::currentMSecsSinceEpoch();Reading r;r.key="fresh";r.sensor="synthetic";r.time=t-60000;r.mmol=10;r.original=10;r.originalUnit="mmol/L";s.ingest({r},{},t-120000,t,false);Backend b(dir.path());QVariantMap in{{"carbs",60},{"measurementConfirmed",true},{"historyComplete",true},{"carbsComplete",true},{"contextReviewed",true}};QCOMPARE(b.bolus(in)["units"].toDouble(),8.0);in["historyComplete"]=false;QCOMPARE(b.bolus(in)["status"].toString(),"blocked");}
    void retrospectiveProfileAndOldMeal(){
        QTemporaryDir dir;Backend b(dir.path());auto p=testProfile();for(auto key:{"maxGlucoseAgeMinutes","maxBolus","doseIncrement","rounding"})p.remove(key);
        auto action=p["actionModel"].toObject();action.remove("curve");p["actionModel"]=action;
        p["effectiveFromText"]=QDateTime::currentDateTime(QTimeZone("Europe/Helsinki")).addDays(-30).toString("yyyy-MM-dd HH:mm");p["historicalConfirmed"]=true;
        auto result=b.saveAnalysisProfile(p.toVariantMap(),true);QCOMPARE(result["status"].toString(),"saved");
        Store store(dir.path()+"/studio.sqlite3");const auto at=start();QVERIFY(store.ingest(samples(at),{},at-240*60000,at+361*60000,false));
        const auto carbs=store.addLocal(at,40,"carbs","","ateria"),insulin=store.addLocal(at,4,"rapid","Synthetic rapid","ateria");
        QCOMPARE(b.saveEpisode(episode(at,"icr",{carbs,insulin}))["status"].toString(),"saved");
        const auto report=b.saveReport();QVERIFY(!report.isEmpty());QVERIFY(QFile::exists(report));QCOMPARE(store.query("SELECT * FROM analysis_runs").size(),1);
    }
    void historicalProfileBoundaryBlocksEpisode(){
        QTemporaryDir dir;Backend b(dir.path());auto p=testProfile();auto zone=QTimeZone("Europe/Helsinki");const auto at=start();
        p["historicalConfirmed"]=true;p["effectiveFromText"]=QDateTime::fromMSecsSinceEpoch(at-86400000,zone).toString("yyyy-MM-dd HH:mm");QCOMPARE(b.saveAnalysisProfile(p.toVariantMap(),true)["status"].toString(),"saved");
        p["effectiveFromText"]=QDateTime::fromMSecsSinceEpoch(at+2*3600000,zone).toString("yyyy-MM-dd HH:mm");QCOMPARE(b.saveAnalysisProfile(p.toVariantMap(),true)["status"].toString(),"saved");b.selectProfile(1);
        Store store(dir.path()+"/studio.sqlite3");store.ingest(samples(at),{},at-240*60000,at+361*60000,false);auto c=store.addLocal(at,40,"carbs","","ateria"),u=store.addLocal(at,4,"rapid","Synthetic rapid","ateria");QCOMPARE(b.saveEpisode(episode(at,"icr",{c,u}))["status"].toString(),"blocked");
    }
    void fileImportPreservesProvenance(){QTemporaryDir dir,source;QFile g(source.path()+"/stream.tsv");QVERIFY(g.open(QIODevice::WriteOnly));g.write("Sensorid\tnr\tUnixTime\tmmol/L\nsynthetic\t1\t1760000000\t6\n");g.close();QFile a(source.path()+"/amounts.tsv");QVERIFY(a.open(QIODevice::WriteOnly));a.write("Source\tnr\tUnixTime\tValue\tLabel\n0\t1\t1760000000\t4\tSynthetic rapid\n");a.close();Backend b(dir.path());QVERIFY(b.importFolder(source.path()));Store s(dir.path()+"/studio.sqlite3");QCOMPARE(s.query("SELECT * FROM import_chunks").size(),2);QCOMPARE(s.imports()[0].toObject()["transport"].toString(),"files-additive");QVERIFY(b.importFolder(source.path()));QCOMPARE(s.amounts(0,1800000000000LL).size(),1);
        const auto backup=b.backup();QVERIFY(!backup.isEmpty());QVERIFY(QFile::exists(backup+".manifest.json"));
        QProcess restore;restore.start("python3",{QStringLiteral(STUDIO_SOURCE_DIR)+"/tools/restore-backup.py","--backup",backup,"--data-dir",dir.path()+"/restored"});QVERIFY(restore.waitForFinished());QVERIFY2(restore.exitCode()==0,restore.readAllStandardError().constData());
        Store restored(dir.path()+"/restored/studio.sqlite3");QVERIFY(restored.good());QCOMPARE(restored.readings(0,1800000000000LL).size(),1);QCOMPARE(restored.amounts(0,1800000000000LL).size(),1);for(auto v:restored.query("SELECT path FROM import_chunks"))QVERIFY(QFile::exists(v.toObject()["path"].toString()));
    }
    void responseEpisodeUsesMediansAndKeepsLegacy(){
        QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");prepare(store);const auto at=start();store.ingest(samples(at),{},at-240*60000,at+361*60000,false);
        auto c=store.addLocal(at,40,"carbs","","ateria"),u=store.addLocal(at,4,"rapid","Synthetic rapid","ateria");for(int day=0;day<3;++day)store.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
        Backend b(dir.path());auto old=episode(at,"icr",{c,u});QCOMPARE(b.saveEpisode(old)["status"].toString(),"saved");
        auto policy=defaultResponsePolicy();policy["approved"]=true;auto groups=policy["groups"].toArray();for(int i=0;i<4;++i){auto g=groups[i].toObject();g["startMinute"]=360+i*240;groups[i]=g;}policy["groups"]=groups;
        for(auto key:{"minimumEpisodes","minimumDays","minimumBasalDays","endpointMinutes"})policy[key]=3;
        policy["endpointMinutes"]=15;for(auto key:{"minCoverage","maxRelativeWidth","maxInvalidFraction","minimumDose","doseMatchToleranceUnits"})policy[key]=.5;
        policy["maxDayInfluencePercent"]=25;policy["maxPeriodDriftPercent"]=25;policy["doseStepPercent"]=5;policy["maximumChangePercent"]=10;policy["minimumCarbs"]=5;
        QCOMPARE(b.saveResponsePolicy(policy.toVariantMap(),true)["status"].toString(),"saved");old["endStable"]=true;
        QCOMPARE(b.saveResponseEpisode(old)["status"].toString(),"saved");QCOMPARE(store.episodes().size(),2);auto modern=store.episodes()[1].toObject();QCOMPARE(modern["methodVersion"].toInt(),2);QCOMPARE(modern["firstGlucose"].toDouble(),6.0);QCOMPARE(modern["lastGlucose"].toDouble(),6.0);
        QVERIFY(!b.responseCandidates().isEmpty());b.analyseResponse(28);QCOMPARE(b.response()["status"].toString(),"evaluated");auto rows=b.response()["groups"].toList();QVERIFY(rows.size()==4);QCOMPARE(b.proposeResponse(0)["status"].toString(),"blocked");
    }
    void oneChangeOnlyAndFreshReview(){
        QTemporaryDir dir;Store s(dir.path()+"/studio.sqlite3");prepare(s);const auto t=QDateTime::currentMSecsSinceEpoch();
        for(int i=0;i<3;++i){const qint64 at=t-(i+5)*86400000LL;QJsonObject e{{"start",double(at)},{"end",double(at+14400000)},{"dependencyFrom",double(at)},{"kind","icr"},{"slot","all"},{"profileVersion",1},{"reviewed",true},{"complete",true},{"contextClear",true},{"coverage",1},{"deviation",3}};s.saveEpisode(e);}
        Backend b(dir.path());QCOMPARE(b.assess("icr","all")["status"].toString(),"research");s.setMeta("revision","changed");QCOMPARE(b.acceptRecommendation()["status"].toString(),"blocked");QCOMPARE(b.assess("icr","all")["status"].toString(),"research");QCOMPARE(b.acceptRecommendation()["status"].toString(),"saved");auto p=s.activeProfile();QCOMPARE(p["version"].toInt(),2);QCOMPARE(p["slots"].toArray()[0].toObject()["icr"].toDouble(),9.5);QCOMPARE(p["slots"].toArray()[0].toObject()["isf"].toDouble(),2.0);QCOMPARE(p["basalDose"].toDouble(),16.0);QCOMPARE(b.acceptRecommendation()["status"].toString(),"blocked");
    }
};
QTEST_GUILESS_MAIN(BackendTest)
#include "test_backend.moc"
