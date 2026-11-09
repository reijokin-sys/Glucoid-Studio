// SPDX-License-Identifier: GPL-3.0-or-later
#include "store.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <cmath>

namespace Studio {
namespace {
struct Event {
    QString key, kind, medicine;
    double value = 0;
};
struct Record {
    QString id, raw, sensor, unit;
    qint64 time = 0;
    double original = 0, mmol = 0;
    bool deleted = false, glucose = false, reliable = true, unsupported = false;
    QList<Event> events;
};

qint64 timestamp(const QJsonValue& value) {
    if (value.isDouble()) {
        const double time = value.toDouble();
        if (!std::isfinite(time) || time <= 0 || time > 9e15) return 0;
        return qRound64(time < 1e11 ? time * 1000 : time);
    }
    const auto date = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    // A timestamp without an offset must not silently inherit the PC timezone.
    if (!date.isValid() || date.timeSpec() == Qt::LocalTime) return 0;
    return date.toMSecsSinceEpoch();
}

bool quantity(const QJsonObject& object, const QString& field, double& value) {
    const auto input = object[field];
    if (!input.isDouble()) return false;
    value = input.toDouble();
    return std::isfinite(value) && value >= 0;
}

bool parseRecord(const QJsonObject& object, const QString& collection, Record& record) {
    const QString identifier = object["identifier"].toString(object["_id"].toString());
    if (identifier.isEmpty() || identifier.size() > 512 || identifier.contains(QChar(0))) return false;
    record.id = "nightscout:" + identifier;
    record.raw = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    if (object.contains("isValid") && !object["isValid"].isBool()) return false;
    record.deleted = object.contains("isValid") && object["isValid"] == false;
    if (record.deleted) return true;
    record.time = timestamp(object["date"]);
    if (!record.time && !object.contains("date")) record.time = timestamp(object["created_at"]);
    if (!record.time) return false;
    if (collection == "entries") {
        if (object["type"].toString(object.contains("sgv") ? "sgv" : "") != "sgv") return true;
        if (!quantity(object, "sgv", record.original)) return false;
        record.unit = object["units"].toString("mg/dl").toLower();
        if (record.unit == "mg" || record.unit == "mg/dl")
            record.mmol = record.original / 18.0;
        else if (record.unit == "mmol" || record.unit == "mmol/l")
            record.mmol = record.original;
        else
            return false;
        record.glucose = true;
        record.reliable = record.mmol > 0;
        record.sensor = object["device"].toString("Nightscout");
        return true;
    }
    const QString type = object["eventType"].toString();
    if (object.contains("carbs") && !object["carbs"].isNull()) {
        double value;
        if (!quantity(object, "carbs", value)) return false;
        const auto units = object["carbsUnits"].toString("grams").toLower();
        if (units != "g" && units != "grams") return false;
        record.events.append({record.id + ":carbs", "carbs", {}, value});
    }
    if (object.contains("insulin") && !object["insulin"].isNull()) {
        double value;
        if (!quantity(object, "insulin", value)) return false;
        const auto units = object["insulinUnits"].toString("U").toLower();
        if (units != "u" && units != "units") return false;
        QString kind;
        if (type == "Correction Bolus")
            kind = "correction";
        else if (QStringList{"Meal Bolus", "Snack Bolus", "Bolus", "Bolus Wizard"}.contains(type))
            kind = "rapid";
        else if (QStringList{"Basal Insulin", "Basal Insulin Dose", "Dose Basal"}.contains(type))
            kind = "basal";
        if (object["duration"].toDouble() > 0 || object["splitExt"].toDouble() > 0) kind.clear();
        if (kind.isEmpty())
            record.unsupported = true;
        else
            record.events.append({record.id + ":insulin", kind, object["insulinType"].toString(), value});
    }
    if (type == "Temp Basal" || type == "Combo Bolus" || object.contains("absolute") || object.contains("percent")) record.unsupported = true;
    return true;
}
} // namespace

bool Store::importNightscoutPage(const QString& runId, const QString& collection, const QByteArray& bytes, qint64 cursor) {
    if ((collection != "entries" && collection != "treatments") || cursor < 0) {
        m_error = "Tuntematon Nightscout-aineisto tai hakuraja.";
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    const auto object = document.object();
    if (parseError.error != QJsonParseError::NoError || object["status"].toInt() != 200 || !object["result"].isArray()) {
        m_error = "Virheellinen Nightscout-vastaus.";
        return false;
    }
    QList<Record> records;
    QSet<QString> identifiers;
    for (const auto& value : object["result"].toArray()) {
        Record record;
        if (!value.isObject() || !parseRecord(value.toObject(), collection, record) || identifiers.contains(record.id)) {
            m_error = "Nightscout-tapahtuman tunniste, aika, arvo tai yksikkö ei kelpaa. Sivua ei tallennettu.";
            return false;
        }
        identifiers.insert(record.id);
        records.append(record);
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString folder = QFileInfo(filename).absolutePath() + "/imports/" + runId;
    if (!QDir().mkpath(folder)) {
        m_error = "Nightscout-tuontiarkiston luominen epäonnistui.";
        return false;
    }
    const QString responsePath = folder + "/" + id + ".json";
    QSaveFile file(responsePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = "Nightscoutin alkuperäisvastauksen tallennus epäonnistui.";
        return false;
    }
    if (!run("BEGIN IMMEDIATE")) return false;
    bool ok = true;
    const int before = sqlite3_total_changes(db);
    for (const auto& record : records) {
        if (!ok) break;
        if (collection == "entries") {
            if (record.deleted || !record.glucose) {
                ok = run("UPDATE glucose SET reliable=0 WHERE id=? AND reliable<>0", {record.id});
            } else {
                ok =
                    run("INSERT INTO glucose(id,sensor,time,mmol,original,unit,reliable,series_kind,calibration,original_timezone) "
                        "VALUES(?,?,?,?,?,?,?,'stream','raw','UTC') ON CONFLICT(id) DO UPDATE SET "
                        "sensor=excluded.sensor,time=excluded.time,mmol=excluded.mmol,original=excluded.original,unit=excluded.unit,reliable=excluded.reliable "
                        "WHERE sensor<>excluded.sensor OR time<>excluded.time OR mmol<>excluded.mmol OR original<>excluded.original "
                        "OR unit<>excluded.unit OR reliable<>excluded.reliable",
                        {record.id, record.sensor, record.time, record.mmol, record.original, record.unit, record.reliable});
            }
        } else {
            QSet<QString> active;
            for (const auto& event : record.events) active.insert(event.key);
            for (const auto& suffix : QStringList{":carbs", ":insulin"}) {
                const auto key = record.id + suffix;
                if (ok && !active.contains(key)) ok = run("UPDATE amounts SET active=0 WHERE id=? AND active=1", {key});
            }
            for (const auto& event : record.events) {
                if (!ok) break;
                ok = run("INSERT INTO amounts(id,source,time,label,value,raw,active,kind,medicine) "
                         "VALUES(?,'nightscout-v3',?,?,?,?,1,?,?) ON CONFLICT(id) DO UPDATE SET "
                         "time=excluded.time,label=excluded.label,value=excluded.value,raw=excluded.raw,active=1,kind=excluded.kind,medicine=excluded.medicine "
                         "WHERE time<>excluded.time OR label<>excluded.label OR value<>excluded.value OR raw<>excluded.raw "
                         "OR active<>1 OR kind<>excluded.kind OR medicine<>excluded.medicine",
                         {event.key, record.time, "Nightscout: " + event.kind, event.value, record.raw, event.kind, event.medicine});
            }
        }
    }
    if (ok && sqlite3_total_changes(db) > before) ok = setMeta("revision", QUuid::createUuid().toString(QUuid::WithoutBraces));
    // Warning metadata, cursors and provenance do not themselves change content revision.
    for (const auto& record : records) {
        if (!ok || collection != "treatments") break;
        const auto key = "nightscoutUnsupported:" + record.id;
        ok = record.unsupported ? setMeta(key, "1") : run("DELETE FROM meta WHERE key=?", {key});
    }
    if (ok) ok = setMeta("nightscoutCursor:" + collection, QString::number(cursor));
    if (ok) ok = setMeta("lastSync", QString::number(QDateTime::currentMSecsSinceEpoch()));
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (ok)
        ok = run("INSERT INTO import_chunks VALUES(?,?,?,?,?,?,?,?,?)",
                 {id, runId, "nightscout-" + collection, 0, cursor, responsePath, hash, records.size(), "typed-nightscout-v3"});
    if (ok) ok = run("COMMIT");
    if (!ok) run("ROLLBACK");
    return ok;
}

bool Store::reconcileNightscout(const QString& collection, qint64 from, const QSet<QString>& seen) {
    if (collection != "entries" && collection != "treatments") return false;
    if (!run("BEGIN IMMEDIATE")) return false;
    const int before = sqlite3_total_changes(db);
    bool ok = true;
    if (collection == "entries") {
        for (const auto& value : query("SELECT id FROM glucose WHERE id GLOB 'nightscout:*' AND time>=? AND reliable<>0", {from})) {
            const auto id = value.toObject()["id"].toString();
            if (ok && !seen.contains(id)) ok = run("UPDATE glucose SET reliable=0 WHERE id=?", {id});
        }
    } else {
        for (const auto& value : query("SELECT id FROM amounts WHERE source='nightscout-v3' AND active=1 AND local=0")) {
            const auto id = value.toObject()["id"].toString();
            if (ok && !seen.contains(id.left(id.lastIndexOf(':')))) ok = run("UPDATE amounts SET active=0 WHERE id=?", {id});
        }
    }
    if (ok && sqlite3_total_changes(db) > before) ok = setMeta("revision", QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (ok && collection == "treatments") {
        const QString prefix = "nightscoutUnsupported:";
        for (const auto& value : query("SELECT key FROM meta WHERE key GLOB 'nightscoutUnsupported:*'")) {
            const auto key = value.toObject()["key"].toString();
            if (ok && !seen.contains(key.mid(prefix.size()))) ok = run("DELETE FROM meta WHERE key=?", {key});
        }
    }
    if (ok) ok = run("COMMIT");
    if (!ok) run("ROLLBACK");
    return ok;
}
} // namespace Studio
