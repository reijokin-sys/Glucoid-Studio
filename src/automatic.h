// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "analysis.h"

namespace Studio {
// Automatic observations and experimental target-dose models. Never a prescription.
QJsonObject automaticMealAnalysis(const QList<Reading>&stream,const QList<Reading>&history,
    const QJsonArray&amounts,const QJsonArray&profiles,const QJsonObject&settings,
    qint64 from,qint64 to,qint64 availableUntil);
QJsonObject automaticMealSnapshot(const QString&database,const QJsonObject&settings,
    qint64 from,qint64 to,qint64 availableUntil);
QString automaticIssueText(const QString&code);
}
