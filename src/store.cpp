// SPDX-License-Identifier: GPL-3.0-or-later
#include "store.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimeZone>
#include <QUuid>
#include <QCryptographicHash>
#include <sys/stat.h>
#include <algorithm>
#include <cmath>

namespace Studio {
static QString json(const QJsonObject& o) {
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
static QJsonObject object(const QJsonValue& v) {
    return QJsonDocument::fromJson(v.toString().toUtf8()).object();
}
Store::Store(const QString& path, bool readOnly) : filename(path) {
    if (readOnly) {
        if (sqlite3_open_v2(path.toUtf8(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
            m_error = "Tietokannan lukeminen epäonnistui.";
            return;
        }
        sqlite3_busy_timeout(db, 10000);
        if (meta("schema") != "2") m_error = "Tietokannan versio ei ole tuettu.";
        m_available = m_error.isEmpty();
        return;
    }
    umask(0077);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_error = "Tietohakemiston luominen epäonnistui.";
        return;
    }
    QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (sqlite3_open_v2(path.toUtf8(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        m_error = "Tietokannan avaaminen epäonnistui.";
        return;
    }
    sqlite3_busy_timeout(db, 10000);
    const QStringList schema{
        "PRAGMA journal_mode=WAL",
        "PRAGMA foreign_keys=ON",
        "PRAGMA synchronous=FULL",
        "CREATE TABLE IF NOT EXISTS glucose_artifacts(id TEXT PRIMARY KEY,from_time INTEGER,to_time INTEGER,reason TEXT,evidence TEXT,reviewer "
        "TEXT,source_hash TEXT,created INTEGER,active INTEGER NOT NULL DEFAULT 1)",
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS glucose(id TEXT PRIMARY KEY,sensor TEXT NOT NULL,time INTEGER NOT NULL,mmol REAL NOT NULL,original REAL NOT NULL,unit TEXT "
        "NOT NULL,reliable INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS glucose_time ON glucose(time)",
        "CREATE TABLE IF NOT EXISTS amounts(id TEXT PRIMARY KEY,source TEXT NOT NULL,time INTEGER NOT NULL,label TEXT NOT NULL,value REAL NOT NULL,raw TEXT "
        "NOT NULL,active INTEGER NOT NULL DEFAULT 1,local INTEGER NOT NULL DEFAULT 0,kind TEXT NOT NULL DEFAULT 'unknown',medicine TEXT NOT NULL DEFAULT "
        "'',purpose TEXT NOT NULL DEFAULT '')",
        "CREATE INDEX IF NOT EXISTS amount_time ON amounts(time)",
        "CREATE TABLE IF NOT EXISTS mappings(label TEXT PRIMARY KEY,kind TEXT NOT NULL,medicine TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS annotations(id TEXT PRIMARY KEY REFERENCES amounts(id),data TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS removed_amounts(id TEXT PRIMARY KEY REFERENCES amounts(id),time INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS episodes(id TEXT PRIMARY KEY,data TEXT NOT NULL,dependency TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS profiles(version INTEGER PRIMARY KEY AUTOINCREMENT,data TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS audit(id INTEGER PRIMARY KEY AUTOINCREMENT,time INTEGER NOT NULL,kind TEXT NOT NULL,data TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS calculations(id INTEGER PRIMARY KEY AUTOINCREMENT,time INTEGER NOT NULL,kind TEXT NOT NULL,data TEXT NOT NULL,revision "
        "TEXT NOT NULL)",
        "CREATE TRIGGER IF NOT EXISTS preserve_glucose BEFORE UPDATE ON glucose BEGIN INSERT INTO audit(time,kind,data) "
        "VALUES(unixepoch()*1000,'previous-glucose',json_object('id',OLD.id,'sensor',OLD.sensor,'time',OLD.time,'mmol',OLD.mmol,'original',OLD.original,'unit',"
        "OLD.unit,'reliable',OLD.reliable)); END",
        "CREATE TRIGGER IF NOT EXISTS preserve_annotation BEFORE UPDATE ON annotations BEGIN INSERT INTO audit(time,kind,data) "
        "VALUES(unixepoch()*1000,'previous-annotation',json_object('id',OLD.id,'data',OLD.data)); END"};
    (void)std::any_of(schema.cbegin(), schema.cend(), [this](const QString& s) { return !run(s); });
    if (meta("schema").isEmpty()) setMeta("schema", "1");
    if (meta("schema") == "1" && m_error.isEmpty()) {
        const QString target = QFileInfo(path).absolutePath() + "/backups/pre-schema2-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".sqlite3";
        if (!backup(target)) return;
        bool ok = run("BEGIN IMMEDIATE");
        for (const auto& sql :
             QStringList{"ALTER TABLE glucose ADD COLUMN series_kind TEXT NOT NULL DEFAULT 'stream'",
                         "ALTER TABLE glucose ADD COLUMN calibration TEXT NOT NULL DEFAULT 'raw'",
                         "ALTER TABLE glucose ADD COLUMN original_timezone TEXT NOT NULL DEFAULT ''",
                         "CREATE TABLE import_runs(id TEXT PRIMARY KEY,started INTEGER NOT NULL,finished INTEGER,from_time INTEGER,to_time INTEGER,transport "
                         "TEXT,status TEXT,error TEXT)",
                         "CREATE TABLE import_chunks(id TEXT PRIMARY KEY,run_id TEXT REFERENCES import_runs(id),kind TEXT,from_time INTEGER,to_time "
                         "INTEGER,path TEXT,sha256 TEXT,row_count INTEGER,mapping TEXT)",
                         "CREATE TABLE mapping_versions(id INTEGER PRIMARY KEY AUTOINCREMENT,time INTEGER,label TEXT,kind TEXT,medicine TEXT)",
                         "CREATE TABLE analysis_exclusions(id TEXT PRIMARY KEY REFERENCES amounts(id),reason TEXT NOT NULL,time INTEGER NOT NULL)",
                         "CREATE TABLE complete_days(date TEXT PRIMARY KEY,revision TEXT NOT NULL)",
                         "CREATE TABLE analysis_runs(id TEXT PRIMARY KEY,time INTEGER,revision TEXT,settings TEXT,result TEXT)"}) {
            if (ok) ok = run(sql);
        }
        if (ok) ok = setMeta("schema", "2");
        if (ok) ok = run("COMMIT");
        if (!ok) {
            run("ROLLBACK");
            return;
        }
    }
    if (meta("schema") != "2") m_error = "Tietokannan versio ei ole tuettu.";
    // A process interrupted mid-import leaves a visible partial run, not a successful import.
    // Only recover runs after their owning process ended; the importer owns the sync lock.

    if (!purgeRetiredComparison()) return;

    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_available = m_error.isEmpty();
}
Store::~Store() {
    if (db) sqlite3_close(db);
}
bool Store::switchDatabase(const QString& path) {
    if (path == filename) return available();
    Store next(path);
    if (!next.available()) {
        m_error = next.error();
        return false;
    }
    if (db && sqlite3_close(db) != SQLITE_OK) {
        m_error = "Odota tietokannan käytön päättymistä.";
        return false;
    }
    db = next.db;
    next.db = nullptr;
    filename = path;
    m_error = next.m_error;
    m_available = next.m_available;
    return true;
}
bool Store::purgeRetiredComparison() {
    const auto rows = query("SELECT key FROM meta WHERE key IN ('supplementDaysV1','supplementSelectionV1','supplementSettingsV1')");
    if (rows.isEmpty() && meta("retiredComparisonV1") == "1") return true;
    if (!run("PRAGMA secure_delete=ON") || !run("BEGIN IMMEDIATE")) return false;
    const bool ok = run("DELETE FROM meta WHERE key IN ('supplementDaysV1','supplementSelectionV1','supplementSettingsV1')") &&
                    run("DELETE FROM audit WHERE kind GLOB 'supplement-*'") && setMeta("retiredComparisonV1", "1");
    if (!ok) {
        run("ROLLBACK");
        return false;
    }
    if (!run("COMMIT")) return false;
    return run("VACUUM") && run("PRAGMA wal_checkpoint(TRUNCATE)");
}
static bool bind(sqlite3_stmt* stmt, const QVariantList& args) {
    for (int i = 0; i < args.size(); ++i) {
        const auto& v = args[i];
        int result;
        if (v.isNull())
            result = sqlite3_bind_null(stmt, i + 1);
        else if (v.metaType().id() == QMetaType::Double)
            result = sqlite3_bind_double(stmt, i + 1, v.toDouble());
        else if (v.metaType().id() == QMetaType::Int || v.metaType().id() == QMetaType::LongLong || v.metaType().id() == QMetaType::Bool)
            result = sqlite3_bind_int64(stmt, i + 1, v.toLongLong());
        else {
            const auto b = v.toString().toUtf8();
            result = sqlite3_bind_text(stmt, i + 1, b.constData(), b.size(), SQLITE_TRANSIENT);
        }
        if (result != SQLITE_OK) return false;
    }
    return true;
}
bool Store::run(const QString& sql, const QVariantList& args) {
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.toUtf8(), -1, &st, nullptr) != SQLITE_OK) {
        m_error = "Tietokantakomennon valmistelu epäonnistui.";
        return false;
    }
    if (!bind(st, args)) {
        sqlite3_finalize(st);
        m_error = "Tietokannan parametrivirhe.";
        return false;
    }
    const int r = sqlite3_step(st);
    sqlite3_finalize(st);
    if (r != SQLITE_DONE && r != SQLITE_ROW) {
        m_error = "Tietokantakomento epäonnistui.";
        return false;
    }
    return true;
}
QJsonArray Store::query(const QString& sql, const QVariantList& args) const {
    QJsonArray rows;
    sqlite3_stmt* st = nullptr;
    if (!db || sqlite3_prepare_v2(db, sql.toUtf8(), -1, &st, nullptr) != SQLITE_OK) {
        m_error = "Tietokantakysely epäonnistui.";
        return rows;
    }
    if (!bind(st, args)) {
        sqlite3_finalize(st);
        m_error = "Tietokannan parametrivirhe.";
        return rows;
    }
    int r;
    while ((r = sqlite3_step(st)) == SQLITE_ROW) {
        QJsonObject o;
        for (int i = 0; i < sqlite3_column_count(st); ++i) {
            const QString key = QString::fromUtf8(sqlite3_column_name(st, i));
            switch (sqlite3_column_type(st, i)) {
            case SQLITE_INTEGER: o[key] = double(sqlite3_column_int64(st, i)); break;
            case SQLITE_FLOAT: o[key] = sqlite3_column_double(st, i); break;
            case SQLITE_NULL: o[key] = QJsonValue(); break;
            default: o[key] = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st, i))); break;
            }
        }
        rows.append(o);
    }
    sqlite3_finalize(st);
    if (r != SQLITE_DONE) m_error = "Tietokannan lukeminen jäi kesken.";
    return rows;
}
QString Store::meta(const QString& key) const {
    auto a = query("SELECT value FROM meta WHERE key=?", {key});
    return a.isEmpty() ? QString() : a[0].toObject()["value"].toString();
}
bool Store::setMeta(const QString& key, const QString& value) {
    return run("INSERT INTO meta VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", {key, value});
}
QString Store::revision() const {
    return meta("revision");
}
bool Store::ingest(const QList<Reading>& readings, const QList<Amount>& amounts, qint64 from, qint64 to, bool authoritative, bool allowEmpty) {
    if (to <= from) {
        m_error = "Virheellinen tuontijakso.";
        return false;
    }
    if (std::any_of(readings.cbegin(), readings.cend(), [&](const Reading& r) { return r.time < from || r.time >= to; })) {
        m_error = "Mittaus ei kuulu pyydettyyn aikaväliin.";
        return false;
    }
    if (std::any_of(amounts.cbegin(), amounts.cend(), [&](const Amount& a) { return a.time < from || a.time >= to; })) {
        m_error = "Merkintä ei kuulu pyydettyyn aikaväliin.";
        return false;
    }
    const auto old = query("SELECT id FROM amounts WHERE local=0 AND active=1 AND time>=? AND time<?", {from, to});
    if (authoritative && !allowEmpty && amounts.isEmpty() && !old.isEmpty()) {
        m_error = "Merkintälähde palautti tyhjää. Aiemmat merkinnät säilytettiin; tarkista vienti.";
        return false;
    }
    if (!run("SAVEPOINT normalized_ingest")) return false;
    bool ok = true;
    const int before = sqlite3_total_changes(db);
    // Only deactivate records actually missing from an authoritative snapshot.
    // Toggling every existing row off and on would create a false revision.
    QSet<QString> incomingIds;
    for (const auto& amount : amounts) incomingIds.insert(amount.key);
    if (authoritative) {
        for (const auto& value : old) {
            const auto id = value.toObject()["id"].toString();
            if (ok && !incomingIds.contains(id)) ok = run("UPDATE amounts SET active=0 WHERE id=? AND local=0 AND active=1", {id});
        }
    }
    for (const auto& r : readings)
        if (ok)
            ok = run("INSERT INTO glucose(id,sensor,time,mmol,original,unit,reliable,series_kind,calibration,original_timezone) VALUES(?,?,?,?,?,?,?,?,?,?) ON "
                     "CONFLICT(id) DO UPDATE SET "
                     "mmol=excluded.mmol,original=excluded.original,unit=excluded.unit,reliable=excluded.reliable,calibration=excluded.calibration,original_"
                     "timezone=excluded.original_timezone WHERE mmol<>excluded.mmol OR original<>excluded.original OR unit<>excluded.unit OR "
                     "reliable<>excluded.reliable OR calibration<>excluded.calibration OR original_timezone<>excluded.original_timezone",
                     {r.key, r.sensor, r.time, r.mmol, r.original, r.originalUnit, r.reliable, r.seriesKind, r.calibration, r.originalTimezone});
    for (const auto& a : amounts)
        if (ok) {
            ok = run("INSERT INTO amounts(id,source,time,label,value,raw,active) VALUES(?,?,?,?,?,?,1) ON CONFLICT(id) DO UPDATE SET active=1 WHERE "
                     "amounts.active<>1",
                     {a.key, a.source, a.time, a.label, a.value, a.raw});
        }
    // Content snapshots, not arbitrary renumbering, determine whether review is invalidated.
    if (ok && sqlite3_total_changes(db) > before) ok = setMeta("revision", QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (ok) ok = setMeta("lastSync", QString::number(QDateTime::currentMSecsSinceEpoch()));
    if (ok)
        ok = run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)", {QDateTime::currentMSecsSinceEpoch(), "import",
                                                                     json({{"from", double(from)},
                                                                           {"to", double(to)},
                                                                           {"readingCount", readings.size()},
                                                                           {"amountCount", amounts.size()},
                                                                           {"authoritative", authoritative}})});
    if (ok) ok = run("RELEASE normalized_ingest");
    if (!ok) {
        run("ROLLBACK TO normalized_ingest");
        run("RELEASE normalized_ingest");
    }
    return ok;
}
QList<Reading> Store::readings(qint64 from, qint64 to, const QString& kind) const {
    QList<Reading> result;
    for (const auto& v : query("SELECT * FROM glucose WHERE time>=? AND time<? AND series_kind=? ORDER BY time,id", {from, to, kind})) {
        auto o = v.toObject();
        Reading r;
        r.key = o["id"].toString();
        r.sensor = o["sensor"].toString();
        r.time = qint64(o["time"].toDouble());
        r.mmol = o["mmol"].toDouble();
        r.original = o["original"].toDouble();
        r.originalUnit = o["unit"].toString();
        r.reliable = o["reliable"].toInt();
        r.seriesKind = o["series_kind"].toString();
        r.calibration = o["calibration"].toString();
        r.originalTimezone = o["original_timezone"].toString();
        result.append(r);
    }
    return result;
}
QJsonArray Store::amounts(qint64 from, qint64 to, bool includeDeleted) const {
    QJsonArray result;
    for (const auto& v :
         query("SELECT a.*,m.kind AS mappedKind,m.medicine AS mappedMedicine,n.data AS annotation,d.time AS deletedAt,x.reason AS exclusionReason FROM amounts "
               "a LEFT JOIN mappings m ON a.label=m.label LEFT JOIN annotations n ON a.id=n.id LEFT JOIN removed_amounts d ON a.id=d.id LEFT JOIN "
               "analysis_exclusions x ON x.id=a.id WHERE a.active=1 AND a.time>=? AND a.time<? AND (? OR d.id IS NULL) ORDER BY a.time,a.id",
               {from, to, includeDeleted})) {
        auto o = v.toObject();
        o["local"] = o["local"].toInt() != 0;
        o["active"] = o["active"].toInt() != 0;
        // Parked 2026-10-31: the Juggluco prot label is not used; the raw rows stay
        // in the database but are hidden from every calculation and view.
        if (!o["local"].toBool() && o["label"].toString() == "prot") continue;
        if (!o["local"].toBool() && o["source"].toString() != "nightscout-v3") {
            o["kind"] = o["mappedKind"].toString("unknown");
            o["medicine"] = o["mappedMedicine"].toString();
        }
        const auto note = object(o["annotation"]);
        o["annotation"] = note;
        o["purpose"] = note.contains("purpose") ? note["purpose"] : o["purpose"];
        o["deleted"] = !o["deletedAt"].isNull();
        o["analysisExcluded"] = !o["exclusionReason"].isNull();
        o.remove("raw");
        o.remove("mappedKind");
        o.remove("mappedMedicine");
        result.append(o);
    }
    return result;
}
QJsonArray Store::calculationAmounts(qint64 from, qint64 to) const {
    QJsonArray result;
    for (auto v : amounts(from, to, true)) {
        auto a = v.toObject();
        if (a["deleted"].toBool() || a["analysisExcluded"].toBool()) {
            if (a["kind"] == "other") continue;
            a["stale"] = true;
        }
        result.append(a);
    }
    return result;
}
bool Store::setDeleted(const QString& id, bool deleted) {
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
QJsonArray Store::mappings() const {
    return query("SELECT DISTINCT a.label,COALESCE(m.kind,'unknown') AS kind,COALESCE(m.medicine,'') AS medicine FROM amounts a LEFT JOIN mappings m ON "
                 "a.label=m.label WHERE a.local=0 AND a.source<>'nightscout-v3' AND a.label<>'prot' ORDER BY a.label");
}
bool Store::mapLabel(const QString& label, const QString& kind, const QString& medicine) {
    if (!QStringList{"rapid", "correction", "basal", "carbs", "other", "unknown"}.contains(kind) ||
        ((kind == "rapid" || kind == "correction" || kind == "basal") && medicine.trimmed().isEmpty()))
        return false;
    return run("INSERT INTO mapping_versions(time,label,kind,medicine) VALUES(?,?,?,?)", {QDateTime::currentMSecsSinceEpoch(), label, kind, medicine}) &&
           run("INSERT INTO mappings VALUES(?,?,?) ON CONFLICT(label) DO UPDATE SET kind=excluded.kind,medicine=excluded.medicine", {label, kind, medicine}) &&
           setMeta("revision", QUuid::createUuid().toString());
}
QString Store::addLocal(qint64 time, double value, const QString& kind, const QString& medicine, const QString& purpose) {
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
bool Store::annotate(const QString& id, const QJsonObject& annotation) {
    return run("INSERT INTO annotations VALUES(?,?) ON CONFLICT(id) DO UPDATE SET data=excluded.data", {id, json(annotation)}) &&
           setMeta("revision", QUuid::createUuid().toString());
}
static QString dependency(const Store& s, qint64 from, qint64 to) {
    QJsonArray readings;
    for (const auto& r : s.readings(from, to))
        readings.append(QJsonObject{{"id", r.key}, {"time", double(r.time)}, {"value", r.mmol}, {"reliable", r.reliable}});
    QJsonObject o{{"readings", readings}, {"amounts", s.amounts(from, to, true)}};
    auto artifacts = s.artifactKeys(from, to).values();
    artifacts.sort();
    if (!artifacts.isEmpty()) o["artifactKeys"] = QJsonArray::fromStringList(artifacts);
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QString Store::saveEpisode(QJsonObject episode) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    episode["id"] = id;
    if (!run("SAVEPOINT reviewed_episode")) return {};
    bool ok = run("INSERT INTO episodes VALUES(?,?,?)",
                  {id, json(episode), dependency(*this, qint64(episode["dependencyFrom"].toDouble()), qint64(episode["end"].toDouble()) + 1)});
    if (ok) ok = setMeta("revision", QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (ok)
        ok = run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)", {QDateTime::currentMSecsSinceEpoch(), "episode-reviewed",
                                                                     json({{"id", id},
                                                                           {"kind", episode["kind"]},
                                                                           {"profileVersion", episode["profileVersion"]},
                                                                           {"methodVersion", episode["methodVersion"]},
                                                                           {"reviewer", episode["reviewer"]}})});
    if (ok) ok = run("RELEASE reviewed_episode");
    if (!ok) {
        run("ROLLBACK TO reviewed_episode");
        run("RELEASE reviewed_episode");
        return {};
    }
    return id;
}
QJsonArray Store::episodes() const {
    QJsonArray out;
    for (auto v : query("SELECT * FROM episodes ORDER BY rowid")) {
        auto row = v.toObject(), e = object(row["data"]);
        e["stale"] = row["dependency"].toString() != dependency(*this, qint64(e["dependencyFrom"].toDouble()), qint64(e["end"].toDouble()) + 1);
        out.append(e);
    }
    return out;
}
QJsonObject Store::activeProfile() const {
    auto rows = query("SELECT version,data FROM profiles ORDER BY version DESC LIMIT 1");
    if (rows.isEmpty()) return {};
    auto p = object(rows[0].toObject()["data"]);
    p["version"] = rows[0].toObject()["version"];
    return p;
}
QJsonObject Store::observationProfile() const {
    const int version = meta("observationProfileVersion").toInt();
    if (version <= 0) return {};
    auto rows = query("SELECT data FROM profiles WHERE version=?", {version});
    if (rows.isEmpty()) return {};
    auto p = object(rows[0].toObject()["data"]);
    if (!p["observationActive"].toBool()) return {};
    p["version"] = version;
    return p;
}
int Store::saveProfile(QJsonObject profile) {
    profile.remove("version");
    if (!run("INSERT INTO profiles(data) VALUES(?)", {json(profile)})) return 0;
    return int(sqlite3_last_insert_rowid(db));
}
QJsonArray Store::profiles() const {
    return query("SELECT version,data FROM profiles ORDER BY version DESC");
}
bool Store::saveCalculation(const QString& kind, const QJsonObject& result) {
    return run("INSERT INTO calculations(time,kind,data,revision) VALUES(?,?,?,?)", {QDateTime::currentMSecsSinceEpoch(), kind, json(result), revision()});
}
bool Store::backup(const QString& target) {
    if (QFileInfo::exists(target)) {
        m_error = "Varmuuskopion kohde on jo olemassa.";
        return false;
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    sqlite3* dest = nullptr;
    if (sqlite3_open(target.toUtf8(), &dest) != SQLITE_OK) {
        if (dest) sqlite3_close(dest);
        return false;
    }
    auto* copy = sqlite3_backup_init(dest, "main", db, "main");
    int code = copy ? sqlite3_backup_step(copy, -1) : SQLITE_ERROR;
    if (copy) sqlite3_backup_finish(copy);
    bool ok = code == SQLITE_DONE;
    sqlite3_stmt* st = nullptr;
    if (ok) {
        ok = sqlite3_prepare_v2(dest, "PRAGMA integrity_check", -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW &&
             QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st, 0))) == "ok";
        if (st) sqlite3_finalize(st);
    }
    sqlite3_close(dest);
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!ok) m_error = "Varmuuskopion eheystarkistus epäonnistui.";
    return ok;
}
QString Store::beginImport(qint64 from, qint64 to, const QString& transport) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return run("INSERT INTO import_runs(id,started,from_time,to_time,transport,status) VALUES(?,?,?,?,?,'running')",
               {id, QDateTime::currentMSecsSinceEpoch(), from, to, transport})
               ? id
               : QString();
}
bool Store::importChunk(const QString& runId, const QString& kind, const QByteArray& bytes, qint64 from, qint64 to, bool authoritative) {
    QList<Reading> parsedReadings;
    QList<Amount> parsedAmounts;
    if (kind == "amounts") {
        auto p = parseAmounts(bytes);
        if (!p.error.isEmpty()) {
            m_error = p.error;
            return false;
        }
        parsedAmounts = p.rows;
    } else {
        auto p = parseStream(bytes, kind);
        if (!p.error.isEmpty()) {
            m_error = p.error;
            return false;
        }
        parsedReadings = p.rows;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), folder = QFileInfo(filename).absolutePath() + "/imports/" + runId;
    if (!QDir().mkpath(folder)) {
        m_error = "Tuontiarkiston luominen epäonnistui.";
        return false;
    }
    const QString chunkPath = folder + "/" + id + ".tsv";
    QFile f(chunkPath);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        m_error = "Raakaviennin tallennus epäonnistui.";
        return false;
    }
    f.close();
    QFile::setPermissions(chunkPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    // A file can remain orphaned on failure, but no partial normalized piece is committed.
    if (!run("BEGIN IMMEDIATE")) return false;
    bool ok = ingest(parsedReadings, parsedAmounts, from, to, kind == "amounts" && authoritative, true);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (ok)
        ok = run("INSERT INTO import_chunks VALUES(?,?,?,?,?,?,?,?,?)",
                 {id, runId, kind, from, to, chunkPath, hash, parsedReadings.size() + parsedAmounts.size(),
                  QString::fromUtf8(QJsonDocument(mappings()).toJson(QJsonDocument::Compact))});
    if (ok) ok = run("COMMIT");
    if (!ok) run("ROLLBACK");
    return ok;
}

void Store::endImport(const QString& runId, bool success, const QString& message) {
    run("UPDATE import_runs SET finished=?,status=?,error=? WHERE id=?",
        {QDateTime::currentMSecsSinceEpoch(), success ? "complete" : "partial", message, runId});
    if (success) setMeta("lastCompleteImport", QString::number(QDateTime::currentMSecsSinceEpoch()));
}
QJsonArray Store::imports() const {
    return query("SELECT r.*, (SELECT count(*) FROM import_chunks c WHERE c.run_id=r.id) AS chunks FROM import_runs r ORDER BY started DESC LIMIT 50");
}
bool Store::exclude(const QString& id, const QString& reason) {
    if (query("SELECT id FROM amounts WHERE id=?", {id}).isEmpty()) return false;
    bool ok = reason.trimmed().isEmpty()
                  ? run("DELETE FROM analysis_exclusions WHERE id=?", {id})
                  : run("INSERT INTO analysis_exclusions VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET reason=excluded.reason,time=excluded.time",
                        {id, reason.trimmed(), QDateTime::currentMSecsSinceEpoch()});
    return ok && setMeta("revision", QUuid::createUuid().toString());
}
QJsonArray Store::exclusions() const {
    return query("SELECT * FROM analysis_exclusions");
}
bool Store::confirmDay(const QString& date, bool complete) {
    if (!QDate::fromString(date, Qt::ISODate).isValid()) return false;
    const auto zone = QTimeZone(meta("analysisZone").isEmpty() ? "Europe/Helsinki" : meta("analysisZone").toUtf8());
    const auto day = QDate::fromString(date, Qt::ISODate);
    const qint64 from = QDateTime(day, QTime(0, 0), zone).toMSecsSinceEpoch(), to = QDateTime(day.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument(QJsonObject{{"zone", QString::fromUtf8(zone.id())}, {"from", double(from)}, {"to", double(to)}, {"events", amounts(from, to, true)}})
                .toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256)
            .toHex());
    return complete ? run("INSERT INTO complete_days VALUES(?,?) ON CONFLICT(date) DO UPDATE SET revision=excluded.revision", {date, hash})
                    : run("DELETE FROM complete_days WHERE date=?", {date});
}
QSet<QString> Store::completeDays() const {
    QSet<QString> out;
    for (auto v : query("SELECT date,revision FROM complete_days")) {
        auto o = v.toObject();
        const auto day = QDate::fromString(o["date"].toString(), Qt::ISODate);
        const auto zone = QTimeZone(meta("analysisZone").isEmpty() ? "Europe/Helsinki" : meta("analysisZone").toUtf8());
        auto a = QDateTime(day, QTime(0, 0), zone).toMSecsSinceEpoch(), b = QDateTime(day.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();
        auto hash = QString::fromLatin1(
            QCryptographicHash::hash(
                QJsonDocument(QJsonObject{{"zone", QString::fromUtf8(zone.id())}, {"from", double(a)}, {"to", double(b)}, {"events", amounts(a, b, true)}})
                    .toJson(QJsonDocument::Compact),
                QCryptographicHash::Sha256)
                .toHex());
        if (hash == o["revision"].toString()) out.insert(o["date"].toString());
    }
    return out;
}
QJsonObject Store::profileAt(qint64 time) const {
    for (auto v : profiles()) {
        auto row = v.toObject(), p = object(row["data"]);
        if (p["approved"].toBool() && p["effectiveFrom"].toDouble() <= time) {
            p["version"] = row["version"];
            return p;
        }
    }
    return {};
}
QJsonObject Store::responseProfileAt(qint64 time) const {
    for (auto v : profiles()) {
        auto row = v.toObject(), p = object(row["data"]);
        if ((p["approved"].toBool() || p["responseApproved"].toBool()) && p["effectiveFrom"].toDouble() <= time) {
            p["version"] = row["version"];
            return p;
        }
    }
    return {};
}

} // namespace Studio

namespace Studio {
static QString artifactHash(const Store& s, qint64 from, qint64 to) {
    QJsonArray rows;
    for (auto r : s.readings(from, to))
        rows << QJsonObject{{"id", r.key},     {"time", double(r.time)}, {"sensor", r.sensor},
                            {"value", r.mmol}, {"reliable", r.reliable}, {"calibration", r.calibration}};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(rows).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QSet<QString> Store::artifactKeys(qint64 from, qint64 to) const {
    QSet<QString> keys;
    for (auto v : query("SELECT * FROM glucose_artifacts WHERE active=1 AND from_time<? AND to_time>?", {to, from})) {
        auto a = v.toObject();
        auto start = qint64(a["from_time"].toDouble()), end = qint64(a["to_time"].toDouble());
        if (a["source_hash"].toString() != artifactHash(*this, start, end)) continue;
        for (auto r : readings(std::max(start, from), std::min(end, to))) keys.insert(r.key);
    }
    return keys;
}
QJsonArray Store::artifactReviews() const {
    auto rows = query("SELECT * FROM glucose_artifacts ORDER BY created DESC");
    for (int i = 0; i < rows.size(); i++) {
        auto a = rows[i].toObject();
        a["current"] =
            a["active"].toInt() && a["source_hash"].toString() == artifactHash(*this, qint64(a["from_time"].toDouble()), qint64(a["to_time"].toDouble()));
        a.remove("source_hash");
        rows[i] = a;
    }
    return rows;
}
bool Store::reviewArtifact(qint64 from, qint64 to, const QString& reason, const QString& evidence, const QString& reviewer) {
    if (to <= from || reason.trimmed().isEmpty() || evidence.trimmed().isEmpty() || reviewer.trimmed().isEmpty() || readings(from, to).isEmpty()) return false;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!run("BEGIN IMMEDIATE")) return false;
    bool ok = run("INSERT INTO glucose_artifacts VALUES(?,?,?,?,?,?,?,?,1)",
                  {id, from, to, reason.trimmed(), evidence.trimmed(), reviewer.trimmed(), artifactHash(*this, from, to), now});
    if (ok) ok = run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)", {now, "glucose-artifact-review", json({{"id", id}, {"reviewer", reviewer.trimmed()}})});
    if (ok) ok = setMeta("revision", QUuid::createUuid().toString());
    if (ok) ok = run("COMMIT");
    if (!ok) run("ROLLBACK");
    return ok;
}
bool Store::deactivateArtifact(const QString& id) {
    if (query("SELECT id FROM glucose_artifacts WHERE id=? AND active=1", {id}).isEmpty() || !run("BEGIN IMMEDIATE")) return false;
    bool ok = run("UPDATE glucose_artifacts SET active=0 WHERE id=?", {id});
    if (ok)
        ok = run("INSERT INTO audit(time,kind,data) VALUES(?,?,?)", {QDateTime::currentMSecsSinceEpoch(), "glucose-artifact-deactivate", json({{"id", id}})});
    if (ok) ok = setMeta("revision", QUuid::createUuid().toString());
    if (ok) ok = run("COMMIT");
    if (!ok) run("ROLLBACK");
    return ok;
}
} // namespace Studio
