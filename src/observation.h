// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "domain.h"
#include "glucoseanalysis.h"
#include <QTimeZone>
#include <QSet>

namespace Studio {
QStringList validateObservationProfile(const QJsonObject& profile, qint64 now = 0);
QJsonObject observeProfile(const QJsonObject& profile, const QList<Interval>& data, QDate first, int days, const QTimeZone& zone);
} // namespace Studio
