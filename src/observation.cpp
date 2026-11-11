// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "observation.h"
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace Studio {
static bool positive(double value) {
    return std::isfinite(value) && value > 0;
}

QStringList validateObservationProfile(const QJsonObject& profile, qint64 now) {
    QStringList errors;
    if (profile["schema"].toInt() != 1 || !profile["researchOnly"].toBool()) errors << QCoreApplication::translate("Studio", "Seurantaprofiilin rakenne ei kelpaa.");
    if (!profile["historicalConfirmed"].toBool()) errors << QCoreApplication::translate("Studio", "Vahvista hoitoasetusten voimassaolo.");
    if (!QTimeZone(profile["timezone"].toString().toUtf8()).isValid()) errors << QCoreApplication::translate("Studio", "Tarkista aikavyöhyke.");
    if (!positive(number(profile, "effectiveFrom")) || number(profile, "effectiveFrom") > (now > 0 ? now : QDateTime::currentMSecsSinceEpoch()))
        errors << QCoreApplication::translate("Studio", "Tarkista hoitoasetusten alkuaika.");
    for (auto key : {"rapidMedicine", "basalMedicine"})
        if (profile[key].toString().trimmed().isEmpty()) errors << QCoreApplication::translate("Studio", "Määritä insuliinivalmiste: ") + QString(key);
    if (!positive(number(profile, "basalDose")) || !QTime::fromString(profile["basalTime"].toString(), "HH:mm").isValid())
        errors << QCoreApplication::translate("Studio", "Tarkista ilmoitettu perusannos ja pistosaika.");
    if (!positive(number(profile, "targetLow")) || !positive(number(profile, "targetHigh")) || number(profile, "targetLow") >= number(profile, "targetHigh"))
        errors << QCoreApplication::translate("Studio", "Tarkista glukoosin tavoitealue.");
    const auto target = profile["correctionTargetRange"].toObject();
    if (!target.isEmpty() && (!positive(number(target, "low")) || !positive(number(target, "high")) || number(target, "low") > number(target, "high")))
        errors << QCoreApplication::translate("Studio", "Tarkista korjausten ilmoitettu tavoitealue.");
    int previous = -1;
    QSet<QString> names;
    const auto periods = profile["slots"].toArray();
    if (periods.isEmpty()) errors << QCoreApplication::translate("Studio", "Ilmoitetut HH-suhdejaksot puuttuvat.");
    for (auto value : periods) {
        auto slot = value.toObject();
        double start = number(slot, "startMinute");
        const auto name = slot["name"].toString().trimmed();
        if (!std::isfinite(start) || std::floor(start) != start || start <= previous || start >= 1440 || (previous == -1 && start != 0) || name.isEmpty() ||
            names.contains(name) || !positive(number(slot, "icr")))
            errors << QCoreApplication::translate("Studio", "Tarkista ilmoitetut HH-suhdejaksot.");
        if (!slot["isf"].isNull() && !slot["isf"].isUndefined() && !positive(number(slot, "isf")))
            errors << QCoreApplication::translate("Studio", "Korjauskerroin on joko positiivinen tai tuntematon (tyhjä).");
        if (std::isfinite(start)) previous = int(start);
        names.insert(name);
    }
    errors.removeDuplicates();
    return errors;
}

QJsonObject observeProfile(const QJsonObject& profile, const QList<Interval>& data, QDate first, int days, const QTimeZone& zone) {
    if (profile.isEmpty()) return {{"status", "inactive"}, {"active", false}};
    const auto errors = validateObservationProfile(profile);
    if (!errors.isEmpty()) return {{"status", "invalid"}, {"active", false}, {"reasons", QJsonArray::fromStringList(errors)}};
    QJsonObject result{{"active", true}, {"profileVersion", profile["version"]}, {"medicationApproved", profile["approved"]}, {"status", "observing"}};
    if (zone.id() != profile["timezone"].toString().toUtf8()) {
        result["status"] = "timezone_mismatch";
        return result;
    }
    const auto from = QDateTime(first, QTime(0, 0), zone).toMSecsSinceEpoch();
    const auto to = QDateTime(first.addDays(days), QTime(0, 0), zone).toMSecsSinceEpoch();
    const auto effective = qint64(number(profile, "effectiveFrom"));
    const auto start = std::max(from, effective);
    result["from"] = double(start);
    result["to"] = double(to);
    if (start >= to) {
        result["status"] = "before_profile_period";
        return result;
    }
    QList<Interval> clipped;
    for (auto interval : data) {
        interval.from = std::max(interval.from, start);
        interval.to = std::min(interval.to, to);
        if (interval.to > interval.from) clipped << interval;
    }
    const auto statistics = analyse(clipped, first, days, zone, number(profile, "targetLow"), number(profile, "targetHigh"));
    QJsonObject stats;
    for (auto key : {"mean", "median", "sd", "cv", "inRange", "below", "above", "coveredSeconds", "low", "high"})
        if (statistics.contains(key)) stats[key] = statistics[key];
    stats["coverage"] = statistics["coveredSeconds"].toDouble() / ((to - start) / 1000.0);
    result["glucose"] = stats;
    if (clipped.isEmpty()) result["status"] = "waiting_for_glucose";
    return result;
}
} // namespace Studio
