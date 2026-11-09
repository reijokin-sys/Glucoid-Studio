// SPDX-License-Identifier: GPL-3.0-or-later
#include "glucose.h"
#include <QCryptographicHash>
#include <QSet>
#include <cmath>
namespace Studio {
static QString digest(const QByteArray &s) { return QString::fromLatin1(QCryptographicHash::hash(s,QCryptographicHash::Sha256).toHex()); }
static QStringList header(const QByteArray &data, QList<QByteArray> &lines) {
    if(data.isEmpty() || !data.endsWith('\n') || data.contains('\0')) return {};
    lines=data.split('\n');
    auto h=QString::fromUtf8(lines.takeFirst()).trimmed().split('\t');
    for(auto &x:h) x=x.trimmed().remove('#');
    return h;
}
ParsedReadings parseStream(const QByteArray &data,const QString &kind) {
    ParsedReadings out; QList<QByteArray> lines; const auto h=header(data,lines);
    const int ti=h.indexOf("UnixTime"),si=h.indexOf("Sensorid"),ni=h.indexOf("nr");
    const bool mmol=h.contains("mmol/L"); const int vi=h.indexOf(mmol?"mmol/L":"mg/dL");
    if(ti<0||si<0||ni<0||vi<0) { out.error="Tuntematon tai katkennut glukoosivienti."; return out; }
    QSet<QString> seen;
    for(const auto &line:lines) {
        if(line.trimmed().isEmpty()) continue;
        const auto cols=QString::fromUtf8(line).trimmed().split('\t');
        if(cols.size()!=h.size()) { out.error="Glukoosiviennissä on vajaa rivi."; break; }
        bool okT=false,okV=false; const double t=cols[ti].toDouble(&okT),v=cols[vi].toDouble(&okV);
        if(!okT || !okV || !std::isfinite(t) || !std::isfinite(v) || t<=0 || t>1e11) { out.error="Virheellinen mittaus tai aikaleima."; break; }
        if(v<=0) continue; // sensor error marker is never a normal reading
        Reading r; r.time=qRound64(t*1000); r.original=v; r.originalUnit=mmol?"mmol/L":"mg/dL";
        r.mmol=mmol?v:v/18.0; r.sensor=cols[si]; r.seriesKind=kind;r.originalTimezone=h.contains("TZ")?cols[h.indexOf("TZ")]:QString();r.key=digest(((kind=="stream"?QString():kind+":")+r.sensor+":"+cols[ni]).toUtf8());
        if(seen.contains(r.key)) { out.error="Glukoosiviennissä on päällekkäinen lähdetunnus."; break; }
        seen.insert(r.key);out.rows.append(r);
    }
    if(!out.error.isEmpty()) out.rows.clear();
    return out;
}
}
