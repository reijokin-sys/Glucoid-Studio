// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysis.h"
#include <QDateTime>
namespace Studio {
QJsonObject insulinSummary(const QJsonArray&amounts,QDate first,int days,const QTimeZone&zone,const QSet<QString>&complete){
    QJsonArray daily;double total=0;int count=0;for(int d=0;d<days;++d){const auto date=first.addDays(d).toString(Qt::ISODate);double rapid=0,basal=0,carbs=0;bool unknown=false;int events=0;
        for(auto v:amounts){auto a=v.toObject();if(QDateTime::fromMSecsSinceEpoch(qint64(a["time"].toDouble()),zone).date().toString(Qt::ISODate)!=date)continue;++events;
            const auto k=a["kind"].toString();if(a["deleted"].toBool()||a["analysisExcluded"].toBool()||k=="unknown")unknown=true;else if(k=="rapid")rapid+=a["value"].toDouble();else if(k=="basal")basal+=a["value"].toDouble();else if(k=="carbs")carbs+=a["value"].toDouble();}
        const bool eligible=complete.contains(date)&&!unknown;if(eligible){total+=rapid+basal;++count;}
        daily<<QJsonObject{{"date",date},{"rapid",rapid},{"basal",basal},{"carbs",carbs},{"events",events},{"complete",eligible},{"unknown",unknown},{"total",rapid+basal}};}
    QJsonObject out{{"daily",daily},{"completeDays",count},{"days",days},{"status",count?"observed":"incomplete"}};if(count)out["meanTotal"]=total/count;return out;
}
}
