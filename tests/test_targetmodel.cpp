// SPDX-License-Identifier: GPL-3.0-or-later
#include "targetmodel.h"
#include "automatic_fixture.h"
#include <QtTest>
#include <random>
using namespace Studio;
#include "target_fixture.h"
using namespace TargetFixture;
class TargetModelTest: public QObject {
    Q_OBJECT
private slots:
    void recoversKnownEffectAndInverseDose(){auto rows=sample();auto r=run(rows);QCOMPARE(r["status"].toString(),"estimated");QVERIFY(std::abs(r["endpointEffectPerUnit"].toDouble()-1.4)<.03);
        double c=r["referenceCarbs"].toDouble(),t=r["referenceTrend"].toDouble(),timing=r["referenceBolusOffsetMinutes"].toDouble();double expected=(1+.8*5.0+.14*c+.5*t+.025*timing-5)/1.4;
        QCOMPARE(r["targetValue"].toDouble(),5.0);QCOMPARE(r["startGlucoseHeldAt"].toDouble(),5.0);
        QVERIFY(std::abs(r["units"].toDouble()-expected)<.05);QVERIFY(std::abs(r["meanPer10g"].toDouble()-10*expected/c)<.02);
        QVERIFY(r["uncertaintyHigh"].toDouble()>r["uncertaintyLow"].toDouble());QVERIFY(r["heldOutRmse"].toDouble()<.15);
        QVERIFY(!r["prescriptive"].toBool());QVERIFY(!r["causalEffectIdentified"].toBool());QVERIFY(!r["clinicallyValidated"].toBool());}
    void correctionCarriesHigherStartBackToTarget(){auto rows=sample();auto r=run(rows);QCOMPARE(r["correctionStatus"].toString(),"estimated");
        double start=r["referenceStartGlucose"].toDouble(),sens=r["endpointEffectPerUnit"].toDouble();
        QVERIFY(std::abs(r["correctionPerMmol"].toDouble()-.8/sens)<.02);
        QVERIFY(std::abs(r["correctionUnits"].toDouble()-(start-5.0)*.8/sens)<.03);
        QVERIFY(r["correctionUncertaintyLow"].toDouble()>0);QVERIFY(r["correctionUncertaintyHigh"].toDouble()>r["correctionUncertaintyLow"].toDouble());}
    void correctionIsNotShownWithoutBaselineEffect(){auto a=sample();
        // A start glucose with no carry-over: only carbs, dose, trend and timing explain the endpoint.
        for(int i=0;i<a.size();++i){auto r=a[i].toObject();double c=r["carbs"].toDouble(),u=r["units"].toDouble(),t=r["premealTrend"].toDouble(),timing=r["bolusOffsetMinutes"].toDouble();
            r["glucose2h"]=5+.168*c-1.4*u+.5*t+.025*timing;r["endpointGlucose"]=r["glucose2h"];a[i]=r;}
        auto r=run(a);QCOMPARE(r["status"].toString(),"estimated");QCOMPARE(r["correctionStatus"].toString(),"unidentifiable");
        QVERIFY(r["correctionPerMmol"].isNull());QVERIFY(r["correctionUnits"].isNull());}
    void differsFromRecordedDoseMean(){auto rows=sample();double sum=0;for(auto v:rows){auto r=v.toObject();sum+=10*r["units"].toDouble()/r["carbs"].toDouble();}QVERIFY(std::abs(run(rows)["meanPer10g"].toDouble()-sum/rows.size())>.05);}
    void constantRatioCannotIdentifySeparateInsulinEffect(){auto a=sample();for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["units"]=r["carbs"].toDouble()*.12;a[i]=r;}auto r=run(a);QCOMPARE(r["status"].toString(),"unidentifiable");QVERIFY(r["units"].isNull());}
    void positiveInsulinAssociationDoesNotBecomeDose(){auto r=run(sample(80,1.4));QCOMPARE(r["status"].toString(),"implausible_effect");QVERIFY(r["meanPer10g"].isNull());}
    void sparseDataAndRepeatedSameDayAreInsufficient(){QCOMPARE(run(sample(20))["status"].toString(),"insufficient_data");auto a=sample();for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["day"]="2026-01-01";a[i]=r;}QCOMPARE(run(a)["status"].toString(),"insufficient_data");}
    void laterRegimeShiftFailsChronologicalPrediction(){auto a=sample();for(int i=54;i<a.size();++i){auto r=a[i].toObject();r["glucose2h"]=r["glucose2h"].toDouble()+7;r["endpointGlucose"]=r["endpointGlucose"].toDouble()+7;a[i]=r;}auto r=run(a);QVERIFY(r["status"]!="estimated");QVERIFY(r["meanPer10g"].isNull());}
    void noGoalSuccessSelectionOrHypoDeletion(){auto a=sample();for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["goalObserved"]=false;r["lowObserved"]=true;a[i]=r;}auto r=run(a);QCOMPARE(r["status"].toString(),"estimated");QCOMPARE(r["meals"].toInt(),a.size());QCOMPARE(r["observedLowMeals"].toInt(),a.size());}
    void separateCohortsAndExclusions(){auto a=sample();auto first=run(a);for(auto v:sample()){auto r=v.toObject();r["cohort"]="old";a<<r;}QCOMPARE(run(a),first);auto bad=a[0].toObject();bad["modelEligible"]=false;bad["modelExclusions"]=QJsonArray{"later_dose"};a<<bad;auto r=run(a);QCOMPARE(r["meals"],first["meals"]);QCOMPARE(r["exclusions"].toObject()["later_dose"].toInt(),1);}
    void totalWindowInsulinUsesLaterCorrections(){auto a=sample();
        for(int i=0;i<a.size();++i){auto r=a[i].toObject();double g=r["startGlucose"].toDouble(),c=r["carbs"].toDouble(),u=r["units"].toDouble(),t=r["premealTrend"].toDouble(),timing=r["bolusOffsetMinutes"].toDouble();
            const double w=u+1.0+double(i%3);
            r["windowUnits"]=w;r["windowUnitsPer10g"]=10*w/c;r["windowEligible"]=true;r["windowExclusions"]=QJsonArray();r["windowBolusOffsetMinutes"]=timing;
            r["glucose2h"]=1+.8*g+.168*c-1.4*w+.5*t+.025*timing;r["endpointGlucose"]=r["glucose2h"];a[i]=r;}
        auto w=theoreticalMealDose(a,0,"one",5.0,true);
        QCOMPARE(w["status"].toString(),"estimated");QCOMPARE(w["doseBasis"].toString(),"window-total");
        QVERIFY(std::abs(w["endpointEffectPerUnit"].toDouble()-1.4)<.03);
        QCOMPARE(theoreticalMealDose(a,0,"one",5.0,false)["doseBasis"].toString(),"meal-bolus");}
    void windowBasisUsesMealsExcludedForLaterDose(){auto a=sample();
        for(int i=0;i<a.size();++i){auto r=a[i].toObject();double g=r["startGlucose"].toDouble(),c=r["carbs"].toDouble(),u=r["units"].toDouble(),t=r["premealTrend"].toDouble(),timing=r["bolusOffsetMinutes"].toDouble();
            const double w=u+2;r["windowUnits"]=w;r["windowEligible"]=true;r["windowExclusions"]=QJsonArray();r["windowBolusOffsetMinutes"]=timing;
            r["glucose2h"]=1+.8*g+.168*c-1.4*w+.5*t+.025*timing;r["endpointGlucose"]=r["glucose2h"];
            if(i==0){r["modelEligible"]=false;r["modelExclusions"]=QJsonArray{"later_dose"};}a[i]=r;}
        QCOMPARE(theoreticalMealDose(a,0,"one",5.0,true)["meals"].toInt(),a.size());
        QCOMPARE(theoreticalMealDose(a,0,"one",5.0,false)["meals"].toInt(),a.size()-1);}
    void correctionDosesAreASeparateRegressor(){auto a=sample();
        for(int i=0;i<a.size();++i){auto r=a[i].toObject();double g=r["startGlucose"].toDouble(),c=r["carbs"].toDouble(),u=r["units"].toDouble(),t=r["premealTrend"].toDouble(),timing=r["bolusOffsetMinutes"].toDouble();
            const double k=double(i%4);
            r["windowCorrectionUnits"]=k;
            r["glucose2h"]=1+.8*g+.168*c-1.4*u-1.0*k+.5*t+.025*timing;r["endpointGlucose"]=r["glucose2h"];a[i]=r;}
        auto r=theoreticalMealDose(a,0,"one",5.0);
        QCOMPARE(r["status"].toString(),"estimated");
        QVERIFY(std::abs(r["endpointEffectPerUnit"].toDouble()-1.4)<.05);
        QVERIFY(r["correctionEffectPerUnit"].isDouble());
        QVERIFY(std::abs(r["correctionEffectPerUnit"].toDouble()-1.0)<.05);}
    void sugarLoadCombinesCarbsAndStartingGlucose(){auto a=sample();
        // One pool: S = carbs + 5*(start-5). The needed response is 1 U per 10 g; a low
        // start becomes a deficit and a high start an extra load.
        for(int i=0;i<a.size();++i){auto r=a[i].toObject();double g=r["startGlucose"].toDouble(),c=r["carbs"].toDouble();
            const double S=c+5.0*(g-5.0),u=S/10.0+double(i%3-1)*0.5;
            r["units"]=u;r["glucose2h"]=g+.14*S-1.4*u;r["endpointGlucose"]=r["glucose2h"];a[i]=r;}
        auto r=theoreticalMealDose(a,0,"one",5.0,false,24,12,true);
        QCOMPARE(r["status"].toString(),"estimated");
        QCOMPARE(r["metric"].toString(),"U/10g-sugar-load");
        QVERIFY(std::abs(r["meanPer10g"].toDouble()-1.0)<.06);
        QVERIFY(std::abs(r["correctionPerMmol"].toDouble()-0.5)<.03);
        QVERIFY(std::abs(r["units"].toDouble()-r["meanPer10g"].toDouble()*r["referenceCarbs"].toDouble()/10)<.05);}
    void returnEndpointIsTheFittedOutcome(){auto a=sample();
        for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["endpointGlucose"]=r["glucose2h"].toDouble()+1.5;a[i]=r;}
        auto r=theoreticalMealDose(a,0,"one",5.0);
        QCOMPARE(r["status"].toString(),"estimated");QCOMPARE(r["endpoint"].toString(),"return-to-start");
        QVERIFY(std::abs(r["endpointEffectPerUnit"].toDouble()-1.4)<.05);}
    void pooledScopeCombinesGroupsAndLowersMinimum(){auto a=sample();
        for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["group"]=i%4;a[i]=r;}
        QCOMPARE(theoreticalMealDose(a,0,"one",5.0)["status"].toString(),"insufficient_data");
        auto pooled=theoreticalMealDose(a,-1,"one",5.0,false,12,8);
        QCOMPARE(pooled["status"].toString(),"estimated");QCOMPARE(pooled["scope"].toString(),"all-day");
        QCOMPARE(pooled["minimumMeals"].toInt(),12);QCOMPARE(pooled["minimumDays"].toInt(),8);}
    void noDoseOutsideObservedRange(){auto r=theoreticalMealDose(sample(),0,"one",20);QVERIFY(r["status"]!="estimated");QVERIFY(r["units"].isNull());}
    void deterministicUnderReordering(){auto a=sample();QJsonArray rev;for(int i=a.size()-1;i>=0;--i)rev<<a[i];QCOMPARE(run(a),run(rev));}
    void constantOptionalCovariatesAreAllowed(){auto a=sample();for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["startGlucose"]=7;r["premealTrend"]=0;r["bolusOffsetMinutes"]=0;r["glucose2h"]=1+7+.14*r["carbs"].toDouble()-1.4*r["units"].toDouble();r["endpointGlucose"]=r["glucose2h"];a[i]=r;}auto r=run(a);QCOMPARE(r["status"].toString(),"estimated");QVERIFY(std::abs(r["endpointEffectPerUnit"].toDouble()-1.4)<1e-9);}
    void targetIsExplicitAndSolvesTheSameEquation(){auto a=sample();auto first=run(a),second=theoreticalMealDose(a,0,"one",6.0);QCOMPARE(second["status"].toString(),"estimated");
        QCOMPARE(first["targetValue"].toDouble(),5.0);QCOMPARE(second["targetValue"].toDouble(),6.0);
        QVERIFY(std::abs((first["units"].toDouble()-second["units"].toDouble())-(1.0/first["endpointEffectPerUnit"].toDouble()-first["correctionPerMmol"].toDouble()))<1e-9);}
    void unloggedConfoundingIsAnExplicitLimitation(){auto a=sample();for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["glucose2h"]=r["glucose2h"].toDouble()+.3*r["units"].toDouble();r["endpointGlucose"]=r["endpointGlucose"].toDouble()+.3*r["units"].toDouble();a[i]=r;}auto r=run(a);QCOMPARE(r["status"].toString(),"estimated");QVERIFY(std::abs(r["endpointEffectPerUnit"].toDouble()-1.1)<.03);QVERIFY(!r["causalEffectIdentified"].toBool());}
    void invalidInputsNeverYieldZeroDose(){auto a=sample();for(int i=0;i<a.size();++i){auto r=a[i].toObject();r["glucose2h"]=QJsonValue();r["endpointGlucose"]=QJsonValue();a[i]=r;}QVERIFY(run(a)["units"].isNull());QCOMPARE(theoreticalMealDose(sample(),0,"one",0.0)["status"].toString(),"invalid_target");}
    void twoHourEndpointAndFullFollowupAreSeparate(){AutomaticFixture::Data d;auto r=d.run();auto m=r["meals"].toArray()[0].toObject();QCOMPARE(m["glucose2h"].toDouble(),7.0);QVERIFY(m["modelEligible"].toBool());
        // A low after 2 h remains visible and does not select a falsely safe subset.
        for(auto &p:d.readings)if(p.time==AutomaticFixture::start()+5*3600000)p.mmol=3;
        m=d.run()["meals"].toArray()[0].toObject();QVERIFY(m["lowObserved"].toBool());QCOMPARE(m["glucose2h"].toDouble(),7.0);QVERIFY(m["modelEligible"].toBool());}
    void missingReturnEndpointPreventsFit(){AutomaticFixture::Data d;d.readings.removeIf([](const auto&r){return r.time>=AutomaticFixture::start()+330*60000&&r.time<=AutomaticFixture::start()+360*60000;});auto m=d.run()["meals"].toArray()[0].toObject();QVERIFY(!m["modelEligible"].toBool());QVERIFY(m["modelExclusions"].toArray().contains("missing_endpoint"));}
    void assumedInsulinDurationDoesNotBecomeKnownExposure(){AutomaticFixture::Data d;auto ps=AutomaticFixture::profiles();auto p=ps[0].toObject();p.remove("actionModel");ps[0]=p;auto m=d.run(AutomaticFixture::settings(),ps)["meals"].toArray()[0].toObject();QVERIFY(!m["modelEligible"].toBool());QVERIFY(m["modelExclusions"].toArray().contains("window_assumed"));}
};
QTEST_GUILESS_MAIN(TargetModelTest)
#include "test_targetmodel.moc"
