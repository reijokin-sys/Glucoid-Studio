// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
namespace Studio {
// Creates synthetic examples of persisted legacy records. Never part of the product.
class FixtureStore : public Store {
public:
    using Store::Store;
    bool backup(const QString& path);
    bool setDeleted(const QString& id, bool deleted);
    QString addLocal(qint64 time, double value, const QString& kind, const QString& medicine, const QString& purpose);
    bool annotate(const QString& id, const QJsonObject& annotation);
};
}
