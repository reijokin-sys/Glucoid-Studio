// SPDX-License-Identifier: GPL-3.0-or-later
// Canonical synthetic fixture. Refresh project copies with sync.py --sync.
#pragma once

#include "glucoseanalysis.h"
#include <QDateTime>

namespace GlucoidFixture {

struct NightWindow {
    QList<Studio::Interval> intervals;
    int days;
    double mean;
    double coverage;
};

struct NightWindows {
    QDate day;
    QTimeZone zone;
    NightWindow clipped;
    NightWindow extended;
    double clippedCoveredSeconds;
};

inline NightWindows nightWindows() {
    const QDate day(2026, 10, 2);
    const QTimeZone zone("UTC");
    const auto at = QDateTime(day, QTime(0, 0), zone).toMSecsSinceEpoch();
    constexpr qint64 hour = 3600000;

    // The selected night is clipped at midnight and 06:00, with a 03–05 gap.
    const NightWindow clipped{{
        {at-hour, at+hour, 4, "stream", {}},
        {at+hour, at+3*hour, 10, "history", {}},
        {at+5*hour, at+7*hour, 6, "stream", {}},
        {at+24*hour, at+25*hour, 20, "stream", {}},
    }, 1, 7.5, 4.0/6};

    // The first night contributes one hour and the second contributes two.
    const NightWindow extended{{
        {at, at+hour, 4, "stream", {}},
        {at+24*hour, at+26*hour, 10, "stream", {}},
    }, 2, 8.0, 3.0/12};
    return {day, zone, clipped, extended, 4*3600.0};
}

}
