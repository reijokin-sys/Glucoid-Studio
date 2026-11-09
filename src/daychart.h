// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "glucoseanalysis.h"

namespace Studio {
// Standard summary policy and local calendar labels, independent of research
// settings and the UI process's system timezone.
QJsonObject currentDayChartData(const QList<Reading> &stream,
    const QList<Reading> &history, QDate day, const QTimeZone &zone,
    double low, double high);
}
