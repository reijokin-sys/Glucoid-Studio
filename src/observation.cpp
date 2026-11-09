// SPDX-License-Identifier: GPL-3.0-or-later
#include "observation.h"
#include <QDateTime>
#include <QMap>
#include <algorithm>
#include <cmath>

namespace Studio {
static bool positive(double value) { return std::isfinite(value) && value > 0; }

QStringList validateObservationProfile(const QJsonObject &p, qint64 now) {
    QStringList errors;
    if (p["schema"].toInt() != 1 || !p["researchOnly"].toBool())
        errors << "Seurantaprofiilin rakenne ei kelpaa.";
    if (!p["historicalConfirmed"].toBool()) errors << "Vahvista hoitoasetusten voimassaolo.";
    if (!QTimeZone(p["timezone"].toString().toUtf8()).isValid()) errors << "Tarkista aikavyöhyke.";
    if (!positive(number(p, "effectiveFrom")) || number(p, "effectiveFrom") > (now > 0 ? now : QDateTime::currentMSecsSinceEpoch()))
        errors << "Tarkista hoitoasetusten alkuaika.";
    for (auto key : {"rapidMedicine", "basalMedicine"})
        if (p[key].toString().trimmed().isEmpty()) errors << "Määritä insuliinivalmiste: " + QString(key);
    if (!positive(number(p, "basalDose")) || !QTime::fromString(p["basalTime"].toString(), "HH:mm").isValid())
        errors << "Tarkista ilmoitettu perusannos ja pistosaika.";
    if (!positive(number(p, "targetLow")) || !positive(number(p, "targetHigh")) || number(p, "targetLow") >= number(p, "targetHigh"))
        errors << "Tarkista glukoosin tavoitealue.";
    const auto target = p["correctionTargetRange"].toObject();
    if (!target.isEmpty() && (!positive(number(target, "low")) || !positive(number(target, "high")) || number(target, "low") > number(target, "high")))
        errors << "Tarkista korjausten ilmoitettu tavoitealue.";
    int previous = -1;
    QSet<QString> names;
    const auto periods = p["slots"].toArray();
    if (periods.isEmpty()) errors << "Ilmoitetut HH-suhdejaksot puuttuvat.";
    for (auto value : periods) {
        auto slot = value.toObject();
        double start = number(slot, "startMinute");
        const auto name = slot["name"].toString().trimmed();
        if (!std::isfinite(start) || std::floor(start) != start || start <= previous || start >= 1440 || (previous == -1 && start != 0) || name.isEmpty() || names.contains(name) || !positive(number(slot, "icr")))
            errors << "Tarkista ilmoitetut HH-suhdejaksot.";
        if (!slot["isf"].isNull() && !slot["isf"].isUndefined() && !positive(number(slot, "isf")))
            errors << "Korjauskerroin on joko positiivinen tai tuntematon (tyhjä).";
        if (std::isfinite(start)) previous = int(start);
        names.insert(name);
    }
    errors.removeDuplicates();
    return errors;
}

QJsonObject observeProfile(const QJsonObject &p, const QList<Interval> &data,
                          const QJsonArray &amounts, QDate first, int days,
                          const QTimeZone &zone, const QSet<QString> &complete) {
    if (p.isEmpty()) return {{"status", "inactive"}, {"active", false}};
    const auto errors = validateObservationProfile(p);
    if (!errors.isEmpty()) return {{"status", "invalid"}, {"active", false}, {"reasons", QJsonArray::fromStringList(errors)}};
    QJsonObject result{{"active", true}, {"profileVersion", p["version"]}, {"medicationApproved", p["approved"]}, {"status", "observing"}};
    if (zone.id() != p["timezone"].toString().toUtf8()) {
        result["status"] = "timezone_mismatch";
        return result;
    }
    const auto from = QDateTime(first, QTime(0, 0), zone).toMSecsSinceEpoch();
    const auto to = QDateTime(first.addDays(days), QTime(0, 0), zone).toMSecsSinceEpoch();
    const auto effective = qint64(number(p, "effectiveFrom"));
    const auto start = std::max(from, effective);
    result["from"] = double(start);
    result["to"] = double(to);
    if (start >= to) { result["status"] = "before_profile_period"; return result; }
    QList<Interval> clipped;
    for (auto interval : data) {
        interval.from = std::max(interval.from, start);
        interval.to = std::min(interval.to, to);
        if (interval.to > interval.from) clipped << interval;
    }
    const auto statistics = analyse(clipped, first, days, zone, number(p, "targetLow"), number(p, "targetHigh"));
    QJsonObject stats;
    for (auto key : {"mean", "median", "sd", "cv", "inRange", "below", "above", "coveredSeconds", "low", "high"})
        if (statistics.contains(key)) stats[key] = statistics[key];
    stats["coverage"] = statistics["coveredSeconds"].toDouble() / ((to - start) / 1000.0);
    result["glucose"] = stats;
    if (clipped.isEmpty()) result["status"] = "waiting_for_glucose";
    QMap<QString, QJsonArray> byDay;
    for (auto value : amounts) {
        auto amount = value.toObject();
        auto time = qint64(amount["time"].toDouble());
        if (time >= start && time < to)
            byDay[QDateTime::fromMSecsSinceEpoch(time, zone).date().toString(Qt::ISODate)] << amount;
    }
    QJsonArray daily;
    int compared = 0, matching = 0;
    for (int i = 0; i < days; ++i) {
        auto date = first.addDays(i);
        auto dayStart = QDateTime(date, QTime(0, 0), zone).toMSecsSinceEpoch();
        if (QDateTime(date.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch() <= start) continue;
        auto dateText = date.toString(Qt::ISODate);
        bool unknown = false, medicineMatches = true;
        int basalCount = 0;
        double basal = 0;
        for (auto value : byDay[dateText]) {
            auto amount = value.toObject();
            auto kind = amount["kind"].toString();
            if (!std::isfinite(number(amount, "value")) || number(amount, "value") < 0 || amount["deleted"].toBool() || amount["analysisExcluded"].toBool() || kind.isEmpty() || kind == "unknown") { unknown = true; continue; }
            if (kind == "basal") {
                ++basalCount;
                basal += amount["value"].toDouble();
                medicineMatches &= amount["medicine"].toString() == p["basalMedicine"].toString();
            }
        }
        QString reason;
        if (dayStart < effective) reason = "partial_profile_day";
        else if (!complete.contains(dateText)) reason = "unconfirmed_day";
        else if (unknown) reason = "unclassified_or_excluded";
        else if (!medicineMatches) reason = "different_medicine";
        else if (basalCount != 1) reason = "single_basal_entry_required";
        QJsonObject row{{"date", dateText}, {"basalEntries", basalCount}, {"comparisonReady", reason.isEmpty()}, {"reason", reason}};
        row["basalRecorded"] = basalCount ? QJsonValue(basal) : QJsonValue();
        if (reason.isEmpty()) {
            row["basalDifference"] = basal - number(p, "basalDose");
            ++compared;
            if (std::abs(basal - number(p, "basalDose")) < 1e-6) ++matching;
        }
        daily << row;
    }
    result["daily"] = daily;
    result["basalComparedDays"] = compared;
    result["basalMatchingDays"] = matching;
    result["note"] = "Kirjattujen annosten vertailu ja glukoosiseuranta; ei arvio lääkkeen syyvaikutuksesta tai annosmuutosehdotus.";
    return result;
}
}
