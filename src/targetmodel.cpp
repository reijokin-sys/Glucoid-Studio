// SPDX-License-Identifier: GPL-3.0-or-later
#include "targetmodel.h"
#include "domain.h"
#include "automatic.h"
#include <QMap>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace Studio {
namespace {
// Intercept, carbs, meal insulin, baseline, premeal trend, timing, correction insulin.
constexpr int P=7;
using Vector=std::array<double,P>;
struct Row { Vector x; double y; QString day; double start=0, carbs=0; };
struct Fit { bool ok=false; Vector b{}; };
double predict(const Fit &f,const Vector &x) { double v=0;for(int j=0;j<P;++j)v+=f.b[j]*x[j];return v; }
double quantile(std::vector<double> v,double q) { if(v.empty())return NAN;std::sort(v.begin(),v.end());double p=(v.size()-1)*q;size_t i=size_t(p);return v[i]+(v[std::min(i+1,v.size()-1)]-v[i])*(p-i); }
// Centered, scaled, twice-reorthogonalized QR. No ridge prior can manufacture
// an insulin effect when recorded doses merely repeat a fixed carbohydrate ratio.
Fit fit(const std::vector<Row> &rows,const std::vector<int> &columns) {
    Fit out;if(rows.size()<=columns.size())return out;
    QMap<QString,int> counts;for(const auto &r:rows)++counts[r.day];
    Vector center{},scale{};scale[0]=1;
    std::vector<double> weights;double total=0;
    for(const auto &r:rows){double w=1.0/counts[r.day];weights.push_back(w);total+=w;for(int j=1;j<P;++j)center[j]+=w*r.x[j];}
    for(int j=1;j<P;++j)center[j]/=total;
    for(size_t i=0;i<rows.size();++i)for(int j=1;j<P;++j)scale[j]+=weights[i]*std::pow(rows[i].x[j]-center[j],2)/total;
    for(int j=1;j<P;++j)scale[j]=std::sqrt(scale[j]);
    std::vector<std::vector<double>> q;double R[P][P]{};double qty[P]{};
    for(size_t c=0;c<columns.size();++c){int j=columns[c];if(scale[j]<1e-8)return out;
        std::vector<double> v;for(size_t i=0;i<rows.size();++i)v.push_back(std::sqrt(weights[i])*(rows[i].x[j]-center[j])/scale[j]);
        for(int pass=0;pass<2;++pass)for(size_t k=0;k<c;++k){double dot=0;for(size_t i=0;i<v.size();++i)dot+=q[k][i]*v[i];R[k][c]+=dot;for(size_t i=0;i<v.size();++i)v[i]-=dot*q[k][i];}
        double norm=0;for(double v0:v)norm+=v0*v0;norm=std::sqrt(norm);
        // Residual variation below 5% is too ill-conditioned for inverse prediction.
        if(norm/std::sqrt(total)<.05)return out;
        R[c][c]=norm;for(size_t i=0;i<v.size();++i){v[i]/=norm;qty[c]+=v[i]*rows[i].y*std::sqrt(weights[i]);}q.push_back(v);
    }
    double beta[P]{};for(int c=int(columns.size())-1;c>=0;--c){double v=qty[c];for(size_t k=c+1;k<columns.size();++k)v-=R[c][k]*beta[k];beta[c]=v/R[c][c];}
    for(size_t c=0;c<columns.size();++c){int j=columns[c];out.b[j]=beta[c]/scale[j];}
    for(int j=1;j<P;++j)out.b[0]-=out.b[j]*center[j];
    out.ok=true;return out;
}
}
QJsonObject theoreticalMealDose(const QJsonArray &meals,int group,const QString &cohort,double target,bool totalWindow,int minimumMeals,int minimumDays,bool sugarLoad) {
    QJsonObject out{{"method","observational-return-v1"},{"status","insufficient_data"},{"horizonMinutes",240},
        {"endpoint","return-to-start"},
        {"doseBasis",totalWindow?"window-total":"meal-bolus"},{"scope",group>=0?"clock-group":"all-day"},
        {"prescriptive",false},{"causalEffectIdentified",false},{"clinicallyValidated",false},
        {"meanPer10g",QJsonValue()},{"units",QJsonValue()}, {"minimumMeals",minimumMeals},{"minimumDays",minimumDays},
        {"correctionStatus","insufficient_data"},{"correctionPerMmol",QJsonValue()},{"correctionUnits",QJsonValue()}};
    auto stop=[&](const char *status,const QString &message){out["status"]=status;out["message"]=message;return out;};
    if(!std::isfinite(target)||target<=0)return stop("invalid_target","Teoreettinen tavoite puuttuu.");
    const QString eligibleKey=totalWindow?"windowEligible":"modelEligible";
    const QString exclusionsKey=totalWindow?"windowExclusions":"modelExclusions";
    const QString unitsKey=totalWindow?"windowUnits":"units";
    const QString timingKey=totalWindow?"windowBolusOffsetMinutes":"bolusOffsetMinutes";
    std::vector<Row> rows;QSet<QString> days;int total=0,lows=0;QMap<QString,int> exclusions;
    for(auto v:meals){auto m=v.toObject();if((group>=0&&m["group"].toInt()!=group)||m["cohort"].toString()!=cohort)continue;++total;
        if(m["lowObserved"].toBool())++lows;
        if(!m[eligibleKey].toBool()){for(auto why:m[exclusionsKey].toArray())++exclusions[why.toString()];continue;}
        // Endpoint value minus the starting glucose: a rise in between is allowed, the meal
        // fails only if the glucose is still above the starting level at the 4 h endpoint.
        const double outcome=number(m,"endpointGlucose")-number(m,"startGlucose");
        const double correctionUnits=m.contains("windowCorrectionUnits")?number(m,"windowCorrectionUnits"):0.0;
        const double carbs=number(m,"carbs"),start=number(m,"startGlucose");
        // One sugar pool: blood glucose above the target counts as extra carbohydrate and
        // glucose below the target counts as a deficit. The exchange rate is fixed, because
        // a free rate would only re-parameterise the separate carb and baseline terms.
        const double load=sugarLoad?carbs+sugarLoadGramsPerMmol*(start-target):carbs;
        Row r{{1,load,number(m,unitsKey),sugarLoad?0.0:start,number(m,"premealTrend"),number(m,timingKey),correctionUnits},outcome,m["day"].toString(),start,carbs};
        bool valid=std::isfinite(r.y)&&!r.day.isEmpty()&&(sugarLoad?std::abs(r.x[1])>1e-9:r.x[1]>0)&&r.x[2]>0;for(auto x:r.x)valid&=std::isfinite(x);if(!valid){++exclusions["invalid_values"];continue;}
        rows.push_back(r);days.insert(r.day);
    }
    std::sort(rows.begin(),rows.end(),[](const Row&a,const Row&b){return a.day==b.day?a.x<b.x:a.day<b.day;});
    QJsonObject excluded;QJsonArray details;for(auto it=exclusions.cbegin();it!=exclusions.cend();++it){excluded[it.key()]=it.value();details<<QJsonObject{{"code",it.key()},{"count",it.value()},{"text",automaticIssueText(it.key())}};}
    out["exclusionDetails"]=details;
    out["meals"]=int(rows.size());out["days"]=days.size();out["excludedMeals"]=total-int(rows.size());out["exclusions"]=excluded;out["observedLowMeals"]=lows;
    if(int(rows.size())<minimumMeals||int(days.size())<minimumDays)return stop("insufficient_data",QString("Malliin sopivia aterioita %1 / %2 ja päiviä %3 / %4. Aineisto ei riitä tämän mallin sovittamiseen.").arg(rows.size()).arg(minimumMeals).arg(days.size()).arg(minimumDays));
    Vector reference{},minimum{},maximum{};minimum.fill(INFINITY);maximum.fill(-INFINITY);QMap<QString,int> dayCounts;for(const auto &r:rows)++dayCounts[r.day];
    double startSum=0;for(const auto &r:rows)startSum+=r.start/dayCounts[r.day]/days.size();
    for(const auto &r:rows)for(int j=0;j<P;++j){reference[j]+=r.x[j]/dayCounts[r.day]/days.size();minimum[j]=std::min(minimum[j],r.x[j]);maximum[j]=std::max(maximum[j],r.x[j]);}
    std::vector<int> columns{0,1,2};for(int j=3;j<P;++j)if(maximum[j]-minimum[j]>(j==5?1:.05))columns.push_back(j);
    auto full=fit(rows,columns);
    if(!full.ok)return stop("unidentifiable","HH-määrä ja annos vaihtelevat liian samansuuntaisesti: niiden erillisiä vaikutuksia ei voi ratkaista.");
    if(full.b[1]<=0||full.b[2]>=0)return stop("implausible_effect","Aineisto ei osoita johdonmukaista glukoosia laskevaa insuliinivaikutusta. Annosarviota ei muodosteta.");
    const double sensitivity=-full.b[2];
    // Reference is the observed, day-balanced meal, not an extrapolated fasting state.
    reference[2]=0;reference[6]=0;
    // Carbohydrate component: the starting glucose is held at the theoretical target, so the
    // modelled carbohydrate rise is zero. A correction component carries a higher start back.
    Vector held=reference;if(!sugarLoad)held[3]=target;
    // The required change at a start equal to the target is zero.
    const double required=0.0;
    const double withoutDoseFlat=predict(full,held),dose=(withoutDoseFlat-required)/sensitivity;
    out["referenceCarbs"]=reference[1];out["referenceStartGlucose"]=startSum;out["referenceTrend"]=reference[4];out["referenceBolusOffsetMinutes"]=reference[5];out["targetValue"]=target;out["startGlucoseHeldAt"]=target;
    // Last third of whole days is never used to fit the validation model.
    auto dayList=days.values();dayList.sort();QString split=dayList[dayList.size()*2/3];std::vector<Row> train,test;
    for(const auto&r:rows)(r.day<split?train:test).push_back(r);
    auto early=fit(train,columns);double error=0;
    if(!early.ok||early.b[2]>=0||early.b[1]<=0)return stop("unstable","Aiemmista päivistä ei synny vakaata mallia myöhempien aterioiden arviointiin.");
    QMap<QString,int> testDays;for(auto r:test)++testDays[r.day];for(auto r:test)error+=std::pow(predict(early,r.x)-r.y,2)/testDays[r.day]/testDays.size();error=std::sqrt(error);
    out["heldOutRmse"]=error;out["heldOutMeals"]=int(test.size());out["heldOutDays"]=testDays.size();
    if(error>2)return stop("poor_prediction","Aikaisemmilla päivillä sovitettu malli ennustaa myöhempiä aterioita liian epätarkasti (virhe yli 2 mmol/l).");
    std::mt19937 random(20261029);std::uniform_int_distribution<int> pick(0,dayList.size()-1);std::vector<double> doses,effects,corrections,responses;
    for(int b=0;b<200;++b){std::vector<Row> sample;for(int d=0;d<dayList.size();++d){const auto chosen=dayList[pick(random)];for(auto r:rows)if(r.day==chosen){r.day=QString::number(d);sample.push_back(r);}}
        auto f=fit(sample,columns);if(!f.ok||f.b[1]<=0||f.b[2]>=0)continue;double u=(predict(f,held)-required)/(-f.b[2]);if(!std::isfinite(u))continue;doses.push_back(u);effects.push_back(-f.b[2]);
        const double carry=f.b[3]+1.0;if(carry>0)corrections.push_back(carry/(-f.b[2]));
        if(sugarLoad)responses.push_back(10*f.b[1]/(-f.b[2]));}
    out["bootstrapValid"]=int(doses.size());out["bootstrapReplicates"]=200;
    if(doses.size()<180)return stop("unstable","Päivien uudelleenotanta ei anna vakaata annosvastetta. Lisää vertailukelpoisia havaintoja tarvitaan.");
    double lower=quantile(doses,.05),upper=quantile(doses,.95);
    if(dose<=0||lower<=0||upper-lower>dose)return stop("uncertain","Teoreettisen annoksen epävarmuus on liian suuri numeeriselle arviolle.");
    // Refuse inverse extrapolation outside doses AND carbohydrate-normalized ratios.
    double minRatio=INFINITY,maxRatio=0;for(const auto&r:rows){minRatio=std::min(minRatio,r.x[2]/r.x[1]);maxRatio=std::max(maxRatio,r.x[2]/r.x[1]);}
    if(lower<minimum[2]||upper>maximum[2]||lower/reference[1]<minRatio||upper/reference[1]>maxRatio)
        return stop("outside_observations","Tavoiteannos tai sen epävarmuus ulottuu havaittujen annosten ulkopuolelle. Malli ei ekstrapoloi annosta.");
    out["status"]="estimated";
    const QString basis=totalWindow?QStringLiteral("pikainsuliinia yhteensä 2 h ikkunassa (ateria + korjaukset)"):QStringLiteral("aterian oma annos");
    out["message"]=QString("Kokeellinen havaintomalli: %1, jolla 4 h kuluttua glukoosi on palannut lähtötasolle (lähtöglukoosi %2 mmol/l); ei pistosohje.").arg(basis).arg(target);
    out["units"]=dose;
    if(sugarLoad){
        // One response: units per 10 g of the combined sugar load. The correction share is
        // implied by the same exchange rate and is not a separate fitted coefficient.
        const double response=10*full.b[1]/sensitivity;
        out["meanPer10g"]=response;out["metric"]="U/10g-sugar-load";out["sugarGramsPerMmol"]=sugarLoadGramsPerMmol;
        out["uncertaintyLow"]=responses.empty()?QJsonValue():QJsonValue(quantile(responses,.05));
        out["uncertaintyHigh"]=responses.empty()?QJsonValue():QJsonValue(quantile(responses,.95));
        const double perMmol=sugarLoadGramsPerMmol*response/10.0;
        out["correctionStatus"]="estimated";out["correctionPerMmol"]=perMmol;
        out["correctionUnits"]=perMmol*(startSum-target);
        out["startGlucoseHeldAt"]=target;
    }else{
        out["meanPer10g"]=10*dose/reference[1];out["uncertaintyLow"]=10*lower/reference[1];out["uncertaintyHigh"]=10*upper/reference[1];
        // Technical plausibility floor (not a clinical limit): a start-glucose carry-over of at
        // least 0.05 mmol/l per mmol/l must be fitted before a correction share is shown.
        const double carry=full.b[3]+1.0;
        if(carry>=0.05&&corrections.size()>=180&&quantile(corrections,.05)>0){
            const double perMmol=carry/sensitivity;
            out["correctionStatus"]="estimated";out["correctionPerMmol"]=perMmol;
            out["correctionUnits"]=perMmol*(reference[3]-target);
            out["correctionUncertaintyLow"]=quantile(corrections,.05);out["correctionUncertaintyHigh"]=quantile(corrections,.95);
        }else out["correctionStatus"]="unidentifiable";
    }
    out["endpointEffectPerUnit"]=sensitivity;out["effectLow"]=quantile(effects,.05);out["effectHigh"]=quantile(effects,.95);
    out["correctionEffectPerUnit"]=std::find(columns.cbegin(),columns.cend(),6)!=columns.cend()?QJsonValue(-full.b[6]):QJsonValue();
    out["uncertaintyMeaning"]="90% day-cluster bootstrap of model mean; not an individual prediction or causal interval";
    return out;
}
}
