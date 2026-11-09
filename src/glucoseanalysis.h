// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "glucose.h"
#include <QDate>
#include <QTimeZone>
namespace Studio {
inline constexpr int summaryMaxGapSeconds = 1200;
inline constexpr qint64 summaryPreRollMs = qint64(summaryMaxGapSeconds) * 1000;
struct Interval {
    qint64 from, to;
    double value;
    QString kind, sensor;
};
QList<Interval> intervals(QList<Reading> rows, qint64 from, qint64 to, int maxGapSeconds = 1200);
QList<Interval> selectSeries(const QList<Reading>& stream, const QList<Reading>& history, qint64 from, qint64 to, bool fill, int maxGapSeconds = 1200);
QJsonObject analyse(const QList<Interval>& data, QDate first, int days, const QTimeZone& zone, double low, double high);
// Standard summary policy: stream first, history only in uncovered intervals.
// Each period estimates cadence independently with the same 20-minute pre-roll.
QJsonObject summaryAnalysis(const QList<Reading>& stream, const QList<Reading>& history, QDate first, int days, const QTimeZone& zone, double low, double high);
QJsonObject currentGlucose(QList<Reading> stream, QList<Reading> history, qint64 now, const QTimeZone& zone, double low, double high, double criticalLow = 0);
// GMI (glucose management indicator), Bergenstal et al., Diabetes Care 2018.
// A CGM-derived estimate of HbA1c, not a laboratory HbA1c measurement.
double gmiPercent(double meanMmol);
double gmiMmolMol(double meanMmol);
} // namespace Studio
