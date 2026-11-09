// SPDX-License-Identifier: GPL-3.0-or-later
#include "response_fixture.h"
#include <QCoreApplication>
#include "store.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <random>
#include <cmath>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);QJsonArray results;const int repeats=48;
 for(const auto&scenario:QStringList{"noisy-stable","missing-half","systematic-carbs-plus20","basal-drift","correction-phase-change","unchanged-prescription","two-day-block"}){
  int eligible=0,intervals=0,covered=0,qualifiedCovered=0,proposals=0,isfEligible=0;double absoluteError=0;const bool unchanged=scenario=="unchanged-prescription";
  for(int trial=0;trial<repeats;trial++){
   std::mt19937 rng(6701+trial);std::normal_distribution<double> noise(0,.8);std::uniform_real_distribution<double> uniform(0,1);auto e=episodes();auto pol=policy();if(scenario=="two-day-block")pol["blockDays"]=2;
   for(int i=0;i<e.size();i++){auto o=e[i].toObject();const auto kind=o["kind"].toString();
    if(kind=="basal"){if(scenario=="basal-drift")o["lastGlucose"]=10;}
    else {if(unchanged&&kind=="icr")o["lastGlucose"]=6;
     if(scenario=="correction-phase-change"&&kind=="isf"&&o["start"].toDouble()>=beginning()+7LL*86400000)o["lastGlucose"]=o["firstGlucose"].toDouble()-(o["firstGlucose"].toDouble()-o["lastGlucose"].toDouble())*.5;
     o["lastGlucose"]=o["lastGlucose"].toDouble()+noise(rng);
     if(scenario=="systematic-carbs-plus20"&&kind=="icr")o["carbs"]=o["carbs"].toDouble()*1.2;
     if(scenario=="missing-half"&&uniform(rng)<.5)o["complete"]=false;
    }e[i]=o;
   }
   auto r=responseEstimate(profile(),pol,e,beginning(),beginning()+14LL*86400000,false);double truths[]={10,12,8,15};
   for(int g=0;g<4;g++){auto row=r["groups"].toArray()[g].toObject();double truth=unchanged?20:truths[g];
    if(row["isfStatus"]=="estimated")isfEligible++;
    if(row["status"]=="estimated"){eligible++;if(row["ratioLow"].toDouble()<=truth&&row["ratioHigh"].toDouble()>=truth)qualifiedCovered++;}
    if(row["ratioLow"].isDouble()&&row["ratioHigh"].isDouble()){intervals++;if(row["ratioLow"].toDouble()<=truth&&row["ratioHigh"].toDouble()>=truth)covered++;absoluteError+=std::abs(row["ratio"].toDouble()/truth-1)*100;}
    if(responseProposal(profile(),pol,r,g)["status"]=="proposal")proposals++;
   }
  }
  results<<QJsonObject{{"scenario",scenario},{"replicates",repeats},{"groupsTotal",repeats*4},{"qualifiedRatios",eligible},{"qualifiedIsf",isfEligible},{"intervalsAvailable",intervals},{"intervalsCoveringTruth",covered},{"qualifiedIntervalsCoveringTruth",qualifiedCovered},{"qualifiedEmpiricalCoverage",eligible?double(qualifiedCovered)/eligible:0},{"empiricalCoverage",intervals?double(covered)/intervals:0},{"meanAbsoluteRatioErrorPercent",intervals?absoluteError/intervals:0},{"proposals",proposals}};
 }
 QJsonObject report{{"syntheticOnly",true},{"clinicalValidation",false},{"seedBase",6701},{"bootstrapPerReplicate",200},{"noiseEndpointMmol",.8},{"note","Finite artificial experiment; interval coverage does not include systematic carbohydrate or physiology errors. Test policy thresholds are not patient defaults."},{"scenarios",results}};
 const auto output=QJsonDocument(report).toJson(QJsonDocument::Indented);if(app.arguments().size()<2||app.arguments().size()>3)return 2;QFile f(app.arguments()[1]);if(!f.open(QIODevice::WriteOnly)||f.write(output)!=output.size())return 3;
 if(app.arguments().size()==3){const auto path=app.arguments()[2];if(QFile::exists(path+"/studio.sqlite3"))return 4;QDir().mkpath(path);Store store(path+"/studio.sqlite3");auto p=profile();auto z=QTimeZone("Europe/Helsinki");const auto end=QDateTime(QDateTime::currentDateTime(z).date(),QTime(0,0),z).toMSecsSinceEpoch();const auto shift=end-(beginning()+14LL*86400000);p["effectiveFrom"]=p["effectiveFrom"].toDouble()+shift;store.saveProfile(p);store.setMeta("responsePolicy",QString::fromUtf8(QJsonDocument(policy()).toJson(QJsonDocument::Compact)));QList<Reading> readings;for(qint64 at=end-30LL*86400000;at<end;at+=5*60000){Reading r;r.time=at;r.key="synthetic-ui:"+QString::number(at);r.sensor="synthetic";r.mmol=7+std::sin(double(at)/3600000);r.original=r.mmol;r.originalUnit="mmol/L";readings<<r;}store.ingest(readings,{},end-30LL*86400000,end,false);store.setMeta("analysisSettings",QString::fromUtf8(QJsonDocument(QJsonObject{{"low",4},{"high",10},{"maxGapMinutes",20},{"timezone","Europe/Helsinki"}}).toJson()));for(auto v:episodes()){auto e=v.toObject();for(auto key:{"start","end","mealTime","doseTime"})if(e.contains(key))e[key]=e[key].toDouble()+shift;e["dependencyFrom"]=e["start"];store.saveEpisode(e);}if(!store.good())return 5;
 }
 return 0;
}
