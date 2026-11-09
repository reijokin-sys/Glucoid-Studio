// SPDX-License-Identifier: GPL-3.0-or-later
#include "glucoseanalysis.h"
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
namespace Studio {
static double percentile(QList<double> a,double q){if(a.isEmpty())return std::numeric_limits<double>::quiet_NaN();std::sort(a.begin(),a.end());double x=(a.size()-1)*q;int i=int(x);return a[i]+(a[std::min(i+1,int(a.size()-1))]-a[i])*(x-i);}
QList<Interval> intervals(QList<Reading> rows,qint64 from,qint64 to,int maxGap){
    std::sort(rows.begin(),rows.end(),[](const auto&a,const auto&b){return a.time==b.time?a.key<b.key:a.time<b.time;});
    QMap<QString,QList<double>> deltas;QMap<QString,qint64> previous;
    for(const auto&r:rows){const auto key=r.seriesKind+":"+r.sensor;if(previous.contains(key)){auto d=r.time-previous[key];if(d>0&&d<=maxGap*1000)deltas[key]<<double(d);}previous[key]=r.time;}
    QMap<QString,qint64> cadences;for(auto it=deltas.cbegin();it!=deltas.cend();++it)cadences[it.key()]=qint64(percentile(it.value(),0.5));
    QList<Interval> out; qint64 covered=from;
    for(qsizetype i=0;i<rows.size();++i){const auto&r=rows[i];if(!r.reliable||!std::isfinite(r.mmol)||r.mmol<=0)continue;
        const auto key=r.seriesKind+":"+r.sensor;const qint64 cadence=cadences.value(key,r.seriesKind=="history"?300000:60000);
        const qint64 next=i+1<rows.size()?rows[i+1].time:to;
        const qint64 a=std::max({from,r.time,covered}),b=std::min({to,next,r.time+std::min(cadence,qint64(maxGap)*1000)});
        if(b>a){out<<Interval{a,b,r.mmol,r.seriesKind,r.sensor};covered=b;}
    }return out;
}
QList<Interval> selectSeries(const QList<Reading>&stream,const QList<Reading>&history,qint64 from,qint64 to,bool fill,int gap){
    auto out=intervals(stream,from,to,gap);if(!fill)return out;
    const auto primary=out;auto secondary=intervals(history,from,to,gap);qsizetype pos=0;
    for(auto h:secondary){while(pos<primary.size()&&primary[pos].to<=h.from)++pos;auto p=pos;qint64 at=h.from;
        while(p<primary.size()&&primary[p].from<h.to){if(primary[p].from>at)out<<Interval{at,std::min(h.to,primary[p].from),h.value,"history",h.sensor};at=std::max(at,primary[p].to);if(at>=h.to)break;++p;}
        if(at<h.to)out<<Interval{at,h.to,h.value,"history",h.sensor};}
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.from<b.from;});return out;
}
QJsonObject analyse(const QList<Interval>&data,QDate first,int days,const QTimeZone&zone,double low,double high){
    const auto from=QDateTime(first,QTime(0,0),zone).toMSecsSinceEpoch(),to=QDateTime(first.addDays(days),QTime(0,0),zone).toMSecsSinceEpoch();
    struct Bin{double sum=0,ms=0;};QMap<QString,QMap<int,Bin>> daily;QList<double> expected(96,0);double duration=0,sum=0,sq=0,below=0,above=0,historyMs=0;QList<QPair<double,double>> weighted;double nightSum=0,nightDuration=0;
    for(qint64 t=from;t<to;){const auto end=std::min(to,t+60000);auto dt=QDateTime::fromMSecsSinceEpoch(t,zone);expected[(dt.time().hour()*60+dt.time().minute())/15]+=end-t;t=end;}
    for(auto v:data){const qint64 a=std::max(from,v.from),b=std::min(to,v.to);if(b<=a)continue;const double d=b-a;duration+=d;sum+=v.value*d;sq+=v.value*v.value*d;weighted<<qMakePair(v.value,d);if(v.kind=="history")historyMs+=d;if(v.value<low)below+=d;if(v.value>high)above+=d;
        for(qint64 t=a;t<b;){const auto end=std::min(b,(t/60000+1)*60000);auto dt=QDateTime::fromMSecsSinceEpoch(t,zone);auto&bin=daily[dt.date().toString(Qt::ISODate)][(dt.time().hour()*60+dt.time().minute())/15];bin.sum+=v.value*(end-t);bin.ms+=end-t;
            if(dt.time().hour()<6){nightSum+=v.value*(end-t);nightDuration+=end-t;}
            t=end;}}
    const bool targets=std::isfinite(low)&&std::isfinite(high)&&low>0&&low<high;
    QJsonObject out{{"firstDate",first.toString(Qt::ISODate)},{"lastDate",first.addDays(days-1).toString(Qt::ISODate)},{"from",double(from)},{"to",double(to)},{"days",days},{"coverage",to>from?duration/(to-from):0},{"coveredSeconds",duration/1000},{"targetsSet",targets},{"historySeconds",historyMs/1000},{"observedDays",daily.size()}};
    // The same selected intervals and local calendar days as the daytime metrics.
    // Expected duration includes DST changes; missing time never becomes zero glucose.
    double expectedNight=0;for(int i=0;i<24;++i)expectedNight+=expected[i];
    out["nightCoveredSeconds"]=nightDuration/1000;
    out["nightCoverage"]=expectedNight>0?nightDuration/expectedNight:0;
    if(nightDuration>0)out["nightMean"]=nightSum/nightDuration;
    if(duration>0){const double mean=sum/duration,sd=std::sqrt(std::max(0.0,sq/duration-mean*mean));out["mean"]=mean;out["sd"]=sd;out["cv"]=sd/mean*100;
        std::sort(weighted.begin(),weighted.end());double acc=0;for(qsizetype i=0;i<weighted.size();++i){acc+=weighted[i].second;if(acc>=duration/2){out["median"]=acc==duration/2&&i+1<weighted.size()?(weighted[i].first+weighted[i+1].first)/2:weighted[i].first;break;}}
        if(targets){out["below"]=below/duration;out["above"]=above/duration;out["inRange"]=(duration-below-above)/duration;}}
    QJsonArray bins,daysOut;for(int i=0;i<96;++i){QList<double> values;double ms=0;for(const auto&day:daily){auto v=day.value(i);if(v.ms>0){values<<v.sum/v.ms;ms+=v.ms;}}
        QJsonObject b{{"minute",i*15+7.5},{"days",values.size()},{"coverage",expected[i]>0?ms/expected[i]:0}};
        if(!values.isEmpty()){double s=0;for(double v:values)s+=v;b["mean"]=s/values.size();b["median"]=percentile(values,.5);b["p05"]=percentile(values,.05);b["p25"]=percentile(values,.25);b["p75"]=percentile(values,.75);b["p95"]=percentile(values,.95);}bins<<b;}
    for(int d=0;d<days;++d){auto date=first.addDays(d);double ms=0;for(auto b:daily.value(date.toString(Qt::ISODate)))ms+=b.ms;double length=QDateTime(date.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch()-QDateTime(date,QTime(0,0),zone).toMSecsSinceEpoch();daysOut<<QJsonObject{{"date",date.toString(Qt::ISODate)},{"coverage",ms/length},{"hours",length/3600000}};}
    out["bins"]=bins;out["daily"]=daysOut;out["low"]=targets?QJsonValue(low):QJsonValue();out["high"]=targets?QJsonValue(high):QJsonValue();return out;
}
QJsonObject summaryAnalysis(const QList<Reading>&stream,const QList<Reading>&history,
    QDate first,int days,const QTimeZone&zone,double low,double high){
    const auto from=QDateTime(first,QTime(0,0),zone).toMSecsSinceEpoch();
    const auto to=QDateTime(first.addDays(days),QTime(0,0),zone).toMSecsSinceEpoch();
    auto window=[&](const QList<Reading>&rows){
        QList<Reading> out;
        for(const auto &row:rows)if(row.time>=from-summaryPreRollMs && row.time<to)out<<row;
        return out;
    };
    auto out=analyse(selectSeries(window(stream),window(history),from,to,true,summaryMaxGapSeconds),first,days,zone,low,high);
    out["fillHistory"]=true;
    const auto covered=out["coveredSeconds"].toDouble();
    out["historyFraction"]=covered>0?out["historySeconds"].toDouble()/covered:0;
    return out;
}
QJsonObject currentGlucose(QList<Reading> stream,QList<Reading> history,qint64 now,
    const QTimeZone&zone,double low,double high,double criticalLow){
    QJsonObject out{{"hasData",false},{"value",0.0},{"time",0.0},{"rate",0.0},
        {"rateKnown",false},{"trend","-"},{"ageMinutes",-1},{"status","unknown"},
        {"stale",true},{"seriesKind",""},{"timezone",zone.isValid()?QString::fromUtf8(zone.id()):QString()}};
    if(!zone.isValid())return out;
    auto clean=[&](QList<Reading>&rows){
        rows.removeIf([&](const Reading&r){return !r.reliable || !std::isfinite(r.mmol) || r.mmol<=0
            || r.time<now-30LL*86400000 || r.time>=now+60000;});
        std::sort(rows.begin(),rows.end(),[](const Reading&a,const Reading&b){return a.time==b.time?a.key<b.key:a.time<b.time;});
    };
    clean(stream);clean(history);
    // Use the newest available reading; stream wins at the same timestamp.
    const bool secondary=!history.isEmpty() && (stream.isEmpty() || history.last().time>stream.last().time);
    const auto &points=secondary?history:stream;
    if(points.isEmpty())return out;
    const auto &last=points.last();
    QList<Reading> window;
    for(auto it=points.crbegin();it!=points.crend();++it){
        if(last.time-it->time>900000 || it->sensor!=last.sensor)break;
        window.prepend(*it);
    }
    double rate=0;bool known=false;
    if(window.size()>=2){
        const auto t0=window.first().time;double n=0,sx=0,sy=0,sxx=0,sxy=0;
        for(const auto &r:window){const double x=(r.time-t0)/60000.0;n+=1;sx+=x;sy+=r.mmol;sxx+=x*x;sxy+=x*r.mmol;}
        const auto denominator=n*sxx-sx*sx;
        if(denominator!=0){rate=(n*sxy-sx*sy)/denominator;known=std::isfinite(rate);}
    }
    const int age=int(std::max<qint64>(0,(now-last.time)/60000));
    const bool stale=now-last.time>15LL*60000;
    const QString trend=known?(std::abs(rate)<0.11?QString::fromUtf8("→"):
        std::abs(rate)<=0.22?(rate>0?QString::fromUtf8("↗"):QString::fromUtf8("↘")):
        (rate>0?QString::fromUtf8("↑"):QString::fromUtf8("↓"))):QStringLiteral("-");
    const QString status=stale?"stale":criticalLow>0&&last.mmol<=criticalLow?"critical":
        (high>0&&last.mmol>high)||(low>0&&last.mmol<low)?"warning":
        high>0||low>0||criticalLow>0?"ok":"plain";
    out["hasData"]=true;out["value"]=last.mmol;out["time"]=double(last.time);
    out["rate"]=rate;out["rateKnown"]=known;out["trend"]=trend;
    out["ageMinutes"]=age;out["status"]=status;out["stale"]=status=="stale";
    out["seriesKind"]=secondary?"history":"stream";
    return out;
}
double gmiPercent(double meanMmol){
    // GMI(%) = 3.31 + 0.02392 × mean glucose in mg/dL.
    return 3.31+0.02392*(meanMmol*18.0182);
}
double gmiMmolMol(double meanMmol){
    // GMI(mmol/mol) = 12.71 + 4.70587 × mean glucose in mmol/L.
    return 12.71+4.70587*meanMmol;
}
}
