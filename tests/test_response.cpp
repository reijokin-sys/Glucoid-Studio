// SPDX-License-Identifier: GPL-3.0-or-later
#include "response.h"
#include "profile_fixture.h"
#include <QtTest>
#include <QTimeZone>
using namespace Studio;
#include "response_fixture.h"
class ResponseTest:public QObject{
 Q_OBJECT
private slots:
 void unknownPrescribedIsfDoesNotChangeObservedResponse(){
    auto p=profile();p["approved"]=false;p["responseApproved"]=true;p["target"]=QJsonValue();
    auto periods=p["slots"].toArray();auto slot=periods[0].toObject();slot["isf"]=QJsonValue();periods[0]=slot;p["slots"]=periods;
    const auto original=p;
    QVERIFY(validateResponseProfile(p).isEmpty());QVERIFY(!validateHistoricalProfile(p).isEmpty());QVERIFY(!validateProfile(p).isEmpty());
    auto r=responseEstimate(p,policy(),episodes(),beginning(),beginning()+14LL*86400000,false);
    QCOMPARE(r["status"].toString(),"evaluated");QCOMPARE(r["groups"],estimate()["groups"]);QVERIFY(!r["medicationApproved"].toBool());QCOMPARE(p,original);
    QCOMPARE(responseProposal(p,policy(),r,0)["status"].toString(),"blocked");QVERIFY(applyResponseProposal(p,policy(),responseProposal(profile(),policy(),estimate(),0),beginning()+15LL*86400000).isEmpty());
    p["approved"]=true;QCOMPARE(responseProposal(p,policy(),r,0)["status"].toString(),"blocked");
    p["approved"]=false;p["responseApproved"]=false;p["observationActive"]=true;
    QCOMPARE(responseEstimate(p,policy(),episodes(),beginning(),beginning()+14LL*86400000,false)["status"].toString(),"blocked");
 }
 void invalidCurrentParametersAreNotUnknown_data(){
    QTest::addColumn<QString>("key");QTest::addColumn<QJsonValue>("value");
    for(const auto &key:QStringList{"isf","target"}){
        QTest::newRow(qPrintable(key+" zero"))<<key<<QJsonValue(0);
        QTest::newRow(qPrintable(key+" negative"))<<key<<QJsonValue(-1);
        QTest::newRow(qPrintable(key+" text"))<<key<<QJsonValue("unknown");
        QTest::newRow(qPrintable(key+" boolean"))<<key<<QJsonValue(true);
    }
 }
 void invalidCurrentParametersAreNotUnknown(){
    QFETCH(QString,key);QFETCH(QJsonValue,value);auto p=profile();
    if(key=="isf"){auto periods=p["slots"].toArray();auto s=periods[0].toObject();s[key]=value;periods[0]=s;p["slots"]=periods;}else p[key]=value;
    QVERIFY(!validateResponseProfile(p).isEmpty());
 }
 void responseApprovalStillRequiresOtherProfileAndPolicyChecks(){
    auto p=profile();p["approved"]=false;p["responseApproved"]=true;
    for(auto key:QStringList{"reviewer","rapidMedicine","basalMedicine","hypo","actionModel","rules"}){auto missing=p;missing.remove(key);QCOMPARE(responseEstimate(missing,policy(),episodes(),beginning(),beginning()+14LL*86400000,false)["status"].toString(),"blocked");}
    auto unapproved=policy();unapproved["approved"]=false;QCOMPARE(responseEstimate(p,unapproved,episodes(),beginning(),beginning()+14LL*86400000,false)["status"].toString(),"blocked");
    auto result=responseEstimate(p,policy(),episodes(),beginning(),beginning()+14LL*86400000,false);
    QCOMPARE(result["groups"].toArray()[0].toObject()["status"].toString(),"estimated");
    QCOMPARE(responseProposal(p,policy(),result,0)["status"].toString(),"blocked");
 }
 void calendarWeeksAcrossDaylightSaving(){auto z=QTimeZone("Europe/Helsinki");auto from=QDateTime(QDate(2026,10,15),QTime(0,0),z).toMSecsSinceEpoch(),to=QDateTime(QDate(2026,10,29),QTime(0,0),z).toMSecsSinceEpoch();auto r=responseEstimate(profile(),policy(),{},from,to,false);QCOMPARE(qint64(r["splitTime"].toDouble()),QDateTime(QDate(2026,10,22),QTime(0,0),z).toMSecsSinceEpoch());}

 void withinPeriodDriftBlocks(){auto e=episodes();for(int i=0;i<e.size();i++){auto o=e[i].toObject();if(o["kind"]=="isf"&&o["start"].toDouble()>beginning()+7LL*86400000)o["lastGlucose"]=o["firstGlucose"].toDouble()-(o["firstGlucose"].toDouble()-o["lastGlucose"].toDouble())*.5;e[i]=o;}auto r=estimate(e);for(auto v:r["groups"].toArray()){auto g=v.toObject();QCOMPARE(g["isfStatus"].toString(),"insufficient");QVERIFY(g["status"]!="estimated");}}
 void missingEpisodesAreNotZeroDoses(){auto e=episodes();for(int i=0;i<e.size();i++){auto o=e[i].toObject();if(o["kind"]=="icr")o["complete"]=false;e[i]=o;}auto r=estimate(e);for(auto v:r["groups"].toArray())QCOMPARE(v.toObject()["meals"].toInt(),0);}

 void missingPolicyBlocks(){QVERIFY(!validateResponsePolicy(defaultResponsePolicy()).isEmpty());QVERIFY2(validateResponsePolicy(policy()).isEmpty(),qPrintable(validateResponsePolicy(policy()).join('\n')));auto p=policy();p["approved"]=false;QCOMPARE(responseEstimate(profile(),p,episodes(),beginning(),beginning()+14LL*86400000,false)["status"].toString(),"blocked");}
 void timeGroupsAndMidnight(){auto p=policy();auto z=QTimeZone("Europe/Helsinki");QCOMPARE(responsePeriod(p,QDateTime(QDate(2026,10,1),QTime(2,0),z).toMSecsSinceEpoch(),"Europe/Helsinki"),3);QCOMPARE(responsePeriod(p,QDateTime(QDate(2026,10,1),QTime(6,0),z).toMSecsSinceEpoch(),"Europe/Helsinki"),0);QCOMPARE(responsePeriod(p,QDateTime(QDate(2026,10,1),QTime(14,0),z).toMSecsSinceEpoch(),"Europe/Helsinki"),2);}
 void recoversKnownFourResponses(){auto r=estimate();QCOMPARE(r["status"].toString(),"evaluated");double sensitivities[]={2,1.5,2.5,1.8},ratios[]={10,12,8,15};for(int i=0;i<4;i++){auto g=r["groups"].toArray()[i].toObject();QCOMPARE(g["status"].toString(),"estimated");QVERIFY(std::abs(g["isf"].toDouble()-sensitivities[i])<1e-9);QVERIFY(std::abs(g["ratio"].toDouble()-ratios[i])<1e-9);QVERIFY(g["ratioLow"].toDouble()<=ratios[i]+1e-9);QVERIFY(g["ratioHigh"].toDouble()>=ratios[i]-1e-9);}}
 void dosePercentAndOnlyOnePeriod(){auto r=estimate();auto proposal=responseProposal(profile(),policy(),r,0);QCOMPARE(proposal["status"].toString(),"proposal");QVERIFY(std::abs(proposal["doseChangePercent"].toDouble()-5)<1e-8);QVERIFY(std::abs(proposal["candidate"].toDouble()-20/1.05)<1e-8);auto next=applyResponseProposal(profile(),policy(),proposal,beginning()+15LL*86400000);QVERIFY(validateHistoricalProfile(next).isEmpty());auto z=QTimeZone("Europe/Helsinki");auto morning=slotProfile(next,slotAt(next,QDateTime(QDate(2026,10,20),QTime(8,0),z).toMSecsSinceEpoch()));auto night=slotProfile(next,slotAt(next,QDateTime(QDate(2026,10,20),QTime(2,0),z).toMSecsSinceEpoch()));QVERIFY(morning["icr"]!=night["icr"]);QCOMPARE(night["icr"].toInt(),20);QCOMPARE(morning["isf"].toInt(),2);QCOMPARE(next["basalDose"].toInt(),16);}
 void excludedHypoStillBlocksIncrease(){auto e=episodes();auto bad=e[0].toObject();bad["hypoObserved"]=true;bad["contextClear"]=false;e[0]=bad;auto r=estimate(e);QVERIFY(r["unsafe"].toBool());QCOMPARE(responseProposal(profile(),policy(),r,0)["status"].toString(),"blocked");}
 void rawSafetyBlocksIncrease(){QCOMPARE(responseProposal(profile(),policy(),estimate(episodes(),true),0)["status"].toString(),"blocked");}
 void oneOddDayAndManyMealsDoNotDominate(){auto e=episodes();auto odd=e[6].toObject();odd["kind"]="icr";odd["mealTime"]=odd["start"];odd["rapid"]=4;odd["carbs"]=80;odd["lastGlucose"]=40;for(int i=0;i<30;i++){odd["id"]=QString("odd%1").arg(i);e<<odd;}auto r=estimate(e);QVERIFY(std::abs(r["groups"].toArray()[0].toObject()["ratio"].toDouble()-10)<1e-9);}
 void missingCorrectionDoesNotBorrowOtherTime(){auto e=episodes();QJsonArray filtered;for(auto v:e){auto o=v.toObject();if(o["kind"]=="isf"&&responsePeriod(policy(),qint64(o["start"].toDouble()),"Europe/Helsinki")==2)continue;filtered<<o;}auto r=estimate(filtered);QVERIFY(r["groups"].toArray()[2].toObject()["status"]!="estimated");QCOMPARE(r["groups"].toArray()[0].toObject()["status"].toString(),"estimated");}
 void conditionalExternalIsNotEmpirical(){auto p=policy();auto groups=p["groups"].toArray();auto g=groups[0].toObject();g["externalIsf"]=2;groups[0]=g;p["groups"]=groups;QJsonArray filtered;for(auto v:episodes()){auto e=v.toObject();if(e["kind"]!="isf")filtered<<e;}auto r=responseEstimate(profile(),p,filtered,beginning(),beginning()+14LL*86400000,false);auto row=r["groups"].toArray()[0].toObject();QCOMPARE(row["status"].toString(),"conditional");QVERIFY(!row.contains("isf"));QCOMPARE(responseProposal(profile(),p,r,0)["status"].toString(),"blocked");}
 void unchangedAndChangedReference(){auto recent=estimate(),same=estimate();auto c=responseComparison(recent,same,policy());QCOMPARE(c["groups"].toArray()[0].toObject()["dosePerCarbChanged"].toBool(),false);auto old=episodes();for(int i=0;i<old.size();i++){auto e=old[i].toObject();if(e["kind"]=="icr"){e["lastGlucose"]=6;e["rapid"]=4;old[i]=e;}}auto past=estimate(old);auto diff=responseComparison(recent,past,policy());QVERIFY(diff["groups"].toArray()[0].toObject()["dosePerCarbChanged"].toBool());}
 void policyChangeAndShortReviewBlock(){auto r=estimate();auto p=policy();p["doseStepPercent"]=6;QCOMPARE(responseProposal(profile(),p,r,0)["status"].toString(),"blocked");auto treatment=profile();treatment["effectiveFrom"]=r["to"].toDouble()-86400000;QCOMPARE(responseProposal(treatment,policy(),r,0)["status"].toString(),"blocked");}
 void overnightFollowupNotTruncated(){auto e=episodes();const auto end=beginning()+14LL*86400000;for(int i=0;i<e.size();i++){auto row=e[i].toObject();if(row["id"]=="13-3"){row["start"]=double(end-3600000);row["mealTime"]=row["start"];row["doseTime"]=row["start"];row["end"]=double(end+3*3600000);e[i]=row;}}auto full=responseEstimate(profile(),policy(),e,beginning(),end,false,end+4*3600000);QCOMPARE(full["groups"].toArray()[3].toObject()["meals"].toInt(),7);auto limited=responseEstimate(profile(),policy(),e,beginning(),end,false);QCOMPARE(limited["groups"].toArray()[3].toObject()["meals"].toInt(),6);}
 void staleHistoricalEstimateDoesNotChangeCurrentCare(){auto r=estimate();QCOMPARE(responseProposal(profile(),policy(),r,0,beginning()+30LL*86400000)["status"].toString(),"blocked");}
 void changedEndpointMethodNeedsReview(){auto e=episodes();for(int i=0;i<e.size();i++){auto row=e[i].toObject();row["endpointMinutes"]=30;e[i]=row;}auto r=estimate(e);QVERIFY(r["groups"].toArray()[0].toObject()["status"]!="estimated");}
 void dosingMismatchDoesNotChangePrescription(){auto r=estimate();auto outcomes=r["mealOutcomes"].toArray();for(int i=0;i<outcomes.size();i++){auto o=outcomes[i].toObject();o["dose"]=1;outcomes[i]=o;}r["mealOutcomes"]=outcomes;QCOMPARE(responseProposal(profile(),policy(),r,0)["status"].toString(),"blocked");}
 void nonsensicalMealRatioIsNotClipped(){auto e=episodes();for(int i=0;i<e.size();i++){auto o=e[i].toObject();if(o["id"]=="1-0"){o["firstGlucose"]=9;o["lastGlucose"]=.1;e[i]=o;}}auto r=estimate(e);QVERIFY(r["groups"].toArray()[0].toObject()["status"]!="estimated");}
 void normalEndpointsDoNotDemandIncrease(){auto r=estimate();auto outcomes=r["mealOutcomes"].toArray();for(int i=0;i<outcomes.size();i++){auto e=outcomes[i].toObject();e["endGlucose"]=7;outcomes[i]=e;}r["mealOutcomes"]=outcomes;QCOMPARE(responseProposal(profile(),policy(),r,0)["status"].toString(),"blocked");}
};
QTEST_GUILESS_MAIN(ResponseTest)
#include "test_response.moc"
