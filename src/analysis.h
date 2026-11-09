// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "domain.h"
#include "glucoseanalysis.h"
#include <QDate>
#include <QSet>
#include <QTimeZone>
namespace Studio {
QJsonObject insulinSummary(const QJsonArray& amounts, QDate first, int days, const QTimeZone& zone, const QSet<QString>& completeDays);
}
