// SPDX-License-Identifier: GPL-3.0-or-later
#include "dailyanalysis.h"
#include "store.h"
#include <QDateTime>
#include <QMap>
#include <algorithm>
#include <array>
#include <cmath>

namespace Studio {
namespace {
constexpr std::array<int,7> starts{360,540,720,900,1080,1260,0};
constexpr std::array<int,7> ends{540,720,900,1080,1260,1440,360};
int bucket(QTime time){const int m=time.hour()*60+time.minute();return m<360?6:(m-360)/180;}
struct Amounts {double carbs=0,units=0;bool hasCarbs=false,hasInsulin=false;};
struct Group {
    double glucoseSum=0,ratioSum=0,carbs=0,units=0,covered=0,expected=0,history=0;
    int glucoseDays=0,ratioDays=0,onlyCarbs=0,onlyInsulin=0;
    QJsonArray days;
};
QString clock(int minutes){return QString("%1:%2").arg(minutes/60,2,10,QChar('0')).arg(minutes%60,2,10,QChar('0'));}
}
QJsonObject dailyPeriodAnalysis(QList<Reading> stream,QList<Reading> history,
    const QJsonArray &amounts,const QTimeZone &zone,QDate last){
    if(!zone.isValid()||!last.isValid())return {{"status","error"},{"message","Virheellinen aikavyöhyke tai päivä."}};
    const qint64 stop=QDateTime(last.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch();
    QDate first;
    auto include=[&](qint64 t){if(t<0||t>=stop)return;const auto d=QDateTime::fromMSecsSinceEpoch(t,zone).date();if(!first.isValid()||d<first)first=d;};
    auto prepare=[&](QList<Reading>&rows){
        rows.removeIf([&](const Reading&r){return !r.reliable||!std::isfinite(r.mmol)||r.mmol<=0||r.time<0||r.time>=stop;});
        std::sort(rows.begin(),rows.end(),[](const Reading&a,const Reading&b){return a.time==b.time?a.key<b.key:a.time<b.time;});
        if(!rows.isEmpty())include(rows.first().time);
    };
    prepare(stream);prepare(history);
    QMap<QDate,std::array<Amounts,7>> entries;int unknown=0;
    for(auto v:amounts){
        const auto a=v.toObject();const double value=a["value"].toDouble(NAN),time=a["time"].toDouble(NAN);
        if(a["deleted"].toBool()||a["analysisExcluded"].toBool()||(a.contains("active")&&!a["active"].toVariant().toBool())
            ||!std::isfinite(value)||value<0||!std::isfinite(time)||time<0||time>=stop)continue;
        const auto kind=a["kind"].toString();
        if(kind!="carbs"&&kind!="rapid"&&kind!="correction"){if(kind=="unknown"||kind.isEmpty())++unknown;continue;}
        const auto dt=QDateTime::fromMSecsSinceEpoch(qint64(time),zone);include(qint64(time));
        auto &row=entries[dt.date()][bucket(dt.time())];
        if(kind=="carbs"){row.carbs+=value;row.hasCarbs=true;}
        else {row.units+=value;row.hasInsulin=true;}
    }
    std::array<Group,7> groups;
    auto window=[](const QList<Reading>&rows,qint64 a,qint64 b){
        QList<Reading> out;
        auto it=std::lower_bound(rows.cbegin(),rows.cend(),a-1200000,[](const Reading&r,qint64 t){return r.time<t;});
        for(;it!=rows.cend()&&it->time<b;++it)out<<*it;
        return out;
    };
    for(auto day=first;day.isValid()&&day<=last;day=day.addDays(1)){
        const auto begin=QDateTime(day,QTime(0,0),zone).toMSecsSinceEpoch();
        const auto end=QDateTime(day.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch();
        const auto intervals=selectSeries(window(stream,begin,end),window(history,begin,end),begin,end,true,1200);
        const auto logged=entries.value(day);
        for(int i=0;i<7;++i){
            const auto a=QDateTime(day,QTime(starts[i]/60,starts[i]%60),zone).toMSecsSinceEpoch();
            const auto b=ends[i]==1440?end:QDateTime(day,QTime(ends[i]/60,ends[i]%60),zone).toMSecsSinceEpoch();
            double duration=0,sum=0,secondary=0;
            for(auto it=std::lower_bound(intervals.cbegin(),intervals.cend(),a,[](const Interval&v,qint64 t){return v.to<=t;});it!=intervals.cend()&&it->from<b;++it){
                const auto d=std::min(b,it->to)-std::max(a,it->from);if(d<=0)continue;
                duration+=d;sum+=d*it->value;if(it->kind=="history")secondary+=d;
            }
            auto &g=groups[i];const auto &e=logged[i];
            g.covered+=duration/1000;g.expected+=(b-a)/1000.0;g.history+=secondary/1000;
            g.carbs+=e.carbs;g.units+=e.units;
            QJsonObject detail{{"date",day.toString(Qt::ISODate)},{"carbs",e.carbs},{"units",e.units},
                {"hasCarbs",e.hasCarbs},{"hasInsulin",e.hasInsulin},{"coverage",duration/double(b-a)}};
            if(duration>0){g.glucoseSum+=sum/duration;++g.glucoseDays;detail["glucoseMean"]=sum/duration;}
            if(e.carbs>0&&e.hasInsulin){const double ratio=10*e.units/e.carbs;g.ratioSum+=ratio;++g.ratioDays;detail["ratioPer10g"]=ratio;}
            if(e.hasCarbs&&!e.hasInsulin)++g.onlyCarbs;
            if(e.hasInsulin&&e.carbs<=0)++g.onlyInsulin;
            if(duration>0||e.hasCarbs||e.hasInsulin)g.days<<detail;
        }
    }
    QJsonArray rows;
    for(int i=0;i<7;++i){const auto &g=groups[i];
        QJsonObject row{{"label",clock(starts[i])+QString::fromUtf8("–")+(ends[i]==1440?QString("00:00"):clock(ends[i]))},
            {"startMinute",starts[i]},{"endMinute",ends[i]},{"glucoseDays",g.glucoseDays},{"ratioDays",g.ratioDays},
            {"carbs",g.carbs},{"units",g.units},{"onlyCarbDays",g.onlyCarbs},{"onlyInsulinDays",g.onlyInsulin},
            {"coverage",g.expected>0?g.covered/g.expected:0},{"historySeconds",g.history},
            {"historyFraction",g.covered>0?g.history/g.covered:0},{"days",g.days}};
        if(g.glucoseDays)row["glucoseMean"]=g.glucoseSum/g.glucoseDays;
        if(g.ratioDays)row["ratioPer10g"]=g.ratioSum/g.ratioDays;
        rows<<row;
    }
    return {{"status","ready"},{"method","daily-clock-means-v1"},{"firstDate",first.toString(Qt::ISODate)},
        {"lastDate",last.toString(Qt::ISODate)},{"timezone",QString::fromUtf8(zone.id())},
        {"groups",rows},{"unknownEntries",unknown},{"calendarDays",first.isValid()?first.daysTo(last)+1:0}};
}
QJsonObject dailyPeriodSnapshot(const QString &path,const QTimeZone &zone,qint64 now){
    Store db(path,true);
    if(!db.good()||!db.run("BEGIN"))return {{"status","error"},{"message","Aineiston lukeminen ei onnistunut."}};
    const auto revision=db.revision();const auto last=QDateTime::fromMSecsSinceEpoch(now,zone).date().addDays(-1);
    const auto stop=QDateTime(last.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch();
    const auto stream=db.readings(0,stop),history=db.readings(0,stop,"history");const auto amounts=db.amounts(0,stop);
    if(!db.good()||!db.run("COMMIT"))return {{"status","error"},{"message","Aineiston lukeminen ei onnistunut."}};
    auto result=dailyPeriodAnalysis(stream,history,amounts,zone,last);result["sourceRevision"]=revision;return result;
}
}
