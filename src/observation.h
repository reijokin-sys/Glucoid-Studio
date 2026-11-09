// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "analysis.h"

namespace Studio {
QStringList validateObservationProfile(const QJsonObject& profile, qint64 now = 0);
QJsonObject observeProfile(const QJsonObject& profile, const QList<Interval>& data, const QJsonArray& amounts, QDate first, int days, const QTimeZone& zone,
                           const QSet<QString>& completeDays);
} // namespace Studio
