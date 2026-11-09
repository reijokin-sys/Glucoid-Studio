// SPDX-License-Identifier: GPL-3.0-or-later
#include "daychart.h"
#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace Studio {
QJsonObject currentDayChartData(const QList<Reading>& stream, const QList<Reading>& history, QDate day, const QTimeZone& zone, double low, double high) {
    if (!day.isValid() || !zone.isValid()) return {};
    const qint64 from = day.startOfDay(zone).toMSecsSinceEpoch();
    const qint64 to = day.addDays(1).startOfDay(zone).toMSecsSinceEpoch();
    auto window = [from, to](const QList<Reading>& rows) {
        QList<Reading> selected;
        for (const auto& row : rows)
            if (row.time >= from - summaryPreRollMs && row.time < to) selected.append(row);
        return selected;
    };
    const auto data = selectSeries(window(stream), window(history), from, to, true, summaryMaxGapSeconds);
    QJsonArray segments, segment;
    qint64 previous = 0;
    QString sensor;
    double maximum = 15;
    for (const auto& interval : data) {
        if (previous && (interval.from - previous > summaryPreRollMs || interval.sensor != sensor)) {
            if (!segment.isEmpty()) segments.append(segment);
            segment = {};
        }
        segment.append(QJsonObject{
            {"x", double(interval.from)}, {"y", interval.value}, {"timeText", QDateTime::fromMSecsSinceEpoch(interval.from, zone).toString("HH:mm")}});
        maximum = std::max(maximum, interval.value + 2);
        previous = interval.from;
        sensor = interval.sensor;
    }
    if (!segment.isEmpty()) segments.append(segment);

    // Walk real minutes: a missing spring hour has no tick; the repeated autumn
    // hour has two distinct timestamps. A wall-clock 24 h interpolation cannot
    // represent either transition correctly.
    QJsonArray ticks;
    for (qint64 time = from; time < to; time += 60000) {
        const auto local = QDateTime::fromMSecsSinceEpoch(time, zone).time();
        if (local.hour() % 3 == 0 && local.minute() == 0)
            ticks.append(QJsonObject{{"time", double(time)}, {"label", QString::number(local.hour()).rightJustified(2, '0')}});
    }
    ticks.append(QJsonObject{{"time", double(to)}, {"label", "24"}});
    const bool targets = std::isfinite(low) && std::isfinite(high) && low > 0 && high > low;
    return {{"from", double(from)},
            {"to", double(to)},
            {"segments", segments},
            {"max", maximum},
            {"low", targets ? QJsonValue(low) : QJsonValue()},
            {"high", targets ? QJsonValue(high) : QJsonValue()},
            {"ticks", ticks},
            {"dateLabel", QLocale("fi_FI").toString(day, "ddd d.M.yyyy")},
            {"timezone", QString::fromUtf8(zone.id())},
            {"date", day.toString(Qt::ISODate)}};
}
} // namespace Studio
