// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "glucose.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace Studio {
struct Amount {
    QString key, source, label, raw;
    QString kind = "unknown";
    qint64 sequence = 0;
    qint64 time = 0;
    double value = 0;
};
struct ParsedAmounts {
    QList<Amount> rows;
    QString error;
};
ParsedAmounts parseAmounts(const QByteArray& data);
QString fingerprint(const Amount& a);
// Attach source categories to existing TSV identities; never infer from label text.
bool applyJugglucoClasses(ParsedAmounts& amounts, const QByteArray& json, QString& error);
double number(const QJsonObject& o, const QString& key);
} // namespace Studio
