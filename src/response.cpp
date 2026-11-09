// SPDX-License-Identifier: GPL-3.0-or-later
#include "response.h"
#include <QDateTime>
#include <QTimeZone>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QMap>
#include <random>
#include <algorithm>
#include <cmath>
namespace Studio {
static bool pos(double v){return std::isfinite(v)&&v>0;}
static double quantile(QList<double> v,double q=.5){if(v.isEmpty())return NAN;std::sort(v.begin(),v.end());
    double x=(v.size()-1)*q;
    int i=int(x);
    return v[i]+(v[std::min(i+1,int(v.size()-1))]-v[i])*(x-i);}
static QJsonObject blocked(const QStringList&reasons){return {{"status","blocked"},{"reasons",QJsonArray::fromStringList(reasons)},{"modelVersion","response-v2"}};}
QJsonObject defaultResponsePolicy(){
    QJsonArray groups;
    for(auto name:{"Aamu","Päivä","Iltapäivä","Ilta/yö"})groups<<QJsonObject{{"name",name},{"startMinute",QJsonValue()},{"externalIsf",QJsonValue()}};
    QJsonObject p{{"version",2},{"approved",false},{"groups",groups},{"bootstrapSamples",2000},{"blockDays",1},{"endpointMinutes",15}};
    for(auto key:{"minimumEpisodes","minimumDays","minCoverage","maxRelativeWidth","maxDayInfluencePercent","maxInvalidFraction","doseStepPercent","maximumChangePercent","minimumDose","minimumCarbs","minimumBasalDays","doseMatchToleranceUnits","maxPeriodDriftPercent"})p[key]=QJsonValue();
    return p;
}
QStringList validateResponsePolicy(const QJsonObject&p){
    QStringList errors;
    if(p["version"].toInt()!=2)errors<<"Vastepolitiikan version pitää olla 2.";
    for(auto key:{"minimumEpisodes","minimumDays","minCoverage","maxRelativeWidth","maxDayInfluencePercent","maxInvalidFraction","doseStepPercent","maximumChangePercent","minimumDose","minimumCarbs","minimumBasalDays","doseMatchToleranceUnits","maxPeriodDriftPercent"})if(!pos(number(p,key)))errors<<"Määritä vastepolitiikan raja: "+QString(key);
    for(auto key:{"minimumEpisodes","minimumDays","minimumBasalDays","bootstrapSamples","blockDays","endpointMinutes"})if(!pos(number(p,key))||std::floor(number(p,key))!=number(p,key))errors<<"Vaaditaan positiivinen kokonaisluku: "+QString(key);
    if(number(p,"minimumDays")<2||number(p,"minCoverage")>1||number(p,"maxInvalidFraction")>=1||number(p,"doseStepPercent")>number(p,"maximumChangePercent")||number(p,"maximumChangePercent")>=100||number(p,"bootstrapSamples")<200||number(p,"bootstrapSamples")>10000||number(p,"blockDays")>7||number(p,"endpointMinutes")>60)errors<<"Vastepolitiikan osuudet, askel tai tekniset rajat eivät kelpaa.";
    const auto groups=p["groups"].toArray();
    if(groups.size()!=4)errors<<"Määritä neljä vuorokaudenaikaa.";
    double previous=-1;QSet<QString> names;
    for(auto v:groups){auto g=v.toObject();
    double start=number(g,"startMinute");if(names.contains(g["name"].toString().trimmed()))errors<<"Vuorokaudenaikojen nimet eivät saa toistua.";names.insert(g["name"].toString().trimmed());
    if(!std::isfinite(start)||start<0||start>=1440||std::floor(start)!=start||start<=previous||g["name"].toString().trimmed().isEmpty())errors<<"Vuorokaudenaikojen rajojen pitää olla kasvavassa järjestyksessä (minuuttia keskiyöstä).";
    previous=start;
    if(!g["externalIsf"].isNull()&&!g["externalIsf"].isUndefined()&&!pos(number(g,"externalIsf")))errors<<"Ulkoinen korjauskerroin ei kelpaa.";}
    return errors;
}
int responsePeriod(const QJsonObject&p,qint64 time,const QString&zone){auto t=QDateTime::fromMSecsSinceEpoch(time,QTimeZone(zone.toUtf8())).time();
    int minute=t.hour()*60+t.minute(),result=3,index=0;
    for(auto v:p["groups"].toArray()){if(v.toObject()["startMinute"].toDouble()>minute)break;result=index++;}return result;}
struct Obs {QString id,day,kind;
    int group=0,doseGroup=0;
    double e=0,carbs=0,dose=0;
    bool hypo=false;};
struct Fit {double s[4]={NAN,NAN,NAN,NAN},q[4]={NAN,NAN,NAN,NAN};
    int sn[4]={},sd[4]={},qn[4]={},qd[4]={};
    bool conditional[4]={};};
static Fit fit(const QMap<QString,QList<Obs>>&byDay,const QStringList&days,const QJsonObject&policy){
    Fit out;
    for(int group=0;group<4;++group){QList<double> values;
    for(const auto&day:days){QList<double> v;
    for(const auto&o:byDay.value(day))if(o.kind=="isf"&&o.group==group&&o.dose>0&&-o.e/o.dose>0){v<<-o.e/o.dose;++out.sn[group];}
    if(!v.isEmpty()){values<<quantile(v);++out.sd[group];}}out.s[group]=quantile(values);}
    for(int group=0;group<4;++group){QList<double> values;
    for(const auto&day:days){QList<double> v;
    for(const auto&o:byDay.value(day))if(o.kind=="icr"&&o.group==group){double s=out.s[o.doseGroup];
    if(!pos(s)){s=number(policy["groups"].toArray()[o.doseGroup].toObject(),"externalIsf");
    if(pos(s))out.conditional[group]=true;}
    if(!pos(s)||o.carbs<=0)continue;
    double q=(o.dose+o.e/s)/o.carbs;
    if(!pos(q))continue;
    v<<q;++out.qn[group];}
    if(!v.isEmpty()){values<<quantile(v);++out.qd[group];}}out.q[group]=quantile(values);}return out;
}
static double currentRatio(const QJsonObject&,const QJsonObject&,int);
bool responseProfileApproved(const QJsonObject&p){return p["approved"].toBool()||p["responseApproved"].toBool();}
QJsonObject responseEstimate(const QJsonObject&profile,const QJsonObject&policy,const QJsonArray&episodes,qint64 from,qint64 to,bool unsafe,qint64 availableUntil){
    auto errors=validateResponsePolicy(policy);errors<<validateResponseProfile(profile);
    if(!policy["approved"].toBool()||!responseProfileApproved(profile))errors<<"Hyväksy profiili vastearvioon ja vastepolitiikka.";
    if(to<=from)errors<<"Virheellinen vastejakso.";
    if(!errors.isEmpty())return blocked(errors);
    const QString zone=profile["timezone"].toString();
    QList<Obs> obs;
    QJsonArray excluded,mealOutcomes;QSet<QString> basalDays,daysSet;
    bool anyHypo=unsafe;
    int invalid[4]={};
    bool inconsistentS[4]={};
    for(auto v:episodes){const auto e=v.toObject();
    const auto cohortTime=qint64(e["kind"]=="icr"?e["mealTime"].toDouble(number(e,"start")):e["doseTime"].toDouble(number(e,"start")));if(cohortTime<from||cohortTime>=to||number(e,"end")>(availableUntil>0?availableUntil:to))continue;
        if(e["hypoObserved"].toBool())anyHypo=true;
        QStringList why;
    if(number(e,"start")<number(profile,"effectiveFrom"))why<<"Jakso edeltää profiilin voimassaoloa.";
    if(e["profileVersion"]!=profile["version"])why<<"Eri hoitoprofiili.";
    if(e["endpointMinutes"]!=policy["endpointMinutes"])why<<"Alku-/loppuikkunan menetelmä on muuttunut.";
    if(e["methodVersion"].toInt()!=2)why<<"Jakso tarvitsee v2:n alku-/loppuikkunoiden tarkistuksen.";
        for(auto key:{"reviewed","complete","contextClear","endStable","basalStable"})if(!e[key].toBool())why<<"Tarkistus puuttuu: "+QString(key);
        if(!pos(number(e,"coverage"))||!pos(number(e,"start"))||!std::isfinite(number(e,"end"))||number(e,"end")<=number(e,"start")||number(e,"end")-number(e,"start")<number(profile["actionModel"].toObject(),"durationMinutes")*60000)why<<"Jakson aika, kesto tai kattavuus on virheellinen.";
        if(!QStringList{"icr","isf","basal"}.contains(e["kind"].toString()))why<<"Tuntematon jaksotyyppi.";
        if(e["stale"].toBool()||number(e,"coverage")<number(policy,"minCoverage"))why<<"Lähde muuttunut tai kattavuus riittämätön.";
        const auto start=qint64(number(e,"start"));
    const qint64 anchor=qint64(e["kind"]=="icr"?e["mealTime"].toDouble(start):e["doseTime"].toDouble(start));
    const int group=responsePeriod(policy,anchor,zone);
    const auto day=QDateTime::fromMSecsSinceEpoch(anchor,QTimeZone(zone.toUtf8())).date().toString(Qt::ISODate);
        const QString kind=e["kind"].toString();
    double dose=number(e,"rapid"),carbs=number(e,"carbs"),delta=number(e,"lastGlucose")-number(e,"firstGlucose");
        if(!std::isfinite(delta))why<<"Loppuvaste puuttuu.";
        if(kind=="basal"){
            if(why.isEmpty()&&std::abs(delta)<=number(profile["rules"].toObject(),"basalDeviation"))basalDays.insert(day);
            else if(why.isEmpty())why<<"Perusvaikutuksen trendi ei ole vakaa.";
        }else{
            if(!e["basalHistoryComplete"].toBool())why<<"Perusannosten toteutuminen tarkistamatta.";
            if(!pos(dose)||dose<number(policy,"minimumDose"))why<<"Annos liian pieni tai puuttuu.";
            if(kind=="icr"&&(number(e,"firstGlucose")<number(profile,"targetLow")||number(e,"firstGlucose")>number(profile,"targetHigh")))why<<"Aterian lähtöarvo ei ole hyväksytyllä alueella.";
            if(kind=="icr"&&(!pos(carbs)||carbs<number(policy,"minimumCarbs")))why<<"HH-tieto puuttuu tai on liian pieni.";
            if(kind=="isf"&&!(delta<0)){if(why.isEmpty())inconsistentS[group]=true;why<<"Korjauksen laskuvaste ei ole positiivinen: ryhmän mallioletus tarkistettava.";}
            if(e["hypoObserved"].toBool())why<<"Hypo: mukana turvallisuusarviossa, ei parametrin sovituksessa.";
            if(why.isEmpty()){const auto doseTime=qint64(e["doseTime"].toDouble(start));obs<<Obs{e["id"].toString(),day,kind,group,responsePeriod(policy,doseTime,zone),delta,carbs,dose,e["hypoObserved"].toBool()};daysSet.insert(day);
    if(kind=="icr")mealOutcomes<<QJsonObject{{"group",group},{"id",e["id"]},{"endGlucose",e["lastGlucose"]},{"carbs",carbs},{"dose",dose}};}
        }
        if(!why.isEmpty()){++invalid[group];excluded<<QJsonObject{{"id",e["id"]},{"reasons",QJsonArray::fromStringList(why)}};}
    }
    QStringList days=daysSet.values();days.sort();
    QMap<QString,QList<Obs>> byDay;for(const auto&o:obs)byDay[o.day]<<o;
    const Fit point=fit(byDay,days,policy);
    bool invalidQ[4]={};
    for(auto o:obs)if(o.kind=="icr"){double s=point.s[o.doseGroup];
    if(!pos(s))s=number(policy["groups"].toArray()[o.doseGroup].toObject(),"externalIsf");
    if(pos(s)&&!(o.dose+o.e/s>0))invalidQ[o.group]=true;}QList<double> sBoot[4],qBoot[4];
    QJsonArray sDraws[4],qDraws[4];
    const int samples=policy["bootstrapSamples"].toInt(),block=policy["blockDays"].toInt();
    const auto digest=QCryptographicHash::hash(QJsonDocument(episodes).toJson(QJsonDocument::Compact)+QByteArray::number(from)+":"+QByteArray::number(to),QCryptographicHash::Sha256);quint32 seed=0;
    for(int i=0;i<4;++i){seed=(seed<<8)|quint8(digest[i]);}
    std::mt19937 random(seed);
    // Sample calendar-day blocks jointly for correction and meal observations.
    QStringList calendar;
    const auto z=QTimeZone(zone.toUtf8());
    for(auto d=QDateTime::fromMSecsSinceEpoch(from,z).date();QDateTime(d,QTime(0,0),z).toMSecsSinceEpoch()<to;d=d.addDays(1))calendar<<d.toString(Qt::ISODate);
    if(!calendar.isEmpty())for(int n=0;n<samples;++n){QStringList drawn;while(drawn.size()<calendar.size()){int start=int(random()%calendar.size());
    for(int k=0;k<block&&drawn.size()<calendar.size();++k)drawn<<calendar[(start+k)%calendar.size()];}auto f=fit(byDay,drawn,policy);
    for(int g=0;g<4;++g){if(pos(f.s[g]))sBoot[g]<<f.s[g];
    if(pos(f.q[g])&&!f.conditional[g])qBoot[g]<<f.q[g];
    sDraws[g]<<QJsonValue(pos(f.s[g])?f.s[g]:NAN);qDraws[g]<<QJsonValue(pos(f.q[g])&&!f.conditional[g]?f.q[g]:NAN);}}
    QStringList earlyDays,lateDays;const auto half=QDateTime::fromMSecsSinceEpoch(from,z).addDays(calendar.size()/2).toMSecsSinceEpoch();for(const auto&day:days)(QDateTime(QDate::fromString(day,Qt::ISODate),QTime(0,0),z).toMSecsSinceEpoch()<half?earlyDays:lateDays)<<day;
    const auto early=fit(byDay,earlyDays,policy),late=fit(byDay,lateDays,policy);bool driftingS[4]={};for(int g=0;g<4;++g)driftingS[g]=!pos(early.s[g])||!pos(late.s[g])||std::abs(late.s[g]/early.s[g]-1)*100>number(policy,"maxPeriodDriftPercent");
    double sInfluence[4]={};bool unstableS[4]={};
    for(const auto&day:days){auto others=days;others.removeAll(day);auto f=fit(byDay,others,policy);for(int g=0;g<4;g++)if(pos(point.s[g])){if(!pos(f.s[g]))unstableS[g]=true;else sInfluence[g]=std::max(sInfluence[g],std::abs(f.s[g]/point.s[g]-1)*100);}}
    bool isfSupported[4]={};for(int g=0;g<4;g++)isfSupported[g]=!driftingS[g]&&!inconsistentS[g]&&!unstableS[g]&&sInfluence[g]<=number(policy,"maxDayInfluencePercent")&&point.sn[g]>=number(policy,"minimumEpisodes")&&point.sd[g]>=number(policy,"minimumDays")&&sBoot[g].size()>=samples*(1-number(policy,"maxInvalidFraction"))&&pos(quantile(sBoot[g],.025))&&(quantile(sBoot[g],.975)-quantile(sBoot[g],.025))/point.s[g]<=number(policy,"maxRelativeWidth")&&basalDays.size()>=number(policy,"minimumBasalDays");
    QJsonArray groups;
    for(int g=0;g<4;++g){QJsonObject row{{"index",g},{"name",policy["groups"].toArray()[g].toObject()["name"]},{"corrections",point.sn[g]},{"correctionDays",point.sd[g]},{"meals",point.qn[g]},{"mealDays",point.qd[g]},{"excluded",invalid[g]},{"conditional",point.conditional[g]}};
    QStringList why;
        const bool sEnough=point.sn[g]>=number(policy,"minimumEpisodes")&&point.sd[g]>=number(policy,"minimumDays");
        const bool rEnough=point.qn[g]>=number(policy,"minimumEpisodes")&&point.qd[g]>=number(policy,"minimumDays");
        // Never silently use an underpowered empirical correction estimate.
        bool correctionSupport=true;
    for(auto o:obs)if(o.kind=="icr"&&o.group==g){int j=o.doseGroup;
    if(!isfSupported[j])correctionSupport=false;}
        if(sEnough&&pos(point.s[g])){row["isf"]=point.s[g];
    if(sBoot[g].size()>=samples*(1-number(policy,"maxInvalidFraction"))){row["isfLow"]=quantile(sBoot[g],.025);
    row["isfHigh"]=quantile(sBoot[g],.975);}}
        if(rEnough&&pos(point.q[g])){row["q"]=point.q[g];
    row["ratio"]=1/point.q[g];}
        row["isfStatus"]=isfSupported[g]?"estimated":"insufficient";
        QStringList isfWhy;if(!sEnough)isfWhy<<"Korjauksia tai erillisiä korjauspäiviä liian vähän.";if(driftingS[g])isfWhy<<"Korjausvaste puuttuu toiselta osajaksolta tai muuttuu liikaa.";if(inconsistentS[g])isfWhy<<"Korjauksen laskuvasteoletus ei toteudu.";if(unstableS[g]||sInfluence[g]>number(policy,"maxDayInfluencePercent"))isfWhy<<"Korjausvaste riippuu liikaa yhdestä päivästä.";if(sBoot[g].size()<samples*(1-number(policy,"maxInvalidFraction"))||!pos(quantile(sBoot[g],.025))||(quantile(sBoot[g],.975)-quantile(sBoot[g],.025))/point.s[g]>number(policy,"maxRelativeWidth"))isfWhy<<"Korjausvasteen epävarmuus on liian suuri.";if(basalDays.size()<number(policy,"minimumBasalDays"))isfWhy<<"Vakaa perusvaikutus ei ole riittävästi varmennettu.";row["isfReasons"]=QJsonArray::fromStringList(isfWhy);
        row["isfDayInfluencePercent"]=sInfluence[g];row["currentRatio"]=currentRatio(profile,policy,g);row["startMinute"]=policy["groups"].toArray()[g].toObject()["startMinute"];row["endMinute"]=policy["groups"].toArray()[(g+1)%4].toObject()["startMinute"];
        if(!pos(early.q[g])||!pos(late.q[g]))why<<"Ateria-arvio puuttuu arviojakson toiselta puoliskolta.";
        row["isfPeriodDriftPercent"]=pos(early.s[g])&&pos(late.s[g])?QJsonValue(100*(late.s[g]/early.s[g]-1)):QJsonValue();
        if(pos(early.q[g])&&pos(late.q[g])&&std::abs(late.q[g]/early.q[g]-1)*100>number(policy,"maxPeriodDriftPercent"))why<<"HH-vaste muuttuu jakson sisällä; ei yhtä vakaata kerrointa.";
        if(invalidQ[g])why<<"Ateriahavainto tuottaa epäpositiivisen q:n; mallioletukset eivät riitä.";
        if(inconsistentS[g])why<<"Korjaushavainnot eivät noudata mallin laskuvasteoletusta.";
        if(!rEnough)why<<"Liian vähän kelpuutettuja aterioita tai erillisiä päiviä.";
        if(!correctionSupport&&!point.conditional[g])why<<"Annosajankohdan korjausvasteen aineisto ei riitä.";
        if(point.conditional[g])why<<"Ehdollinen ulkoinen korjauskerroin: koko epävarmuutta ei tunneta.";
        if(basalDays.size()<number(policy,"minimumBasalDays"))why<<"Perusvaikutuksen vakaita tarkistettuja päiviä puuttuu.";
        if(qBoot[g].size()<samples*(1-number(policy,"maxInvalidFraction")))why<<"Liikaa puuttuvia bootstrap-estimaatteja.";
        else if(pos(point.q[g])){double qlo=quantile(qBoot[g],.025),qhi=quantile(qBoot[g],.975);
    row["qLow"]=qlo;
    row["qHigh"]=qhi;
    row["ratioLow"]=1/qhi;
    row["ratioHigh"]=1/qlo;
    if((qhi-qlo)/point.q[g]>number(policy,"maxRelativeWidth"))why<<"Arvioalue on liian leveä.";}
        QJsonArray leftOutQ;
    double influence=0;
    bool unstable=false;
    for(const auto&day:days){auto others=days;others.removeAll(day);
    auto f=fit(byDay,others,policy);leftOutQ<<QJsonValue(pos(f.q[g])?f.q[g]:NAN);
    if(!pos(f.q[g])){if(pos(point.q[g]))unstable=true;continue;}
    if(pos(point.q[g]))influence=std::max(influence,std::abs(f.q[g]/point.q[g]-1)*100);}
        if(influence>number(policy,"maxDayInfluencePercent")||unstable)why<<"Yksi päivä vaikuttaa ratkaisevasti arvioon.";
    row["dayInfluencePercent"]=influence;
    row["leftOutQ"]=leftOutQ;
        // Independently compare weeks: opposite endpoint direction blocks adjustment.
        QList<double> first,last;
    const qint64 midpoint=half;QMap<QString,QList<double>> dailyDelta;
        for(auto o:obs)if(o.kind=="icr"&&o.group==g)dailyDelta[o.day]<<o.e;
        for(auto it=dailyDelta.cbegin();it!=dailyDelta.cend();++it){auto date=QDate::fromString(it.key(),Qt::ISODate);(QDateTime(date,QTime(0,0),z).toMSecsSinceEpoch()<midpoint?first:last)<<quantile(it.value());}
        const double deltaLimit=number(profile["rules"].toObject(),"mealDeviation");
        if(!first.isEmpty()&&!last.isEmpty()&&((quantile(first)>deltaLimit&&quantile(last)<-deltaLimit)||(quantile(first)<-deltaLimit&&quantile(last)>deltaLimit)))why<<"Jakson osien loppuvasteet ovat ristiriitaisia.";
        row["status"]=why.isEmpty()?"estimated":(point.conditional[g]&&rEnough?"conditional":"insufficient");
    row["reasons"]=QJsonArray::fromStringList(why);
        QJsonArray observations;
    for(auto o:obs)if(o.group==g)observations<<QJsonObject{{"id",o.id},{"day",o.day},{"kind",o.kind},{"delta",o.e}};
    row["observations"]=observations;
    row["sDraws"]=sDraws[g];
    row["qDraws"]=qDraws[g];
        groups<<row;
    }
    return {{"status","evaluated"},{"modelVersion","response-v2"},{"from",double(from)},{"to",double(to)},{"splitTime",double(half)},{"groups",groups},{"excluded",excluded},{"mealOutcomes",mealOutcomes},{"unsafe",anyHypo},{"basalDays",basalDays.size()},{"profileVersion",profile["version"]},{"medicationApproved",profile["approved"].toBool()},{"seed",double(seed)},{"bootstrapSamples",samples},{"policyHash",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(policy).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())}};
}
QJsonObject responseComparison(const QJsonObject&a,const QJsonObject&b,const QJsonObject&policy){
    QJsonArray groups;
    for(int i=0;i<4;++i){QJsonObject row{{"index",i},{"status","incomparable"}};
    auto ag=a["groups"].toArray(),bg=b["groups"].toArray();
    if(ag.size()!=4||bg.size()!=4){groups<<row;continue;}auto recent=ag[i].toObject(),old=bg[i].toObject();
        for(auto key:QStringList{"q","s"}){if(key=="q"?(recent["status"]!="estimated"||old["status"]!="estimated"):(recent["isfStatus"]!="estimated"||old["isfStatus"]!="estimated"))continue;
    auto x=recent[key+"Draws"].toArray(),y=old[key+"Draws"].toArray();
    QList<double> changes;
    for(int j=0;j<std::min(x.size(),y.size());++j)if(x[j].isDouble()&&y[j].isDouble()&&y[j].toDouble()>0)changes<<100*(x[j].toDouble()/y[j].toDouble()-1);
    if(changes.size()>=number(policy,"bootstrapSamples")*(1-number(policy,"maxInvalidFraction"))){QString prefix=key=="q"?"dosePerCarb":"isf";
    row[prefix+"ChangePercent"]=key=="q"?100*(number(recent,"q")/number(old,"q")-1):100*(number(recent,"isf")/number(old,"isf")-1);
    row[prefix+"ChangeLow"]=quantile(changes,.025);
    row[prefix+"ChangeHigh"]=quantile(changes,.975);
    row[prefix+"Changed"]=quantile(changes,.025)>0||quantile(changes,.975)<0;
    row["status"]="compared";}}
        groups<<row;}return {{"groups",groups},{"note","Muutosväli sisältää aineistovaihtelun, ei kaikkia mallivirheitä."}};
}
static double currentRatio(const QJsonObject&p,const QJsonObject&policy,int group){QList<double> values;
    auto periodSlots=p["slots"].toArray();
    for(int m=0;m<1440;++m){int g=3;
    for(int i=0;i<4;++i){if(policy["groups"].toArray()[i].toObject()["startMinute"].toInt()>m)break;g=i;}
    if(g!=group)continue;
    QJsonObject slot;
    for(auto v:periodSlots){if(v.toObject()["startMinute"].toInt()>m)break;slot=v.toObject();}double v=number(slot,"icr");
    if(!pos(v))return NAN;
    values<<v;}
    if(values.isEmpty())return NAN;
    return *std::min_element(values.begin(),values.end())==*std::max_element(values.begin(),values.end())?values[0]:NAN;}
QJsonObject responseProposal(const QJsonObject&p,const QJsonObject&policy,const QJsonObject&estimate,int group,qint64 now){
    auto errors=validateResponsePolicy(policy);errors<<validateHistoricalProfile(p);if(now>0&&QDateTime::fromMSecsSinceEpoch(qint64(number(estimate,"to")),QTimeZone(p["timezone"].toString().toUtf8())).date()!=QDateTime::fromMSecsSinceEpoch(now,QTimeZone(p["timezone"].toString().toUtf8())).date())errors<<"Hoitomuutosta varten valitse viimeisimmät kokonaiset päivät; historiallinen arvio ei ole nykyhoidon muutosperuste.";
    if(!policy["approved"].toBool()||!p["approved"].toBool())errors<<"Hyväksyntä puuttuu.";
    const auto hash=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(policy).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    if(hash!=estimate["policyHash"].toString())errors<<"Vastepolitiikka on muuttunut.";
    if(!errors.isEmpty())return blocked(errors);
    if(group<0||group>=4||estimate["groups"].toArray().size()!=4)return blocked({"Valitse vuorokaudenaika."});
    auto row=estimate["groups"].toArray()[group].toObject();
    QStringList why;
    if(number(estimate,"to")-number(p,"effectiveFrom")<number(p["rules"].toObject(),"minReviewDays")*86400000)why<<"Edellisestä profiilimuutoksesta on liian vähän seuranta-aikaa.";
    if(row["status"]!="estimated")why<<"R-arvion kelpoisuus tai epävarmuus ei riitä.";
    if(estimate["profileVersion"]!=p["version"])why<<"Hoitoprofiili on vaihtunut.";
    double current=currentRatio(p,policy,group),qlo=number(row,"qLow"),qhi=number(row,"qHigh"),step=number(policy,"doseStepPercent");
    if(!pos(current)||!pos(qlo)||!pos(qhi))why<<"Ryhmän nykyinen suhde puuttuu tai vaihtelee.";
    if(!why.isEmpty())return blocked(why);
    double q0=1/current,edge=q0<qlo?qlo:q0>qhi?qhi:q0;
    if(edge==q0)return blocked({"Nykyinen suhde on arvioalueella: ei muutostarvetta estimaatin perusteella."});
    for(auto v:row["leftOutQ"].toArray())if(!v.isDouble()||(v.toDouble()-q0)*(number(row,"q")-q0)<=0)return blocked({"Ehdotuksen suunta riippuu yhdestä päivästä."});
    if(edge>q0&&estimate["unsafe"].toBool())return blocked({"Ratkaisematon matala havainto estää insuliinin lisäämisen."});
    // Require a repeated out-of-target endpoint, not just a difference from baseline.
    int outside=0,meals=0;
    for(auto v:estimate["mealOutcomes"].toArray()){auto e=v.toObject();
    if(e["group"].toInt()!=group)continue;
    if(std::abs(number(e,"dose")-number(e,"carbs")/current)>number(policy,"doseMatchToleranceUnits"))return blocked({"Toteutunut ateria-annos poikkeaa nykyisestä hoitoprofiilista; selvitä toteutuminen ensin."});
    ++meals;
    if(edge>q0?number(e,"endGlucose")>number(p,"targetHigh"):number(e,"endGlucose")<number(p,"targetLow"))++outside;}
    if(!meals||double(outside)/meals<number(p["rules"].toObject(),"consensus"))return blocked({"Ei toistuvaa tavoitealueen ulkopuolista loppuvastetta."});
    const double qc=q0+std::copysign(std::min(q0*step/100,std::abs(edge-q0)),edge-q0),candidate=1/qc;
    const auto rules=p["rules"].toObject();
    if(std::abs(candidate/current-1)*100>number(rules,"maxChangePercent")||candidate<number(rules,"minimumIcr")||candidate>number(rules,"maximumIcr"))return blocked({"Ehdotus ylittää hyväksytyt HH-suhderajat."});
    return {{"status","proposal"},{"modelVersion","response-v2"},{"group",group},{"current",current},{"candidate",candidate},{"doseChangePercent",100*(qc/q0-1)},{"ratioChangePercent",100*(candidate/current-1)},{"profileVersion",p["version"]},{"policyHash",estimate["policyHash"]},{"researchOnly",true}};
}
QJsonObject applyResponseProposal(const QJsonObject&p,const QJsonObject&policy,const QJsonObject&proposal,qint64 now){
    if(proposal["status"]!="proposal"||proposal["profileVersion"]!=p["version"]||!validateResponsePolicy(policy).isEmpty()||!validateHistoricalProfile(p).isEmpty()||!pos(number(proposal,"candidate"))||proposal["group"].toInt(-1)<0||proposal["group"].toInt(-1)>3)return {};
    auto result=p;QSet<int> boundaries{0};
    for(auto v:p["slots"].toArray())boundaries.insert(v.toObject()["startMinute"].toInt());
    for(auto v:policy["groups"].toArray())boundaries.insert(v.toObject()["startMinute"].toInt());
    auto starts=boundaries.values();std::sort(starts.begin(),starts.end());
    QJsonArray updatedSlots;
    for(int minute:starts){QJsonObject s;
    for(auto v:p["slots"].toArray()){if(v.toObject()["startMinute"].toInt()>minute)break;s=v.toObject();}int g=3;
    for(int i=0;i<4;++i){if(policy["groups"].toArray()[i].toObject()["startMinute"].toInt()>minute)break;g=i;}s["startMinute"]=minute;s["name"]=policy["groups"].toArray()[g].toObject()["name"].toString()+QString(" @%1:%2").arg(minute/60,2,10,QChar('0')).arg(minute%60,2,10,QChar('0'));
    if(g==proposal["group"].toInt())s["icr"]=proposal["candidate"];
    updatedSlots<<s;}
    result["slots"]=updatedSlots;result["effectiveFrom"]=double(now);result["effectiveFromText"]=QDateTime::fromMSecsSinceEpoch(now,QTimeZone(p["timezone"].toString().toUtf8())).toString("yyyy-MM-dd HH:mm");result["acceptedResponseProposal"]=proposal;result["approvedAt"]=QDateTime::fromMSecsSinceEpoch(now,QTimeZone::UTC).toString(Qt::ISODate);result.remove("version");
    return result;
}
}
