// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
namespace Studio {
struct Reading {
    QString key, sensor, originalUnit;
    qint64 time = 0;
    double mmol = 0, original = 0;
    bool reliable = true;
    QString seriesKind = "stream", calibration = "raw", originalTimezone;
};
struct ParsedReadings {
    QList<Reading> rows;
    QString error;
};
ParsedReadings parseStream(const QByteArray& data, const QString& kind = "stream");
} // namespace Studio
