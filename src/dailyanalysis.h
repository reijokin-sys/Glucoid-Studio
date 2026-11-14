// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "glucoseanalysis.h"
namespace Studio {
QJsonObject dailyPeriodAnalysis(QList<Reading> history, const QJsonArray& amounts, const QTimeZone& zone, QDate last);
QJsonObject dailyPeriodSnapshot(const QString& database, const QTimeZone& zone, qint64 now);
} // namespace Studio
