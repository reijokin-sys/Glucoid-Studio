// SPDX-License-Identifier: GPL-3.0-or-later
#include "storefixtures.h"
#include "databasebackup.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
namespace Studio {
bool FixtureStore::backup(const QString& target) {
    return exportDatabaseBackup(path(), target, "juggluco");
}
static QString json(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
bool FixtureStore::setDeleted(const QString& id, bool deleted) {
    if (query("SELECT id FROM amounts WHERE id=? AND active=1", {id}).isEmpty()) return false;
    if (!run("BEGIN IMMEDIATE")) return false;
    bool ok = deleted ? run("INSERT INTO removed_amounts(id,time) VALUES(?,?) ON CONFLICT(id) DO NOTHING", {id, QDateTime::currentMSecsSinceEpoch()})
                      : run("DELETE FROM removed_amounts WHERE id=?", {id});
    if (ok)
        ok = run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)",
                 {QDateTime::currentMSecsSinceEpoch(), deleted ? "diary-remove" : "diary-restore", json({{"id", id}})});
    if (ok) ok = setMeta("revision", QUuid::createUuid().toString());
    if (ok) ok = run("COMMIT");
    if (!ok) run("ROLLBACK");
    return ok;
}
QString FixtureStore::addLocal(qint64 time, double value, const QString& kind, const QString& medicine, const QString& purpose) {
    if (!std::isfinite(value) || value < 0 || time <= 0 || !QStringList{"rapid", "correction", "basal", "carbs", "other"}.contains(kind) ||
        ((kind == "rapid" || kind == "correction" || kind == "basal") && medicine.trimmed().isEmpty()))
        return {};
    const auto id = "local:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!run("INSERT INTO amounts(id,source,time,label,value,raw,local,kind,medicine,purpose) VALUES(?,?,?,?,?,?,1,?,?,?)",
             {id, "local", time, kind, value, "", kind, medicine, purpose}))
        return {};
    setMeta("revision", QUuid::createUuid().toString());
    return id;
}
bool FixtureStore::annotate(const QString& id, const QJsonObject& annotation) {
    return run("INSERT INTO annotations VALUES(?,?) ON CONFLICT(id) DO UPDATE SET data=excluded.data", {id, json(annotation)}) &&
           setMeta("revision", QUuid::createUuid().toString());
}
} // namespace Studio
