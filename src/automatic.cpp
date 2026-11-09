// SPDX-License-Identifier: GPL-3.0-or-later
#include "automatic.h"
#include "targetmodel.h"
#include "store.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Studio {
namespace {
constexpr qint64 minute=60000;
bool positive(double v){return std::isfinite(v)&&v>0;}
double mean(const QList<double>&v){if(v.isEmpty())return NAN;double sum=0;for(double x:v)sum+=x;return sum/v.size();}
double quantile(QList<double> v,double q){if(v.isEmpty())return NAN;std::sort(v.begin(),v.end());double at=(v.size()-1)*q;int i=int(at);return v[i]+(v[std::min(i+1,int(v.size()-1))]-v[i])*(at-i);}
QString hash(const QJsonObject&o){return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());}
struct Event {QJsonObject row;qint64 time;QString id,kind,medicine;double value;bool blocked;};
struct Meal {qint64 time,last;QList<int> carbs,doses,corrections;bool ambiguous=false;};
QJsonObject profileAt(const QJsonArray&profiles,qint64 time){
    QJsonObject chosen;double last=-1;int version=-1;
    for(auto v:profiles){auto p=v.toObject();if(!(p["observationActive"].toBool()||p["approved"].toBool()||p["responseApproved"].toBool()))continue;
        const double start=number(p,"effectiveFrom");const int next=p["version"].toInt();
        if(start<=time&&(start>last||(start==last&&next>version))){chosen=p;last=start;version=next;}}
    return chosen;
}
QJsonObject treatmentKey(const QJsonObject&p){return {{"rapid",p["rapidMedicine"]},{"basal",p["basalMedicine"]},{"dose",p["basalDose"]},{"duration",p["actionModel"].toObject()["durationMinutes"]}};}
QJsonArray groups(const QJsonObject&settings){
    auto supplied=settings["groups"].toArray();int previous=-1;bool valid=supplied.size()==4;
    for(auto v:supplied){auto g=v.toObject();double start=number(g,"startMinute");valid&=std::isfinite(start)&&std::floor(start)==start&&start>previous&&start<1440&&!g["name"].toString().trimmed().isEmpty();previous=int(std::isfinite(start)?start:-1);}
    if(valid)return supplied;
    return {QJsonObject{{"name","Aamu"},{"startMinute",360}},QJsonObject{{"name","Päivä"},{"startMinute",660}},QJsonObject{{"name","Iltapäivä"},{"startMinute",900}},QJsonObject{{"name","Ilta / yö"},{"startMinute",1140}}};
}
int groupAt(const QJsonArray&groups,qint64 time,const QTimeZone&zone){const auto t=QDateTime::fromMSecsSinceEpoch(time,zone).time();const int at=t.hour()*60+t.minute();int group=3;for(int i=0;i<groups.size();++i){if(groups[i].toObject()["startMinute"].toInt()>at)break;group=i;}return group;}
struct Stats {double coverage=0,average=NAN,median=NAN,minimum=NAN,maximum=NAN,inTarget=NAN,historySeconds=0;int points=0;};
Stats stats(const QList<Interval>&data,qint64 from,qint64 to,double low,double high){
    Stats out;if(to<=from)return out;double ms=0,sum=0,in=0;QList<QPair<double,double>> values;
    auto begin=std::lower_bound(data.cbegin(),data.cend(),from,[](const Interval&v,qint64 t){return v.to<=t;});
    for(auto it=begin;it!=data.cend()&&it->from<to;++it){double d=std::min(to,it->to)-std::max(from,it->from);if(d<=0)continue;
        ms+=d;sum+=it->value*d;values<<qMakePair(it->value,d);++out.points;
        out.minimum=std::isfinite(out.minimum)?std::min(out.minimum,it->value):it->value;out.maximum=std::isfinite(out.maximum)?std::max(out.maximum,it->value):it->value;
        if(it->value>=low&&it->value<=high)in+=d;
        if(it->kind=="history")out.historySeconds+=d/1000;}
    out.coverage=ms/double(to-from);if(ms>0){out.average=sum/ms;out.inTarget=in/ms;std::sort(values.begin(),values.end());double weight=0;for(int i=0;i<values.size();++i){weight+=values[i].second;if(weight>=ms/2){out.median=weight==ms/2&&i+1<values.size()?(values[i].first+values[i+1].first)/2:values[i].first;break;}}}return out;
}
// Reports describe history or an ongoing regimen; they never create injection entries.
bool validBasalReport(const QJsonObject&r,const QJsonObject&settings){
    return r["source"]=="patient_report"&&r["daily"].toBool()&&positive(number(r,"units"))&&r["timezone"]==settings["timezone"]&&QTime::fromString(r["time"].toString(),"HH:mm").isValid()&&positive(number(r,"effectiveFrom"))&&number(r,"reportedThrough")>=number(r,"effectiveFrom");
}
QJsonObject basalReport(const QJsonObject&settings,qint64 from,qint64 to){
    auto history=settings["reportedBasalHistory"].toArray();
    for(int i=history.size()-1;i>=0;--i){auto r=history[i].toObject();if(!validBasalReport(r,settings))continue;
        if(number(r,"effectiveFrom")<=to&&(number(r,"reportedThrough")>=to||r["ongoing"].toBool()))return number(r,"effectiveFrom")<=from?r:QJsonObject{};
    }return {};
}
QJsonObject contextKey(const QJsonObject&p,const QJsonObject&report){auto key=treatmentKey(p);if(!report.isEmpty()){key["basal"]=report["medicine"];key["dose"]=report["units"];}return key;}
QJsonArray issues(const QSet<QString>&codes){auto sorted=codes.values();sorted.sort();QJsonArray out;for(auto code:sorted)out<<QJsonObject{{"code",code},{"text",automaticIssueText(code)}};return out;}
QJsonObject overnightAnalysis(const QList<Reading>&stream,const QList<Reading>&history,const QList<Interval>&series,const QJsonArray&events,const QJsonArray&profiles,const QJsonObject&settings,qint64 availableUntil){
    if(settings["reportedBasalHistory"].toArray().isEmpty())return {{"status","needs_history"}};
    const QTimeZone zone(settings["timezone"].toString().toUtf8());
    if(series.isEmpty())return {{"status","no_data"}};
    const double low=number(settings,"targetLow"),high=number(settings,"targetHigh"),hypo=number(settings,"hypo");
    auto first=QDateTime::fromMSecsSinceEpoch(series.first().from,zone).date();
    const auto last=QDateTime::fromMSecsSinceEpoch(availableUntil,zone).date();
    QJsonObject latestReport;auto declared=settings["reportedBasalHistory"].toArray();for(int i=declared.size()-1;i>=0;--i){auto r=declared[i].toObject();if(validBasalReport(r,settings)){latestReport=r;break;}}
    auto signature=[](const QJsonObject&r){return hash(QJsonObject{{"units",r["units"]},{"medicine",r["medicine"]},{"time",r["time"]}});};
    QJsonArray rows;QList<double> allChanges,quietChanges,assessmentChanges,starts,ends;int covered=0,quiet=0,lows=0,quietLows=0,morningTarget=0,otherRegimen=0;double belowSeconds=0,coveredSeconds=0;
    for(auto day=first;day<=last;day=day.addDays(1)){
        const auto from=QDateTime(day,QTime(0,0),zone).toMSecsSinceEpoch(),to=QDateTime(day,QTime(6,0),zone).toMSecsSinceEpoch();
        if(to>availableUntil)continue;
        const auto report=basalReport(settings,from-4LL*86400000,to);if(report.isEmpty())continue;
        auto whole=stats(series,from,to,low,high),start=stats(series,from,from+30*minute,low,high),end=stats(series,to-30*minute,to,low,high);
        if(whole.coverage<=0)continue;
        if(signature(report)!=signature(latestReport)){++otherRegimen;continue;}
        QSet<QString> flags;if(number(report,"reportedThrough")<to)flags.insert("basal_continuation_assumed");bool rawLow=false,priorLow=false,unreliable=false,sensorOverlap=false;
        auto inspect=[&](const QList<Reading>&rs){auto it=std::lower_bound(rs.cbegin(),rs.cend(),from-12LL*3600000,[](const auto&r,qint64 t){return r.time<t;});const Reading *prior=nullptr;
            for(;it!=rs.cend()&&it->time<to;++it){if(it->mmol>0&&it->mmol<hypo){if(it->time>=from)rawLow=true;else priorLow=true;}if(it->time>=from){unreliable|=!it->reliable;if(prior&&prior->sensor!=it->sensor&&it->time-prior->time<2*minute)sensorOverlap=true;}prior=&*it;}};
        inspect(stream);inspect(history);
        const bool measured=whole.coverage>=.9&&start.coverage>=.8&&end.coverage>=.8&&start.points>=2&&end.points>=2;
        if(!measured)flags.insert("missing_glucose");
        if(rawLow){flags.insert("hypo");++lows;}
        if(priorLow)flags.insert("preceding_hypo");
        if(unreliable)flags.insert("unreliable_reading");
        if(sensorOverlap)flags.insert("sensor_overlap");
        const auto p=profileAt(profiles,from);double duration=number(p["actionModel"].toObject(),"durationMinutes");if(!positive(duration)||duration<30||duration>720){duration=240;flags.insert("window_assumed");}
        bool loggedInterference=false;QMap<QString,double> basalDays;
        for(auto value:events){auto e=value.toObject();if(e["local"].toBool())continue;const auto at=qint64(number(e,"time"));if(at>availableUntil)continue;
            if(at>=from-4LL*86400000&&at<to&&e["kind"]=="basal"&&!e["deleted"].toBool()&&!e["analysisExcluded"].toBool()){
                basalDays[QDateTime::fromMSecsSinceEpoch(at,zone).date().toString(Qt::ISODate)]+=number(e,"value");
                if(!report["medicine"].toString().isEmpty()&&e["medicine"]!=report["medicine"])flags.insert("basal_report_conflict");
            }
            if(at<=from-qRound64(duration*minute)||at>=to)continue;
            if(e["kind"]=="rapid"||e["kind"]=="carbs"||e["kind"]=="unknown"||e["kind"].toString().isEmpty()){loggedInterference=true;flags.insert("night_entries");}
        }
        for(double units:basalDays)if(std::abs(units-number(report,"units"))>1e-6)flags.insert("basal_report_conflict");
        double lowSecs=0;for(const auto&i:series){if(i.to<=from)continue;if(i.from>=to)break;if(i.value<hypo)lowSecs+=(std::min(i.to,to)-std::max(i.from,from))/1000.0;}
        coveredSeconds+=whole.coverage*(to-from)/1000.0;belowSeconds+=lowSecs;
        const double change=end.median-start.median;
        const bool comparable=measured&&!loggedInterference&&!unreliable&&!sensorOverlap&&!flags.contains("basal_report_conflict");
        const bool assessment=comparable&&!rawLow&&!priorLow;
        if(measured){++covered;allChanges<<change;if(end.median>=low&&end.median<=high)++morningTarget;}
        if(comparable){++quiet;if(rawLow)++quietLows;quietChanges<<change;starts<<start.median;ends<<end.median;}
        if(assessment)assessmentChanges<<change;
        rows<<QJsonObject{{"date",day.toString(Qt::ISODate)},{"from",double(from)},{"to",double(to)},{"coverage",whole.coverage},{"start",QJsonValue(start.median)},{"end",QJsonValue(end.median)},{"change",QJsonValue(change)},{"minimum",QJsonValue(whole.minimum)},{"maximum",QJsonValue(whole.maximum)},{"low",rawLow},{"belowMinutes",lowSecs/60},{"measured",measured},{"loggedQuiet",comparable},{"assessment",assessment},{"basalContext",report},{"issues",issues(flags)}};
    }
    qint64 observedFrom=0,observedTo=0;if(!rows.isEmpty()){observedFrom=qint64(rows.first().toObject()["from"].toDouble());observedTo=qint64(rows.last().toObject()["to"].toDouble());}
    auto period=[&](int offset){
        QDate endDate=rows.isEmpty()?last:QDate::fromString(rows.last().toObject()["date"].toString(),Qt::ISODate);endDate=endDate.addDays(-offset);auto startDate=endDate.addDays(-13);
        int count=0,measured=0,quietCount=0,lowCount=0;QList<double> changes;for(auto value:rows){auto r=value.toObject();auto d=QDate::fromString(r["date"].toString(),Qt::ISODate);if(d<startDate||d>endDate)continue;++count;if(r["measured"].toBool())++measured;if(r["low"].toBool())++lowCount;if(r["loggedQuiet"].toBool()){++quietCount;changes<<number(r,"change");}}
        return QJsonObject{{"firstDate",startDate.toString(Qt::ISODate)},{"lastDate",endDate.toString(Qt::ISODate)},{"nights",count},{"coveredNights",measured},{"loggedQuietNights",quietCount},{"lowNights",lowCount},{"medianChangeQuiet",QJsonValue(quantile(changes,.5))}};
    };
    return {{"status",rows.isEmpty()?"no_data":"observed"},{"window","00:00–06:00"},{"from",double(observedFrom)},{"to",double(observedTo)},{"observedNights",rows.size()},{"coveredNights",covered},{"loggedQuietNights",quiet},{"assessmentNights",assessmentChanges.size()},{"lowNights",lows},{"lowNightsQuiet",quietLows},{"otherRegimenNights",otherRegimen},{"last14",period(0)},{"previous14",period(14)},{"morningInTargetNights",morningTarget},{"medianChangeAll",QJsonValue(quantile(allChanges,.5))},{"medianChangeQuiet",QJsonValue(quantile(quietChanges,.5))},{"changeP25",QJsonValue(quantile(quietChanges,.25))},{"changeP75",QJsonValue(quantile(quietChanges,.75))},{"medianStartQuiet",QJsonValue(quantile(starts,.5))},{"medianEndQuiet",QJsonValue(quantile(ends,.5))},{"medianChangeAssessment",QJsonValue(quantile(assessmentChanges,.5))},{"belowMinutes",belowSeconds/60},{"timeBelow",coveredSeconds>0?QJsonValue(belowSeconds/coveredSeconds):QJsonValue()},{"source","patient_report"},{"fastingVerified",false},{"prescriptive",false},{"nights",rows}};
}
}

QString automaticIssueText(const QString&code){
    static const QMap<QString,QString> text{
        {"night_entries","Yön aikana tai sitä edeltävässä vaikutusikkunassa on HH-, pika- tai tunnistamattomia merkintöjä."},
        {"preceding_hypo","Yötä edeltäneen 12 tunnin aikana on matala raaka-arvo."},
        {"missing_dose","Aterian läheltä ei löytynyt pikainsuliinikirjausta."},
        {"ambiguous_pairing","Sama annos voi liittyä useampaan ateriaan."},
        {"multiple_doses","Aterian lähellä on useita annoksia; korjausosuutta ei voi erottaa."},
        {"duplicate_entry","Samanaikaisissa kirjauksissa on mahdollinen kaksoiskirjaus."},
        {"excluded_source","Ateriaan liittyvä kirjaus on poistettu tai jätetty pois arviosta."},
        {"unknown_label","Vaikutusjaksolla on tunnistamaton merkintäluokka."},
        {"unknown_medicine","Pikainsuliinin valmistetta ei tunnistettu."},
        {"mixed_medicine","Ateriaan liittyy eri pikainsuliinivalmisteita."},
        {"previous_rapid","Aiemman pikainsuliinin vaikutus voi jatkua aterialle."},
        {"previous_meal","Aiemman aterian vaikutus voi jatkua tälle aterialle."},
        {"next_meal","Seurannan aikana on toinen ateria tai HH-kirjaus."},
        {"later_dose","Seurannan aikana on lisäinsuliinia; aterian oma vaste sekoittuu."},
        {"carbs_before_endpoint","Kahden tunnin päätöspisteen sisällä on toinen ateria tai HH-kirjaus."},
        {"missing_glucose","Glukoositietoa puuttuu seurannasta tai alku-/loppuikkunasta."},
        {"hypo","Raakamittauksissa on matala glukoosiarvo. Ei tavoiteannoksen vertailuun."},
        {"sensor_overlap","Päällekkäisten sensorien arvoja ei voi tulkita yksiselitteisesti."},
        {"unreliable_reading","Jaksossa on epäluotettavaksi merkittyjä mittauksia."},
        {"pending","Aterian seuranta on vielä kesken."},
        {"window_assumed","Seurantaikkuna on tekninen oletus; insuliinin vaikutusaikaa ei ole ilmoitettu."},
        {"basal_unknown","Perusinsuliinin toteutumisesta ei ole riittävää kirjaushistoriaa."},
        {"basal_continuation_assumed","Ilmoitetun perushoidon jatkuminen on oletus viimeisimmän vahvistuksen jälkeen; päiväkohtaisia pistoksia ei ole varmennettu."},
        {"basal_reported","Perusinsuliinin päivittäinen toteutuminen perustuu käyttäjän ilmoitukseen."},
        {"basal_report_conflict","Perusinsuliinikirjaukset poikkeavat käyttäjän ilmoittamasta historiasta."},
        {"basal_change","Perusinsuliinin annos tai valmiste vaihtelee lähihistoriassa."},
        {"regimen_change","Hoito muuttuu kesken aterian seurannan."},
        {"starting_outside_goal","Aterian lähtöglukoosi ei ole valitulla tavoitealueella."},
        {"missing_endpoint","Kahden tunnin mittaus tai edeltävä glukoositrendi puuttuu."},
        {"short_followup","Myöhempi seuranta ei kata vähintään neljää tuntia."},
        {"invalid_values","Mallin lähtötiedoissa on puuttuva tai virheellinen arvo."},
        {"few_meals","Aterioita tai erillisiä havaintopäiviä on vielä vähän."},
        {"cohorts_separated","Eri insuliini-/perusannosjaksojen keskiarvot on erotettu."},
        {"wide_spread","Kirjattu annos–HH-suhde vaihtelee paljon."},
        {"stale_meals","Viimeisistä ateriamerkinnöistä on yli kaksi vuorokautta."},
        {"timing_groups_assumed","Ateriaryhmät perustuvat teknisiin kellorajoihin, eivät aterian nimimerkintään."},
        {"unobserved_context","Liikuntaa, aterian koostumusta ja kirjaamattomia tapahtumia ei voi sulkea pois."}};
    return text.value(code,code);
}

QJsonObject automaticMealAnalysis(const QList<Reading>&streamInput,const QList<Reading>&historyInput,const QJsonArray&amounts,
        const QJsonArray&profiles,const QJsonObject&settings,qint64 from,qint64 to,qint64 availableUntil){
    auto stream=streamInput,history=historyInput;auto byTime=[](const Reading&a,const Reading&b){return a.time==b.time?a.key<b.key:a.time<b.time;};std::sort(stream.begin(),stream.end(),byTime);std::sort(history.begin(),history.end(),byTime);
    const double low=number(settings,"targetLow"),high=number(settings,"targetHigh"),hypo=number(settings,"hypo");
    const QTimeZone zone(settings.value("timezone").toString("Europe/Helsinki").toUtf8());
    if(!zone.isValid()||to<=from)return {{"status","error"},{"message","Tarkista tarkastelujakso ja aikavyöhyke."}};
    if(!positive(low)||!positive(high)||low>=high||!positive(hypo)||hypo>low)return {{"status","needs_targets"},{"message","Aterioiden tavoitealue ja matalan glukoosin raja puuttuvat."}};
    const auto periods=groups(settings);qint64 readFrom=from-8LL*86400000;
    for(auto v:settings["reportedBasalHistory"].toArray()){auto r=v.toObject();if(r["source"]=="patient_report"&&positive(number(r,"effectiveFrom")))readFrom=std::min(readFrom,qint64(number(r,"effectiveFrom")));}
    const auto series=selectSeries(stream,history,readFrom,availableUntil,true,600);
    QList<Event> events;int ignoredLocal=0;QSet<QString> unknownLabels;
    for(auto v:amounts){auto a=v.toObject();if(a["local"].toBool()){++ignoredLocal;continue;}
        const double value=number(a,"value"),stamp=number(a,"time");if(!positive(value)||!positive(stamp))continue;const auto at=qint64(stamp);if(at>availableUntil)continue;
        QString kind=a["kind"].toString("unknown");if(kind=="unknown")unknownLabels.insert(a["label"].toString());
        events<<Event{a,at,a["id"].toString(),kind,a["medicine"].toString(),value,a["deleted"].toBool()||a["analysisExcluded"].toBool()};}
    std::sort(events.begin(),events.end(),[](const auto&a,const auto&b){return a.time==b.time?a.id<b.id:a.time<b.time;});
    QList<Meal> meals;for(int i=0;i<events.size();++i)if(events[i].kind=="carbs"){
        if(!meals.isEmpty()&&events[i].time-meals.last().time<=15*minute){meals.last().carbs<<i;meals.last().last=events[i].time;}
        else meals<<Meal{events[i].time,events[i].time,{i},{},{},false};}
    for(int i=0;i<events.size();++i)if(events[i].kind=="rapid"||events[i].kind=="correction"){
        QList<int> candidates;for(int j=0;j<meals.size();++j)if(events[i].time>=meals[j].time-30*minute&&events[i].time<=meals[j].last+30*minute)candidates<<j;
        if(candidates.size()==1){if(events[i].kind=="rapid")meals[candidates[0]].doses<<i;else meals[candidates[0]].corrections<<i;}
        else if(candidates.size()>1)for(int j:candidates)meals[j].ambiguous=true;
    }
    QJsonArray rows;QSet<QString> allIssues{"unobserved_context"};if(periods!=settings["groups"].toArray())allIssues.insert("timing_groups_assumed");qint64 lastMeal=0;
    for(const auto&m:meals){if(m.time<from||m.time>=to)continue;lastMeal=std::max(lastMeal,m.time);
        QSet<QString> flags;QSet<int> own;double carbs=0,dose=0,ownCorrection=0;QSet<QString> medicines;qint64 anchor=m.time,last=m.last;
        for(int i:m.carbs){own.insert(i);carbs+=events[i].value;if(events[i].blocked)flags.insert("excluded_source");}
        for(int i:m.doses){own.insert(i);dose+=events[i].value;anchor=std::min(anchor,events[i].time);last=std::max(last,events[i].time);medicines.insert(events[i].medicine);if(events[i].blocked)flags.insert("excluded_source");}
        for(int i:m.corrections){own.insert(i);ownCorrection+=events[i].value;anchor=std::min(anchor,events[i].time);last=std::max(last,events[i].time);medicines.insert(events[i].medicine);if(events[i].blocked)flags.insert("excluded_source");}
        if(m.doses.isEmpty())flags.insert("missing_dose");
        if(m.ambiguous)flags.insert("ambiguous_pairing");
        if(m.doses.size()>1)flags.insert("multiple_doses");
        if(medicines.contains(""))flags.insert("unknown_medicine");
        if(medicines.size()>1)flags.insert("mixed_medicine");
        for(int i:own)for(int j:own)if(i<j&&events[i].time==events[j].time&&events[i].kind==events[j].kind&&events[i].value==events[j].value&&events[i].row["label"]==events[j].row["label"])flags.insert("duplicate_entry");
        const auto profile=profileAt(profiles,m.time);double duration=number(profile["actionModel"].toObject(),"durationMinutes");
        if(!std::isfinite(duration)||duration<30||duration>720){duration=240;flags.insert("window_assumed");}
        const auto span=qRound64(duration*minute),end=last+span,historyFrom=anchor-span;
        if(end>availableUntil)flags.insert("pending");
        for(int i=0;i<events.size();++i){const auto&e=events[i];if(own.contains(i)||e.time<=historyFrom||e.time>=end)continue;
            if(e.kind=="unknown")flags.insert("unknown_label");
            if(e.kind=="rapid")flags.insert(e.time<anchor?"previous_rapid":"later_dose");
            // A correction dose is insulin too; before the meal it is still an active prior dose.
            if(e.kind=="correction"&&e.time<anchor)flags.insert("previous_rapid");
            if(e.kind=="carbs")flags.insert(e.time<m.time?"previous_meal":"next_meal");}
        auto baseline=stats(series,anchor-15*minute,anchor,low,high),endpoint=stats(series,end-15*minute,end,low,high),whole=stats(series,anchor,end,low,high);
        if(whole.coverage<.8||baseline.coverage<.8||endpoint.coverage<.8||baseline.points<2||endpoint.points<2)flags.insert("missing_glucose");
        if(std::isfinite(baseline.median)&&(baseline.median<low||baseline.median>high))flags.insert("starting_outside_goal");
        auto inspect=[&](const QList<Reading>&rs){auto first=std::lower_bound(rs.cbegin(),rs.cend(),anchor-30*minute,[](const auto&r,qint64 t){return r.time<t;});
            const Reading *previous=nullptr;for(auto it=first;it!=rs.cend()&&it->time<=std::min(end,availableUntil);++it){if(it->mmol>0&&it->mmol<hypo)flags.insert("hypo");if(!it->reliable)flags.insert("unreliable_reading");if(previous&&previous->sensor!=it->sensor&&it->time-previous->time<2*minute)flags.insert("sensor_overlap");previous=&*it;}};
        inspect(stream);inspect(history);
        QMap<QString,QPair<QString,double>> basalDays;bool mixedBasal=false;QString latestBasal;double latestBasalDose=NAN;
        for(const auto&e:events)if(e.kind=="basal"&&!e.blocked&&e.time>=m.time-4LL*86400000&&e.time<=m.time){const auto day=QDateTime::fromMSecsSinceEpoch(e.time,zone).date().toString(Qt::ISODate);auto&entry=basalDays[day];if(!entry.first.isEmpty()&&entry.first!=e.medicine)mixedBasal=true;entry.first=e.medicine;entry.second+=e.value;latestBasal=e.medicine;latestBasalDose=e.value;}
        const auto report=basalReport(settings,m.time-4LL*86400000,end);
        if(!report.isEmpty()){
            flags.insert("basal_reported");
            if(number(report,"reportedThrough")<end)flags.insert("basal_continuation_assumed");
            for(auto entry:basalDays)if(std::abs(entry.second-number(report,"units"))>1e-6||(!report["medicine"].toString().isEmpty()&&entry.first!=report["medicine"].toString()))flags.insert("basal_report_conflict");
            latestBasal=report["medicine"].toString();latestBasalDose=number(report,"units");
        }else if(basalDays.size()<3||latestBasal.isEmpty())flags.insert("basal_unknown");
        QSet<QString> basalSignatures;for(auto entry:basalDays)basalSignatures.insert(entry.first+":"+QString::number(entry.second,'g',12));if(mixedBasal||basalSignatures.size()>1)flags.insert("basal_change");
        const auto endProfile=profileAt(profiles,end);if(!profile.isEmpty()&&!endProfile.isEmpty()&&contextKey(profile,report)!=contextKey(endProfile,report))flags.insert("regimen_change");
        auto medicineList=medicines.values();medicineList.sort();auto cohort=hash(QJsonObject{{"rapid",QJsonArray::fromStringList(medicineList)},{"basal",latestBasal},{"basalDose",std::isfinite(latestBasalDose)?QJsonValue(latestBasalDose):QJsonValue()},{"profile",contextKey(profile,report)}});
        const bool paired=positive(carbs)&&positive(dose)&&!m.ambiguous&&!flags.contains("excluded_source")&&!flags.contains("duplicate_entry");
        const QSet<QString> blocking{"missing_dose","ambiguous_pairing","multiple_doses","duplicate_entry","excluded_source","unknown_label","unknown_medicine","mixed_medicine","previous_rapid","previous_meal","next_meal","later_dose","missing_glucose","hypo","sensor_overlap","unreliable_reading","pending","basal_change","basal_report_conflict","regimen_change","starting_outside_goal"};
        bool clean=paired;for(const auto&flag:flags)if(blocking.contains(flag))clean=false;
        // A target observation means all supported glucose intervals were inside the goal.
        // This is a retrospective observation, not a claim about missing measurements.
        const bool goal=clean&&whole.coverage>=.95&&whole.minimum>=low&&whole.maximum<=high;
        // A fixed outcome horizon is measured from the first carbohydrate entry.
        // A high start or a measured low is not selected out of model fitting.
        const auto twoHour=stats(series,m.time+112*minute+30000,m.time+127*minute+30000,low,high);
        // The modelled endpoint is the return to the starting level at the end of the
        // standard four-hour window; a rise in between is allowed.
        const auto fourHour=stats(series,m.time+225*minute,m.time+240*minute,low,high);
        const auto prior=stats(series,anchor-30*minute,anchor-15*minute,low,high);
        QSet<QString> modelFlags=flags;
        for(const auto &allowed:QStringList{"starting_outside_goal","hypo","basal_reported","basal_continuation_assumed"})modelFlags.remove(allowed);
        if(prior.coverage<.8||prior.points<2||fourHour.coverage<.8||fourHour.points<2)modelFlags.insert("missing_endpoint");
        if(duration<240||end<m.time+240*minute)modelFlags.insert("short_followup");
        // Recent lows can alter the subsequent response even without a rescue log.
        for(const auto &rs:{stream,history}){
            auto it=std::lower_bound(rs.cbegin(),rs.cend(),anchor-12LL*3600000,[](const auto&r,qint64 t){return r.time<t;});
            for(;it!=rs.cend()&&it->time<anchor;++it)if(it->mmol>0&&it->mmol<hypo){modelFlags.insert("preceding_hypo");break;}
        }
        // Peak rise and the first return to the starting level inside the four-hour window.
        double peakValue=NAN,peakMinutes=NAN,returnMinutes=NAN;qint64 peakTime=0;
        for(const auto&i:series){if(i.to<=m.time)continue;if(i.from>=m.time+240*minute)break;
            if(std::isnan(peakValue)||i.value>peakValue){peakValue=i.value;peakTime=std::max(i.from,m.time);}}
        if(!std::isnan(peakValue))peakMinutes=(peakTime-m.time)/double(minute);
        if(std::isfinite(baseline.median))for(const auto&i:series){if(i.from<=peakTime+30*minute)continue;if(i.from>=m.time+240*minute)break;
            if(i.value<=baseline.median+.5&&i.to-i.from>=60000){returnMinutes=(i.from-m.time)/double(minute);break;}}
        auto sortedModelFlags=modelFlags.values();sortedModelFlags.sort();
        // Total rapid insulin in the fixed two-hour window: the meal's own bolus plus every
        // correction dose recorded before the endpoint. A later dose is given because the
        // glucose was rising, so it is not an independent predictor; this is an explicit
        // limitation of the window basis, not a property of the fit.
        const double endpointEnd=m.time+240*minute;double windowUnits=0,windowCorrection=0;
        for(const auto&e:events){if(e.blocked)continue;if(e.kind!="rapid"&&e.kind!="correction")continue;if(e.time>=anchor&&e.time<=endpointEnd){windowUnits+=e.value;if(e.kind=="correction")windowCorrection+=e.value;}}
        QSet<QString> windowFlags=modelFlags;
        windowFlags.remove("later_dose");windowFlags.remove("next_meal");windowFlags.remove("multiple_doses");
        for(int i=0;i<events.size();++i){const auto&e=events[i];if(own.contains(i))continue;if(e.kind=="carbs"&&e.time>=m.time&&e.time<=endpointEnd)windowFlags.insert("carbs_before_endpoint");}
        if(!positive(windowUnits))windowFlags.insert("missing_dose");
        auto sortedWindowFlags=windowFlags.values();sortedWindowFlags.sort();
        const double bolusOffset=m.doses.size()==1?(events[m.doses.first()].time-m.time)/double(minute):NAN;
        const double windowBolusOffset=m.doses.isEmpty()?NAN:(events[m.doses.first()].time-m.time)/double(minute);
        QStringList sourceIds;for(int i:own)sourceIds<<events[i].id;sourceIds.sort();const auto ids=QJsonArray::fromStringList(sourceIds);
        rows<<QJsonObject{{"id",events[m.carbs.first()].id},{"sourceIds",ids},{"time",double(m.time)},{"end",double(end)},{"group",groupAt(periods,m.time,zone)},{"day",QDateTime::fromMSecsSinceEpoch(m.time,zone).date().toString(Qt::ISODate)},{"carbs",carbs},{"units",dose},{"unitsPer10g",paired?QJsonValue(10*dose/carbs):QJsonValue()},{"paired",paired},{"modelEligible",paired&&modelFlags.isEmpty()},{"modelExclusions",QJsonArray::fromStringList(sortedModelFlags)},{"glucose2h",QJsonValue(twoHour.median)},{"endpointGlucose",QJsonValue(fourHour.median)},{"peakGlucose",QJsonValue(peakValue)},{"peakMinutes",QJsonValue(peakMinutes)},{"returned",!std::isnan(returnMinutes)},{"returnMinutes",QJsonValue(returnMinutes)},{"premealTrend",QJsonValue(baseline.median-prior.median)},{"bolusOffsetMinutes",QJsonValue(bolusOffset)},{"windowUnits",windowUnits},{"windowUnitsPer10g",positive(carbs)?QJsonValue(10*windowUnits/carbs):QJsonValue()},{"correctionUnits",ownCorrection},{"windowCorrectionUnits",windowCorrection},{"windowEligible",paired&&positive(windowUnits)&&windowFlags.isEmpty()},{"windowExclusions",QJsonArray::fromStringList(sortedWindowFlags)},{"windowBolusOffsetMinutes",QJsonValue(windowBolusOffset)},{"clean",clean},{"goalObserved",goal},{"lowObserved",flags.contains("hypo")},{"coverage",whole.coverage},{"startGlucose",QJsonValue(baseline.median)},{"endGlucose",QJsonValue(endpoint.median)},{"inTarget",QJsonValue(whole.inTarget)},{"minimum",QJsonValue(whole.minimum)},{"maximum",QJsonValue(whole.maximum)},{"historySeconds",whole.historySeconds},{"windowMinutes",duration},{"cohort",cohort},{"basalContext",report.isEmpty()?QJsonObject{{"source","juggluco_or_unknown"}}:report},{"issues",issues(flags)}};
        allIssues.unite(flags);
    }
    QJsonArray summaries;
    for(int group=0;group<4;++group){QString cohort;for(auto v:rows){auto r=v.toObject();if(r["group"].toInt()==group&&r["paired"].toBool())cohort=r["cohort"].toString();}
        QList<double> ratios,cleanRatios,goalRatios,units,carbs,coverages,targetTimes;QSet<QString> days,goalDays,codes;QMap<int,QJsonArray> buckets;int count=0,cleanCount=0,hypoCount=0,earlier=0,pending=0;
        for(auto v:rows){auto r=v.toObject();if(r["group"].toInt()!=group)continue;++count;if(r["lowObserved"].toBool())++hypoCount;if(r["end"].toDouble()>availableUntil)++pending;for(auto issue:r["issues"].toArray())codes.insert(issue.toObject()["code"].toString());
            if(r["cohort"]!=cohort){++earlier;continue;}if(!r["paired"].toBool())continue;
            ratios<<number(r,"unitsPer10g");units<<number(r,"units");carbs<<number(r,"carbs");days.insert(r["day"].toString());coverages<<number(r,"coverage");if(r["inTarget"].isDouble())targetTimes<<number(r,"inTarget");
            if(r["clean"].toBool()){cleanRatios<<number(r,"unitsPer10g");++cleanCount;}if(r["goalObserved"].toBool()){goalRatios<<number(r,"unitsPer10g");goalDays.insert(r["day"].toString());}
            buckets[int(number(r,"carbs")/20)*20]<<r;
        }
        if(ratios.size()<5||days.size()<3)codes.insert("few_meals");
        if(earlier)codes.insert("cohorts_separated");
        if(positive(mean(ratios))&&(quantile(ratios,.9)-quantile(ratios,.1))/mean(ratios)>.5)codes.insert("wide_spread");
        QJsonArray carbBands;for(auto it=buckets.cbegin();it!=buckets.cend();++it){QList<double> doses,goalDoses;int goals=0;for(auto v:it.value()){auto r=v.toObject();doses<<number(r,"units");if(r["goalObserved"].toBool()){++goals;goalDoses<<number(r,"units");}}carbBands<<QJsonObject{{"from",it.key()},{"to",it.key()+20},{"count",doses.size()},{"meanUnits",QJsonValue(mean(doses))},{"goalCount",goals},{"goalMeanUnits",QJsonValue(mean(goalDoses))}};}
        // Descriptive return-to-start outcome: share of paired meals back at the starting
        // level inside the four-hour window, the median time and the median peak rise.
        int returnedMeals=0,correctionMeals=0;QList<double> returnMinutes,peakRises,correctionValues,observedResponse;
        // Descriptive, updating response: units actually given per 10 g of the same sugar
        // load the model uses. It says what was done, not what is needed.
        for(auto v:rows){auto r=v.toObject();if(r["group"].toInt()!=group||r["cohort"].toString()!=cohort)continue;if(!r["paired"].toBool())continue;
            const double carbs=number(r,"carbs"),start=number(r,"startGlucose"),units=number(r,"windowUnits");
            if(!std::isfinite(start)||!std::isfinite(units))continue;
            const double load=carbs+sugarLoadGramsPerMmol*(start-theoreticalTargetMmol);
            if(std::abs(load)<1)continue;
            observedResponse<<10*units/load;
        }
        for(auto v:rows){auto r=v.toObject();if(r["group"].toInt()!=group||r["cohort"].toString()!=cohort)continue;
            if(r["returned"].toBool()){++returnedMeals;returnMinutes<<number(r,"returnMinutes");}
            if(r["windowCorrectionUnits"].isDouble()&&number(r,"windowCorrectionUnits")>0){++correctionMeals;correctionValues<<number(r,"windowCorrectionUnits");}
            if(r["peakGlucose"].isDouble()&&r["startGlucose"].isDouble())peakRises<<number(r,"peakGlucose")-number(r,"startGlucose");}
        // A single clock group is often too thin. Fall back to one all-day model with the
        // documented prototype minimum of 12 meals and 8 days, and label the scope.
        auto perGroup=theoreticalMealDose(rows,group,cohort,theoreticalTargetMmol,true,24,12,true);
        auto pooled=theoreticalMealDose(rows,-1,cohort,theoreticalTargetMmol,true,12,8,true);
        auto primary=perGroup["status"].toString()=="estimated"?perGroup:pooled;
        if(primary["scope"].toString()=="all-day")primary["pooledFallback"]=true;
        summaries<<QJsonObject{{"index",group},{"name",periods[group].toObject()["name"]},{"startMinute",periods[group].toObject()["startMinute"]},{"endMinute",periods[(group+1)%4].toObject()["startMinute"]},{"meals",count},{"pairedMeals",ratios.size()},{"days",days.size()},{"cleanMeals",cleanCount},{"goalMeals",goalRatios.size()},{"goalDays",goalDays.size()},{"lowMeals",hypoCount},{"pendingMeals",pending},{"otherCohortMeals",earlier},{"recordedMeanPer10g",QJsonValue(mean(ratios))},{"cleanMeanPer10g",QJsonValue(mean(cleanRatios))},{"goalMeanPer10g",QJsonValue(mean(goalRatios))},{"spreadLow",QJsonValue(quantile(ratios,.1))},{"spreadHigh",QJsonValue(quantile(ratios,.9))},{"meanUnits",QJsonValue(mean(units))},{"meanCarbs",QJsonValue(mean(carbs))},{"meanCoverage",QJsonValue(mean(coverages))},{"meanTimeInTarget",QJsonValue(mean(targetTimes))},{"status",ratios.isEmpty()?"no_data":(ratios.size()<5||days.size()<3?"few":"observed")},{"carbBands",carbBands},{"observedResponsePer10g",QJsonValue(quantile(observedResponse,.5))},{"observedResponseLow",QJsonValue(quantile(observedResponse,.1))},{"observedResponseHigh",QJsonValue(quantile(observedResponse,.9))},{"observedResponseMeals",observedResponse.size()},{"returnedMeals",returnedMeals},{"correctionMeals",correctionMeals},{"meanCorrectionUnits",QJsonValue(mean(correctionValues))},{"medianReturnMinutes",QJsonValue(quantile(returnMinutes,.5))},{"medianPeakRise",QJsonValue(quantile(peakRises,.5))},{"theoreticalDose",primary},{"theoreticalDosePerGroup",perGroup},{"theoreticalDosePooled",pooled},{"theoreticalDoseMealOnly",theoreticalMealDose(rows,group,cohort,theoreticalTargetMmol,false,24,12,true)},{"issues",issues(codes)}};
    }
    if(lastMeal&&availableUntil-lastMeal>2LL*86400000)allIssues.insert("stale_meals");
    if(!unknownLabels.isEmpty())allIssues.insert("unknown_label");
    auto sortedUnknown=unknownLabels.values();sortedUnknown.sort();
    return {{"status","ready"},{"method","automatic-observations-v1"},{"from",double(from)},{"to",double(to)},{"availableUntil",double(availableUntil)},{"targetLow",low},{"targetHigh",high},{"hypo",hypo},{"groups",summaries},{"meals",rows},{"issues",issues(allIssues)},{"ignoredLocalEntries",ignoredLocal},{"unknownLabels",QJsonArray::fromStringList(sortedUnknown)},{"lastMeal",double(lastMeal)},{"prescriptive",false},{"sourceCompletenessVerified",false},{"timingGroups",periods},{"overnight",overnightAnalysis(stream,history,series,amounts,profiles,settings,availableUntil)}};
}

QJsonObject automaticMealSnapshot(const QString&path,const QJsonObject&settings,qint64 from,qint64 to,qint64 availableUntil){
    Store db(path,true);if(!db.good()||!db.run("BEGIN"))return {{"status","error"},{"message","Aineiston lukeminen ei onnistunut."}};
    const QString revision=db.revision();QJsonArray profiles;for(auto v:db.profiles()){auto row=v.toObject();auto p=QJsonDocument::fromJson(row["data"].toString().toUtf8()).object();p["version"]=row["version"];profiles<<p;}
    qint64 readFrom=from-8LL*86400000;for(auto v:settings["reportedBasalHistory"].toArray()){auto r=v.toObject();if(r["source"]=="patient_report"&&positive(number(r,"effectiveFrom")))readFrom=std::min(readFrom,qint64(number(r,"effectiveFrom")));}
    auto stream=db.readings(readFrom,availableUntil+1,"stream"),history=db.readings(readFrom,availableUntil+1,"history");auto amounts=db.amounts(readFrom,availableUntil+1,true);
    if(!db.good()||!db.run("COMMIT"))return {{"status","error"},{"message","Aineiston lukeminen ei onnistunut."}};
    auto result=automaticMealAnalysis(stream,history,amounts,profiles,settings,from,to,availableUntil);result["sourceRevision"]=revision;result["settings"]=settings;return result;
}
}
