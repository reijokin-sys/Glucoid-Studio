// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "domain.h"
#include <QDateTime>
#include <QTimeZone>
namespace UiFixture {
using namespace Studio;
inline qint64 start(){return QDateTime(QDate(2026,10,1),QTime(0,0),QTimeZone("Europe/Helsinki")).toMSecsSinceEpoch();}
inline QJsonObject amountEvent(QString id,qint64 time,QString kind,double value,QString medicine={}){return {{"id",id},{"time",double(time)},{"kind",kind},{"value",value},{"medicine",medicine},{"local",false},{"label",kind}};}
struct Data {
    QList<Reading> readings;QJsonArray amounts;
    Data(){
        for(auto t=start()-8*3600000;t<=start()+7LL*86400000;t+=300000){Reading r;r.time=t;r.key=QString::number(t);r.mmol=7;r.original=7;r.sensor="synthetic";r.originalUnit="mmol/L";readings<<r;}
        for(int day=-4;day<7;++day)amounts<<amountEvent(QString("b%1").arg(day),start()+day*86400000LL+22*3600000,"basal",12,"Synthetic basal");
        for(int day=0;day<6;++day)for(int group=0;group<4;++group){auto at=start()+day*86400000LL+(2+6*group)*3600000;amounts<<amountEvent(QString("c%1-%2").arg(day).arg(group),at,"carbs",40);amounts<<amountEvent(QString("u%1-%2").arg(day).arg(group),at,"rapid",2+group,"Synthetic rapid");}
    }
};
}
