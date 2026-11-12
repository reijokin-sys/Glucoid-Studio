// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "domain.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
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
        a.sequence = c[ni].toLongLong();
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


bool applyJugglucoClasses(ParsedAmounts& amounts, const QByteArray& json, QString& error) {
    struct Classification { qint64 time; double value; QString kind, label; bool hasValue; };
    QHash<QString, Classification> classes;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    const auto object = document.object();
    auto fail = [&] {
        error = QCoreApplication::translate("Studio", "Jugglucon luokkatietoja ei voitu yhdistää merkintöihin. Päivitä tiedot uudelleen; aiempi historia säilyi.");
        return false;
    };
    if (parseError.error != QJsonParseError::NoError || object["status"].toInt() != 200 || !object["result"].isArray()) return fail();
    // Juggluco's mkidV3/mkididV3 encode source in the first group and
    // zero-based record position in the last group. The middle groups differ
    // between old/new IDs. Validate the whole shape, position AND timestamp.
    const QRegularExpression idPattern("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    const QRegularExpression paddedNumber("^([0-9]+)f*$");
    for (const auto& item : object["result"].toArray()) {
        if (!item.isObject()) return fail();
        const auto row = item.toObject();
        const QString id = row["identifier"].toString();
        const auto match = idPattern.match(id);
        const auto date = row["date"];
        if (row["app"].toString() != "Juggluco" || id.size() != 36 || !match.hasMatch() ||
            !date.isDouble() || !std::isfinite(date.toDouble()) || date.toDouble() <= 0 || date.toDouble() > 9e15 ||
            (row.contains("isValid") && !row["isValid"].isBool())) return fail();
        if (row["isValid"] == false) continue;
        const bool legacyId = id.mid(4, 4) == "ffff" && id.mid(9, 14) == "ffff-ffff-ffff";
        const auto sourceMatch = paddedNumber.match(id.left(legacyId ? 8 : 4));
        const auto positionMatch = paddedNumber.match(id.mid(24));
        if (!sourceMatch.hasMatch() || !positionMatch.hasMatch()) return fail();
        const auto source = sourceMatch.captured(1).toLongLong();
        const auto position = positionMatch.captured(1).toLongLong();
        const QString key = QString::number(source) + ":" + QString::number(position);
        if (classes.contains(key)) return fail();
        Classification c{qRound64(date.toDouble()), 0, "other", {}, false};
        const bool carbs = row.contains("carbs") && !row["carbs"].isNull();
        const bool insulin = row.contains("insulin") && !row["insulin"].isNull();
        if (carbs && insulin) return fail(); // Native Juggluco amounts have one class each.
        if (carbs || insulin) {
            const auto value = row[carbs ? "carbs" : "insulin"];
            if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0) return fail();
            const auto unit = row[carbs ? "carbsUnits" : "insulinUnits"].toString(carbs ? "grams" : "U").toLower();
            if ((carbs && unit != "g" && unit != "grams") || (insulin && unit != "u" && unit != "units")) return fail();
            c.value = value.toDouble();
            c.hasValue = true;
            if (carbs) c.kind = "carbs";
            else {
                c.kind = row["notes"].toString() == "Rapid-Acting" ? "rapid" :
                         row["notes"].toString() == "Long-Acting" ? "basal" : "unknown";
                c.label = row["insulinType"].toString();
            }
        }
        classes.insert(key, c);
    }
    for (auto& amount : amounts.rows) {
        bool sourceOk = false;
        const auto source = amount.source.toLongLong(&sourceOk);
        if (!sourceOk || source < 0 || amount.sequence <= 0) return fail();
        const QString key = QString::number(source) + ":" + QString::number(amount.sequence - 1);
        const auto found = classes.constFind(key);
        // Omitted records (e.g. zero, unconfigured, Don't send) are retained.
        // Absence from the v3 export never means "delete" or "guess from name".
        amount.kind = "unknown";
        if (found != classes.cend()) {
            const auto& c = found.value();
            if (c.time != amount.time || (!c.label.isEmpty() && c.label != amount.label)) return fail();
            // Insulin export has factor 1; carbs may be converted by Juggluco.
            if (!c.label.isEmpty() && std::abs(c.value - amount.value) > 1e-5 * std::max(1.0, std::abs(amount.value))) return fail();
            amount.kind = c.kind;
            if (c.hasValue) amount.value = c.value;
        }
        amount.source = "juggluco-v3";
        // key was computed from the original TSV before classification. Keep it
        // so annotations, exclusions and local deletions survive this upgrade.
    }
    return true;
}
} // namespace Studio
