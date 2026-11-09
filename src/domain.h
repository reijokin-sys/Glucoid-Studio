// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "glucose.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace Studio {
struct Amount { QString key, source, label, raw; qint64 time=0; double value=0; };
struct ParsedAmounts { QList<Amount> rows; QString error; };
ParsedAmounts parseAmounts(const QByteArray &data);
QString fingerprint(const Amount &a);
QStringList validateProfile(const QJsonObject &profile);
QStringList validateHistoricalProfile(const QJsonObject &profile);
QStringList validateResponseProfile(const QJsonObject &profile);
QJsonObject emptyProfile();
QJsonObject metrics(QList<Reading> readings, qint64 from, qint64 to, double low, double high, int gapSeconds=600);
double remainingFraction(const QJsonArray &curve, double minutes);
QJsonObject calculateBolus(const QJsonObject &profile, const QJsonObject &input, const QJsonArray &doses, qint64 now);
QJsonObject recommend(const QJsonObject &profile, const QString &kind, const QString &slot, const QJsonArray &episodes, qint64 now);
QString slotAt(const QJsonObject &profile, qint64 time);
QJsonObject slotProfile(const QJsonObject &profile, const QString &slot);
double number(const QJsonObject &o, const QString &key);
}
