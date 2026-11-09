// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "response.h"
#include "profile_fixture.h"
#include <QTimeZone>
using namespace Studio;
static qint64 beginning(){return QDateTime(QDate(2026,10,1),QTime(0,0),QTimeZone("Europe/Helsinki")).toMSecsSinceEpoch();}
static QJsonObject profile(){auto p=testProfile();p["historical"]=true;p["effectiveFrom"]=double(beginning()-90LL*86400000);auto a=p["slots"].toArray();auto slot=a[0].toObject();slot["icr"]=20;a[0]=slot;p["slots"]=a;return p;}
static QJsonObject policy(){auto p=defaultResponsePolicy();p["approved"]=true;auto groups=p["groups"].toArray();int starts[]={360,600,840,1080};for(int i=0;i<4;i++){auto g=groups[i].toObject();g["startMinute"]=starts[i];groups[i]=g;}p["groups"]=groups;
 for(auto pair:QList<QPair<QString,double>>{{"minimumEpisodes",2},{"minimumDays",2},{"minimumBasalDays",1},{"minCoverage",.8},{"minimumDose",.1},{"minimumCarbs",5},{"maxRelativeWidth",.8},{"maxPeriodDriftPercent",25},{"maxDayInfluencePercent",25},{"maxInvalidFraction",.4},{"doseStepPercent",5},{"maximumChangePercent",10},{"doseMatchToleranceUnits",.5}})p[pair.first]=pair.second;p["bootstrapSamples"]=200;return p;}
static QJsonArray episodes(){QJsonArray result;auto zone=QTimeZone("Europe/Helsinki");double sensitivities[]={2,1.5,2.5,1.8},ratios[]={10,12,8,15};for(int day=0;day<14;day++){
 auto date=QDate(2026,10,1).addDays(day);auto base=QDateTime(date,QTime(0,0),zone).toMSecsSinceEpoch();QJsonObject basal{{"id",QString("b%1").arg(day)},{"kind","basal"},{"start",double(base)},{"end",double(base+14400000)},{"firstGlucose",6},{"lastGlucose",6},{"methodVersion",2},{"endpointMinutes",15},{"profileVersion",1},{"reviewed",true},{"complete",true},{"contextClear",true},{"endStable",true},{"basalStable",true},{"basalHistoryComplete",true},{"coverage",1}};result<<basal;
 for(int g=0;g<4;g++){auto e=basal;auto at=QDateTime(date,QTime(6+g*4,0),zone).toMSecsSinceEpoch();e["id"]=QString("%1-%2").arg(day).arg(g);e["start"]=double(at);e["doseTime"]=double(at);e["mealTime"]=double(at);e["end"]=double(at+14400000);if(day%2==0){e["kind"]="isf";e["rapid"]=2;e["carbs"]=0;e["firstGlucose"]=12;e["lastGlucose"]=12-2*sensitivities[g];}else{e["kind"]="icr";e["rapid"]=4;e["carbs"]=80;e["firstGlucose"]=6;e["lastGlucose"]=6+sensitivities[g]*(80/ratios[g]-4);}result<<e;}}
 return result;}
static QJsonObject estimate(QJsonArray e=episodes(),bool unsafe=false){return responseEstimate(profile(),policy(),e,beginning(),QDateTime::fromMSecsSinceEpoch(beginning(),QTimeZone("Europe/Helsinki")).addDays(14).toMSecsSinceEpoch(),unsafe);}
