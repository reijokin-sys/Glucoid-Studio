// SPDX-License-Identifier: GPL-3.0-or-later
#include "domain.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Studio {
double number(const QJsonObject &o,const QString &k) {
    return o.value(k).isDouble() ? o.value(k).toDouble() : std::numeric_limits<double>::quiet_NaN();
}
static bool positive(double n) { return std::isfinite(n) && n>0; }
static QString digest(const QByteArray &s) { return QString::fromLatin1(QCryptographicHash::hash(s,QCryptographicHash::Sha256).toHex()); }
static QStringList header(const QByteArray &data, QList<QByteArray> &lines) {
    if(data.isEmpty() || !data.endsWith('\n') || data.contains('\0')) return {};
    lines=data.split('\n');
    auto h=QString::fromUtf8(lines.takeFirst()).trimmed().split('\t');
    for(auto &x:h) x=x.trimmed().remove('#');
    return h;
}
QString fingerprint(const Amount &a) { return digest((a.source+"|"+QString::number(a.time)+"|"+a.label+"|"+QString::number(a.value,'g',17)).toUtf8()); }
ParsedAmounts parseAmounts(const QByteArray &data) {
    ParsedAmounts out; QList<QByteArray> lines;const auto h=header(data,lines);
    const int si=h.indexOf("Source"),ni=h.indexOf("nr"),ti=h.indexOf("UnixTime"),vi=h.indexOf("Value"),li=h.indexOf("Label");
    if(si<0||ni<0||ti<0||vi<0||li<0) { out.error="Tuntematon tai katkennut merkintävienti."; return out; }
    QHash<QString,int> occurrences;
    for(const auto &line:lines) {
        if(line.trimmed().isEmpty()) continue;
        const auto c=QString::fromUtf8(line).trimmed().split('\t');
        if(c.size()!=h.size()) { out.error="Merkintäviennissä on vajaa rivi."; break; }
        bool ot=false,ov=false;const double t=c[ti].toDouble(&ot),v=c[vi].toDouble(&ov);
        if(!ot||!ov||!std::isfinite(t)||!std::isfinite(v)||t<=0||t>1e11||v<0||c[li].isEmpty()) { out.error="Virheellinen merkintä."; break; }
        Amount a;a.source=c[si];a.time=qRound64(t*1000);a.value=v;a.label=c[li];a.raw=QString::fromUtf8(line);
        // Juggluco sequence numbers may shift after inserting an old record.
        // Match content AND multiplicity; identical actual doses stay separate.
        const QString fp=fingerprint(a);a.key=fp+":"+QString::number(occurrences[fp]++);out.rows.append(a);
    }
    if(!out.error.isEmpty()) out.rows.clear();
    return out;
}

QJsonObject emptyProfile() {
    QJsonObject p{{"schema",1},{"researchOnly",true},{"approved",false},{"reviewer",""},{"timezone","Europe/Helsinki"},
        {"rapidMedicine",""},{"basalMedicine",""},{"basalTime",""},{"rounding","down"}};
    for(const QString k:{"targetLow","targetHigh","target","hypo","maxGlucoseAgeMinutes","maxBolus","doseIncrement","basalDose","basalStableDays"})p[k]=QJsonValue();
    p["slots"]=QJsonArray{QJsonObject{{"name","Koko vuorokausi"},{"startMinute",0},{"icr",QJsonValue()},{"isf",QJsonValue()}}};
    p["actionModel"]=QJsonObject{{"name",""},{"reference",""},{"medicine",""},{"durationMinutes",QJsonValue()},{"curve",QJsonArray()}};
    QJsonObject rules;
    for(const QString k:{"minimumEpisodes","minimumDays","minCoverage","maxGapMinutes","consensus","stepPercent","maxChangePercent","minReviewDays","mealWindowMinutes","correctionWindowMinutes","basalWindowMinutes","mealDeviation","correctionDeviation","basalDeviation","minimumIcr","maximumIcr","minimumIsf","maximumIsf","minimumBasal","maximumBasal"})rules[k]=QJsonValue();
    p["rules"]=rules;return p;
}
static QStringList validateProfileImpl(const QJsonObject &p,bool historical,bool response=false) {
    QStringList e;
    const auto missing=[](const QJsonValue &v){return v.isNull()||v.isUndefined();};
    const QMap<QString,QString> fieldNames{{"targetLow","tavoitealueen alaraja (mmol/l)"},{"targetHigh","tavoitealueen yläraja (mmol/l)"},{"target","annoslaskurin tavoite (mmol/l)"},{"hypo","matalan glukoosin raja (mmol/l)"},{"maxGlucoseAgeMinutes","mittauksen enimmäisikä (min)"},{"maxBolus","enimmäisannos (U)"},{"doseIncrement","annosporras (U)"},{"basalDose","perusinsuliinin annos (U)"},{"basalStableDays","perusannoksen tasaantumisaika (vrk)"}};
    if(p["schema"].toInt()!=1 || !p["researchOnly"].toBool())e<<"Vain tutkimusprofiilin skeema 1 on tuettu.";
    for(const QString k:{"targetLow","targetHigh","target","hypo","maxGlucoseAgeMinutes","maxBolus","doseIncrement","basalDose","basalStableDays"})if(!(historical&&QStringList{"maxGlucoseAgeMinutes","maxBolus","doseIncrement"}.contains(k))&&!(response&&k=="target"&&missing(p[k]))&&!positive(number(p,k)))e<<"Puuttuva tai virheellinen arvo: "+fieldNames.value(k,k);
    if(!(number(p,"hypo")<=number(p,"targetLow") && number(p,"targetLow")<=number(p,"targetHigh"))||(!(response&&missing(p["target"]))&&!(number(p,"targetLow")<=number(p,"target")&&number(p,"target")<=number(p,"targetHigh"))))e<<"Tavoite- ja hypoglykemiarajojen järjestys ei kelpaa.";
    if(!historical&&number(p,"doseIncrement")>number(p,"maxBolus"))e<<"Annosporras ylittää enimmäisannoksen.";
    for(const QString k:{"rapidMedicine","basalMedicine","reviewer"})if(p[k].toString().trimmed().isEmpty())e<<"Puuttuva tieto: "+k;
    if(!QTime::fromString(p["basalTime"].toString(),"HH:mm").isValid())e<<"Perusinsuliinin annostusaika puuttuu.";
    if(!QTimeZone(p["timezone"].toString().toUtf8()).isValid())e<<"Tuntematon aikavyöhyke.";
    if(!historical&&p["rounding"]!="down" && p["rounding"]!="nearest")e<<"Tuntematon pyöristyssääntö.";
    const auto periods=p["slots"].toArray();int prev=-1;QSet<QString> names;
    if(periods.isEmpty())e<<"HH-suhde- ja korjauskerroinjaksot puuttuvat.";
    for(const auto &s:periods) {
        const auto o=s.toObject();const double start=number(o,"startMinute");const QString name=o["name"].toString();
        if(!std::isfinite(start)||start!=std::floor(start)||start<=prev||start>=1440||(prev==-1&&start!=0)||name.isEmpty()||names.contains(name)||!positive(number(o,"icr"))||(!(response&&missing(o["isf"]))&&!positive(number(o,"isf"))))e<<"Virheellinen hoitojakso: "+name;
        if(std::isfinite(start))prev=int(start);
        names.insert(name);
    }
    const auto a=p["actionModel"].toObject(); const auto curve=a["curve"].toArray();
    if(a["name"].toString().isEmpty()||a["reference"].toString().isEmpty()||a["medicine"]!=p["rapidMedicine"]||!positive(number(a,"durationMinutes"))||(!historical&&curve.size()<2))e<<"Insuliinin hyväksyttävä vaikutusmalli puuttuu.";
    double lastT=-1,lastF=1;
    for(int i=0;!historical&&i<curve.size();++i) {
        const auto pt=curve[i].toObject();const double t=number(pt,"minute"),f=number(pt,"fraction");
        if(!std::isfinite(t)||!std::isfinite(f)||t<=lastT||f<0||f>lastF||(i==0&&(t!=0||f!=1))||(i==curve.size()-1&&(f!=0||t!=number(a,"durationMinutes"))))e<<"Vaikutuskäyrän on vähennyttävä arvosta 1 arvoon 0 määritetyssä vaikutusajassa.";
        lastT=t;lastF=f;
    }
    const auto r=p["rules"].toObject();
    for(const QString &k:emptyProfile()["rules"].toObject().keys())if(!positive(number(r,k)))e<<"Puuttuva laskentasääntö: "+k;
    for(const QString k:{"minimumEpisodes","minimumDays"})if(number(r,k)!=std::floor(number(r,k)))e<<"Lukumäärän on oltava kokonaisluku: "+k;
    if(number(r,"consensus")<=0.5||number(r,"consensus")>1||number(r,"minCoverage")>1||number(r,"stepPercent")>number(r,"maxChangePercent")||number(r,"maxChangePercent")>=100)e<<"Laskentasääntöjen osuudet tai muutosrajat eivät kelpaa.";
    if(number(r,"mealWindowMinutes")<number(a,"durationMinutes")||number(r,"correctionWindowMinutes")<number(a,"durationMinutes"))e<<"Aterian ja korjauksen seurannan on katettava pikainsuliinin koko vaikutusaika.";
    for(const auto &name:QStringList{"Icr","Isf","Basal"})if(number(r,"minimum"+name)>=number(r,"maximum"+name))e<<"Virheelliset parametrirajat: "+name;
    for(const auto &s:periods) {auto o=s.toObject();if(number(o,"icr")<number(r,"minimumIcr")||number(o,"icr")>number(r,"maximumIcr")||number(o,"isf")<number(r,"minimumIsf")||number(o,"isf")>number(r,"maximumIsf"))e<<"Nykyinen hoitoasetus ylittää profiilin rajat.";}
    if(number(p,"basalDose")<number(r,"minimumBasal")||number(p,"basalDose")>number(r,"maximumBasal"))e<<"Nykyinen perusinsuliini ylittää profiilin rajat.";
    e.removeDuplicates();return e;
}
QStringList validateProfile(const QJsonObject &p){return validateProfileImpl(p,false);}
QStringList validateHistoricalProfile(const QJsonObject &p){return validateProfileImpl(p,true);}
// Estimation uses observed glucose change and dose, not a prescribed ISF or point target.
// All remaining profile requirements stay identical to historical evaluation.
QStringList validateResponseProfile(const QJsonObject &p){return validateProfileImpl(p,true,true);}
QString slotAt(const QJsonObject &p,qint64 time) {
    auto dt=QDateTime::fromMSecsSinceEpoch(time,QTimeZone(p["timezone"].toString().toUtf8()));int min=dt.time().hour()*60+dt.time().minute();QString s;
    for(const auto &v:p["slots"].toArray())if(v.toObject()["startMinute"].toInt()<=min)s=v.toObject()["name"].toString();
    return s;
}
QJsonObject slotProfile(const QJsonObject &p,const QString &slot) {for(const auto &s:p["slots"].toArray())if(s.toObject()["name"]==slot)return s.toObject();return {};}
QJsonObject metrics(QList<Reading> rs,qint64 from,qint64 to,double low,double high,int gap) {
    std::sort(rs.begin(),rs.end(),[](const auto&a,const auto&b){return a.time<b.time;});
    double dur=0,sum=0,sq=0,below=0,above=0; qint64 coveredTo=from; int n=0;
    for(int i=0;i<rs.size();++i) {
        const auto &r=rs[i];if(!r.reliable||!positive(r.mmol))continue;
        const qint64 next=i+1<rs.size()?rs[i+1].time:to;
        const qint64 end=std::min({next,to,r.time+qint64(gap)*1000});const qint64 begin=std::max({r.time,from,coveredTo});
        if(end<=begin)continue;
        const double d=(end-begin)/1000.0;dur+=d;sum+=r.mmol*d;sq+=r.mmol*r.mmol*d;++n;coveredTo=end;
        if(positive(low)&&r.mmol<low)below+=d;
        if(positive(high)&&r.mmol>high)above+=d;
    }
    const bool targets=positive(low)&&positive(high)&&low<high;
    QJsonObject out{{"coverage",to>from?dur/((to-from)/1000.0):0},{"coveredSeconds",dur},{"samples",n},{"targetsSet",targets}};
    if(dur>0) {const double mean=sum/dur;out["mean"]=mean;out["cv"]=std::sqrt(std::max(0.0,sq/dur-mean*mean))/mean*100;
        if(targets){out["below"]=below/dur;out["above"]=above/dur;out["inRange"]=(dur-below-above)/dur;}}
    return out;
}
double remainingFraction(const QJsonArray &c,double minute) {
    if(minute<0)return std::numeric_limits<double>::quiet_NaN();
    for(int i=1;i<c.size();++i){const auto a=c[i-1].toObject(),b=c[i].toObject();const double at=number(a,"minute"),bt=number(b,"minute");if(minute<=bt)return number(a,"fraction")+(number(b,"fraction")-number(a,"fraction"))*(minute-at)/(bt-at);}
    return 0;
}
static QJsonObject blocked(const QStringList &why) {return {{"status","blocked"},{"researchOnly",true},{"reasons",QJsonArray::fromStringList(why)}};}
QJsonObject calculateBolus(const QJsonObject &p,const QJsonObject &in,const QJsonArray &doses,qint64 now) {
    auto errors=validateProfile(p);if(!p["approved"].toBool())errors<<"Hoitoprofiilia ei ole hyväksytty tutkimuslaskentaan.";
    for(const QString k:{"historyComplete","carbsComplete","contextReviewed"})if(!in[k].toBool())errors<<"Vahvista tietojen kattavuus ja tilanteen soveltuvuus: "+k;
    if(!in["reliable"].toBool())errors<<"Glukoosimittauksen luotettavuutta ei ole vahvistettu.";
    const double bg=number(in,"glucose"),carbs=number(in,"carbs"),stamp=number(in,"glucoseTime");
    if(!positive(bg)||!std::isfinite(carbs)||carbs<0||!positive(stamp))errors<<"Virheellinen glukoosi-, aika- tai HH-tieto.";
    if(bg<=number(p,"hypo"))errors<<"Matala glukoosi estää annosehdotuksen.";
    if(stamp>now+60000||now-stamp>number(p,"maxGlucoseAgeMinutes")*60000)errors<<"Mittaus on liian vanha tai tulevaisuudessa.";
    const auto s=slotProfile(p,slotAt(p,now));if(s.isEmpty())errors<<"Tämän ajankohdan hoitoasetusta ei ole.";
    const auto action=p["actionModel"].toObject();double iob=0;
    for(const auto &v:doses) {
        const auto d=v.toObject();const double time=number(d,"time"),units=number(d,"value");
        if(!positive(time)||!std::isfinite(units)||units<0){errors<<"Virheellinen insuliinimerkintä.";continue;}
        if(time<now-number(action,"durationMinutes")*60000)continue;
        if(time>now){errors<<"Annosmerkintä on tulevaisuudessa.";continue;}
        if(d["kind"]=="unknown"||d["stale"].toBool())errors<<"Annosikkunassa on luokittelematon tai muuttunut merkintä.";
        if(d["kind"]!="rapid")continue;
        if(d["medicine"]!=p["rapidMedicine"]){errors<<"Pikainsuliini ei vastaa vaikutusmallia.";continue;}
        iob+=units*remainingFraction(action["curve"].toArray(),(now-time)/60000.0);
    }
    if(!errors.isEmpty())return blocked(errors);
    const double meal=carbs/number(s,"icr"),corr=(bg-number(p,"target"))/number(s,"isf"),raw=meal+corr-iob;
    if(!std::isfinite(raw))return blocked({"Laskentatulos ei ole äärellinen."});
    if(raw>number(p,"maxBolus"))return blocked({"Laskelma ylittää profiilin enimmäisannoksen."});
    const double step=number(p,"doseIncrement"),positiveRaw=std::max(0.0,raw);
    const double units=(p["rounding"]=="nearest"?std::round(positiveRaw/step):std::floor(positiveRaw/step+1e-10))*step;
    if(units>number(p,"maxBolus"))return blocked({"Pyöristetty annos ylittää enimmäisannoksen."});
    return {{"status","research"},{"researchOnly",true},{"profileVersion",p["version"]},{"meal",meal},{"correction",corr},{"iob",iob},{"unrounded",raw},{"units",units},{"rounding",p["rounding"]},{"time",double(now)},{"validUntil",stamp+number(p,"maxGlucoseAgeMinutes")*60000},{"message",raw<=0?"Ei positiivista annosehdotusta.":"Tutkimuslaskelma; ei toteutunut pistos."}};
}
QJsonObject recommend(const QJsonObject &p,const QString &kind,const QString &slot,const QJsonArray &episodes,qint64 now) {
    auto errors=p["historical"].toBool()?validateHistoricalProfile(p):validateProfile(p);if(!p["approved"].toBool())errors<<"Hoitoprofiilia ei ole hyväksytty tutkimuslaskentaan.";
    if(kind!="icr"&&kind!="isf"&&kind!="basal")errors<<"Tuntematon arviointikohde.";
    const auto r=p["rules"].toObject(),s=slotProfile(p,slot);if(kind!="basal"&&s.isEmpty())errors<<"Tuntematon hoitojakso.";
    const double since=number(p,"effectiveFrom");
    if(!positive(since)||now-since<number(r,"minReviewDays")*86400000)errors<<"Profiilin muutoksesta ei ole riittävästi seuranta-aikaa.";
    if(!errors.isEmpty())return blocked(errors);
    QJsonArray included,excluded;int up=0,down=0;QSet<QString> days;
    const QString thresholdKey=kind=="icr"?"mealDeviation":kind=="isf"?"correctionDeviation":"basalDeviation";
    for(const auto &v:episodes) {
        const auto e=v.toObject();if(e["methodVersion"].toInt()==2)continue;if(e["kind"]!=kind||(kind!="basal"&&e["slot"]!=slot))continue;
        QStringList reasons;const double start=number(e,"start"),end=number(e,"end");
        if(!e["reviewed"].toBool()||!e["complete"].toBool()||!e["contextClear"].toBool())reasons<<"Tarkistus tai kattavuuden vahvistus puuttuu.";
        if(e["stale"].toBool())reasons<<"Lähdetiedot ovat muuttuneet.";
        if(e["profileVersion"]!=p["version"]||start<since)reasons<<"Jakso kuuluu eri hoitoprofiiliin.";
        if(end>now||end<=start||!positive(number(e,"coverage"))||number(e,"coverage")<number(r,"minCoverage"))reasons<<"Seuranta ei kata kelpuutettua jaksoa.";
        if(kind=="basal"&&!e["basalStable"].toBool())reasons<<"Perusinsuliinin vakaa annostus ei ole vahvistettu.";
        const double deviation=number(e,"deviation");if(!std::isfinite(deviation))reasons<<"Poikkeamaa ei voitu laskea.";
        if(!reasons.isEmpty()){excluded.append(QJsonObject{{"id",e["id"]},{"reasons",QJsonArray::fromStringList(reasons)}});continue;}
        included.append(e);days.insert(QDateTime::fromMSecsSinceEpoch(qint64(start),QTimeZone(p["timezone"].toString().toUtf8())).date().toString(Qt::ISODate));
        if(deviation>number(r,thresholdKey))++up;
        if(deviation<-number(r,thresholdKey))++down;
        if(e["hypoObserved"].toBool()&&deviation>=0)++down; // conflict blocks any increase
    }
    QJsonObject out{{"status","blocked"},{"researchOnly",true},{"kind",kind},{"slot",slot},{"profileVersion",p["version"]},{"included",included},{"excluded",excluded}};
    if(included.size()<number(r,"minimumEpisodes")||days.size()<number(r,"minimumDays"))errors<<"Ei riittävästi kelpuutettuja jaksoja ja erillisiä päiviä.";
    if(up&&down)errors<<"Ristiriitaiset havainnot: sekä nousuja että laskuja tai hypoglykemiaa.";
    int direction=0;const double count=included.size();if(count>0&&up/count>=number(r,"consensus"))direction=1;if(count>0&&down/count>=number(r,"consensus"))direction=-1;
    if(!direction)errors<<"Ei yhtenäistä, profiilin rajat ylittävää poikkeamaa.";
    if(!errors.isEmpty()){out["reasons"]=QJsonArray::fromStringList(errors);return out;}
    const double current=kind=="basal"?number(p,"basalDose"):number(s,kind);const double sign=kind=="basal"?direction:-direction;
    const double candidate=current*(1+sign*number(r,"stepPercent")/100.0);const QString k=kind=="icr"?"Icr":kind=="isf"?"Isf":"Basal";
    if(candidate<number(r,"minimum"+k)||candidate>number(r,"maximum"+k))errors<<"Ehdotus ylittää profiilin parametrirajat.";
    if(!errors.isEmpty()){out["reasons"]=QJsonArray::fromStringList(errors);return out;}
    out["status"]="research";out["current"]=current;out["candidate"]=candidate;out["changePercent"]=sign*number(r,"stepPercent");
    out["rule"]="Toistuva kelpuutettu poikkeama → hyväksytyn profiilin yksi muutosaskel. Ei kausaalinen estimaatti.";out["time"]=double(now);return out;
}
}
