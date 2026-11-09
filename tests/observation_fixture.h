// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "profile_fixture.h"
#include <QTimeZone>
inline QJsonObject observationFixture(){
    auto p=testProfile();p["historical"]=true;p["historicalConfirmed"]=true;p["approved"]=false;p["approvedAt"]="";p["reviewer"]="";
    p["effectiveFromText"]="2026-10-01 00:00";p["effectiveFrom"]=double(QDateTime(QDate(2026,10,1),QTime(0,0),QTimeZone("Europe/Helsinki")).toMSecsSinceEpoch());
    for(auto key:{"target","hypo","maxGlucoseAgeMinutes","maxBolus","doseIncrement","basalStableDays"})p[key]=QJsonValue();
    auto periods=p["slots"].toArray();auto slot=periods[0].toObject();slot["isf"]=QJsonValue();periods[0]=slot;p["slots"]=periods;
    auto action=p["actionModel"].toObject();action["curve"]=QJsonArray();action["name"]="";action["reference"]="";p["actionModel"]=action;
    auto rules=p["rules"].toObject();for(const auto&key:rules.keys())rules[key]=QJsonValue();p["rules"]=rules;
    p["correctionTargetRange"]=QJsonObject{{"low",5.5},{"high",7.5}};return p;
}
