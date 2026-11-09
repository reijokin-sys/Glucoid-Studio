// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "domain.h"
#include <QDateTime>
using namespace Studio;
inline constexpr qint64 now=1800000000000LL;
inline QJsonObject testProfile(){
    auto p=emptyProfile();p["approved"]=true;p["version"]=1;p["reviewer"]="Synthetic test reviewer";p["rapidMedicine"]="Synthetic rapid";p["basalMedicine"]="Synthetic basal";p["basalTime"]="22:00";
    for(const auto&v:QList<QPair<QString,double>>{{"targetLow",4},{"targetHigh",10},{"target",6},{"hypo",4},{"maxGlucoseAgeMinutes",5},{"maxBolus",15},{"doseIncrement",0.5},{"basalDose",16},{"basalStableDays",3}})p[v.first]=v.second;
    p["effectiveFrom"]=double(now-30LL*86400000);
    p["slots"]=QJsonArray{QJsonObject{{"name","all"},{"startMinute",0},{"icr",10},{"isf",2}}};
    p["actionModel"]=QJsonObject{{"name","Synthetic linear fixture, NOT a clinical model"},{"reference","Unit test only"},{"medicine","Synthetic rapid"},{"durationMinutes",240},{"curve",QJsonArray{QJsonObject{{"minute",0},{"fraction",1}},QJsonObject{{"minute",120},{"fraction",0.5}},QJsonObject{{"minute",240},{"fraction",0}}}}};
    QJsonObject r{{"minimumEpisodes",3},{"minimumDays",3},{"minCoverage",0.8},{"maxGapMinutes",10},{"consensus",0.75},{"stepPercent",5},{"maxChangePercent",10},{"minReviewDays",3},{"mealWindowMinutes",240},{"correctionWindowMinutes",240},{"basalWindowMinutes",360},{"mealDeviation",2},{"correctionDeviation",2},{"basalDeviation",2},{"minimumIcr",2},{"maximumIcr",50},{"minimumIsf",0.2},{"maximumIsf",10},{"minimumBasal",1},{"maximumBasal",50}};
    p["rules"]=r;return p;
}
