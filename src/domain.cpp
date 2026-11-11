// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "domain.h"
#include <QCryptographicHash>
#include <cmath>
#include <limits>

namespace Studio {
double number(const QJsonObject& o, const QString& key) {
    return o.value(key).isDouble() ? o.value(key).toDouble() : std::numeric_limits<double>::quiet_NaN();
}
static QString digest(const QByteArray& s) {
    return QString::fromLatin1(QCryptographicHash::hash(s, QCryptographicHash::Sha256).toHex());
}
static QStringList header(const QByteArray& data, QList<QByteArray>& lines) {
    if (data.isEmpty() || !data.endsWith('\n') || data.contains('\0')) return {};
    lines = data.split('\n');
    auto h = QString::fromUtf8(lines.takeFirst()).trimmed().split('\t');
    for (auto& x : h) x = x.trimmed().remove('#');
    return h;
}
QString fingerprint(const Amount& a) {
    return digest((a.source + "|" + QString::number(a.time) + "|" + a.label + "|" + QString::number(a.value, 'g', 17)).toUtf8());
}
ParsedAmounts parseAmounts(const QByteArray& data) {
    ParsedAmounts out;
    QList<QByteArray> lines;
    const auto h = header(data, lines);
    const int si = h.indexOf("Source"), ni = h.indexOf("nr"), ti = h.indexOf("UnixTime"), vi = h.indexOf("Value"), li = h.indexOf("Label");
    if (si < 0 || ni < 0 || ti < 0 || vi < 0 || li < 0) {
        out.error = QCoreApplication::translate("Studio", "Tuntematon tai katkennut merkintävienti.");
        return out;
    }
    QHash<QString, int> occurrences;
    for (const auto& line : lines) {
        if (line.trimmed().isEmpty()) continue;
        const auto c = QString::fromUtf8(line).trimmed().split('\t');
        if (c.size() != h.size()) {
            out.error = QCoreApplication::translate("Studio", "Merkintäviennissä on vajaa rivi.");
            break;
        }
        bool ot = false, ov = false;
        const double t = c[ti].toDouble(&ot), v = c[vi].toDouble(&ov);
        if (!ot || !ov || !std::isfinite(t) || !std::isfinite(v) || t <= 0 || t > 1e11 || v < 0 || c[li].isEmpty()) {
            out.error = QCoreApplication::translate("Studio", "Virheellinen merkintä.");
            break;
        }
        Amount a;
        a.source = c[si];
        a.time = qRound64(t * 1000);
        a.value = v;
        a.label = c[li];
        a.raw = QString::fromUtf8(line);
        // Juggluco sequence numbers may shift after inserting an old record.
        // Match content AND multiplicity; identical actual doses stay separate.
        const QString fp = fingerprint(a);
        a.key = fp + ":" + QString::number(occurrences[fp]++);
        out.rows.append(a);
    }
    if (!out.error.isEmpty()) out.rows.clear();
    return out;
}

} // namespace Studio
