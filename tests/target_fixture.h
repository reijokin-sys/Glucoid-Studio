// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "targetmodel.h"
#include <QDate>
#include <random>
namespace TargetFixture {
using namespace Studio;
QJsonArray sample(int count=80,double insulinEffect=-1.4) {
    QJsonArray out;std::mt19937 rng(33);std::uniform_real_distribution<double> unit(0,1);
    for(int i=0;i<count;++i){double c=30+40*unit(rng),u=c*.12+(unit(rng)-.5)*4,g=5+5*unit(rng),trend=unit(rng)-.5,timing=-20+20*unit(rng);
        double y=1+.8*g+.14*c+insulinEffect*u+.5*trend+.025*timing+(unit(rng)-.5)*.2;
        out<<QJsonObject{{"group",0},{"cohort","one"},{"modelEligible",true},{"day",QDate(2026,1,1).addDays(i/2).toString(Qt::ISODate)},
            {"carbs",c},{"units",u},{"startGlucose",g},{"premealTrend",trend},{"bolusOffsetMinutes",timing},{"glucose2h",y},{"endpointGlucose",y},{"windowCorrectionUnits",0},{"lowObserved",y<3.9}};
    }return out;
}
QJsonObject run(const QJsonArray &rows){return theoreticalMealDose(rows,0,"one",theoreticalTargetMmol);}
}
