// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "glucoseanalysis.h"
#include <QDateTime>
namespace GlucoidFixture {
struct SummaryPolicy {
    QDate first{2026,10,1};
    QTimeZone zone{"UTC"};
    qint64 start=QDateTime(first,QTime(0,0),zone).toMSecsSinceEpoch();
    Studio::Reading row(QString key,qint64 time,double value,QString kind="stream") const {
        Studio::Reading r;r.key=key;r.sensor="synthetic";r.time=time;
        r.mmol=r.original=value;r.originalUnit="mmol/L";r.seriesKind=kind;return r;
    }
    QList<Studio::Reading> stream() const {
        // The preceding comparison period must not influence current cadence.
        return {row("prior0",start-86400000,6),row("prior1",start-86100000,6),
            row("prior2",start-85800000,6),row("live",start,6)};
    }
    QList<Studio::Reading> history() const {
        return {row("stored0",start,10,"history"),row("stored1",start+300000,10,"history")};
    }
};
}
