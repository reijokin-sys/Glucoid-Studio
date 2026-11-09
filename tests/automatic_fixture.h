// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "automatic.h"
#include <QDateTime>
namespace AutomaticFixture {
using namespace Studio;
inline qint64 start(){return QDateTime(QDate(2026,10,1),QTime(0,0),QTimeZone("Europe/Helsinki")).toMSecsSinceEpoch();}
inline QJsonObject settings(){return {{"timezone","Europe/Helsinki"},{"targetLow",6},{"targetHigh",8},{"hypo",4},{"groups",QJsonArray{QJsonObject{{"name","Night"},{"startMinute",0}},QJsonObject{{"name","Morning"},{"startMinute",360}},QJsonObject{{"name","Day"},{"startMinute",720}},QJsonObject{{"name","Evening"},{"startMinute",1080}}}}};}
inline QJsonArray profiles(){return {QJsonObject{{"version",1},{"observationActive",true},{"approved",false},{"effectiveFrom",double(start()-90LL*86400000)},{"rapidMedicine","Synthetic rapid"},{"basalMedicine","Synthetic basal"},{"basalDose",12},{"actionModel",QJsonObject{{"durationMinutes",240}}}}};}
inline QJsonObject amountEvent(QString id,qint64 time,QString kind,double value,QString medicine={}){return {{"id",id},{"time",double(time)},{"kind",kind},{"value",value},{"medicine",medicine},{"local",false},{"label",kind}};}
inline QJsonObject reportedSettings(){auto s=settings();s["reportedBasalHistory"]=QJsonArray{QJsonObject{{"id","synthetic-report"},{"source","patient_report"},{"daily",true},{"units",12},{"time","22:00"},{"timeApproximate",true},{"timezone","Europe/Helsinki"},{"effectiveFrom",double(start()-90LL*86400000)},{"reportedThrough",double(start()+7LL*86400000)},{"medicine","Synthetic basal"}}};return s;}
struct Data {
    QList<Reading> readings;QJsonArray amounts;
    Data(){
        for(auto t=start()-8*3600000;t<=start()+7LL*86400000;t+=300000){Reading r;r.time=t;r.key=QString::number(t);r.mmol=7;r.original=7;r.sensor="synthetic";r.originalUnit="mmol/L";readings<<r;}
        for(int day=-4;day<7;++day)amounts<<amountEvent(QString("b%1").arg(day),start()+day*86400000LL+22*3600000,"basal",12,"Synthetic basal");
        for(int day=0;day<6;++day)for(int group=0;group<4;++group){auto at=start()+day*86400000LL+(2+6*group)*3600000;amounts<<amountEvent(QString("c%1-%2").arg(day).arg(group),at,"carbs",40);amounts<<amountEvent(QString("u%1-%2").arg(day).arg(group),at,"rapid",2+group,"Synthetic rapid");}
    }
    QJsonObject run(QJsonObject config=settings(),QJsonArray ps=profiles(),qint64 until=start()+7LL*86400000)const{return automaticMealAnalysis(readings,{},amounts,ps,config,start(),start()+6LL*86400000,until);}
    QJsonObject meal(QString id)const{for(auto v:run()["meals"].toArray())if(v.toObject()["id"]==id)return v.toObject();return {};}
};
inline bool has(const QJsonObject&row,const QString&code){for(auto v:row["issues"].toArray())if(v.toObject()["code"]==code)return true;return false;}
}
