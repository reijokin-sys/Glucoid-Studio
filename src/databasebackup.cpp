// SPDX-License-Identifier: GPL-3.0-or-later
#include "databasebackup.h"
#include "nightscout.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <sqlite3.h>
namespace Studio {
namespace {
bool integrityCheck(sqlite3* db, QString& error) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &stmt, nullptr) != SQLITE_OK) {
        error = QCoreApplication::translate("Studio", "Tietokannan eheystarkistus epäonnistui.");
        return false;
    }
    bool ok = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto value = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        ok = value == "ok";
        if (!ok) break;
    }
    sqlite3_finalize(stmt);
    if (!ok) error = QCoreApplication::translate("Studio", "Varmuuskopion tietokanta ei läpäissyt eheystarkistusta.");
    return ok;
}
bool execSql(sqlite3* db, const char* sql, QString& error) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) == SQLITE_OK) return true;
    sqlite3_free(message);
    error = QCoreApplication::translate("Studio", "Tietokantakomennon suorittaminen epäonnistui.");
    return false;
}

bool copyDatabase(const QString& sourcePath, const QString& targetPath, QString& error) {
    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    if (sqlite3_open_v2(sourcePath.toUtf8(), &source, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        if (source) sqlite3_close(source);
        error = QCoreApplication::translate("Studio", "Varmuuskopion tietokannan lukeminen epäonnistui.");
        return false;
    }
    if (!integrityCheck(source, error)) {
        sqlite3_close(source);
        return false;
    }
    if (sqlite3_open_v2(targetPath.toUtf8(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        if (target) sqlite3_close(target);
        sqlite3_close(source);
        error = QCoreApplication::translate("Studio", "Palautetun tietokannan luominen epäonnistui.");
        return false;
    }
    sqlite3_backup* copy = sqlite3_backup_init(target, "main", source, "main");
    int code = copy ? sqlite3_backup_step(copy, -1) : SQLITE_ERROR;
    if (copy) sqlite3_backup_finish(copy);
    bool ok = code == SQLITE_DONE && integrityCheck(target, error);
    sqlite3_close(target);
    sqlite3_close(source);
    if (!ok && QFileInfo::exists(targetPath)) QFile::remove(targetPath);
    if (ok) QFile::setPermissions(targetPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return ok;
}
bool restoreDatabaseFile(const QString& staged, const QString& finalPath, QString& error) {
    const QString backupDir = QFileInfo(finalPath).absolutePath() + "/backups";
    if (!QDir().mkpath(backupDir)) {
        error = QCoreApplication::translate("Studio", "Varmuuskopiohakemiston luominen epäonnistui.");
        return false;
    }
    QFile::setPermissions(backupDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString prefix = backupDir + "/pre-restore-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
                           QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QStringList moved;
    auto rollback = [&] {
        for (auto it = moved.crbegin(); it != moved.crend(); ++it) {
            const auto parts = it->split('\n');
            if (parts.size() == 2) QFile::rename(parts[1], parts[0]);
        }
    };
    auto moveAside = [&](const QString& from, const QString& to) {
        if (!QFileInfo::exists(from) && !QFileInfo(from).isSymLink()) return true;
        if (!QFile::rename(from, to)) {
            error = QCoreApplication::translate("Studio", "Nykyisen tietokannan siirtäminen turvakopioksi epäonnistui.");
            return false;
        }
        moved.append(from + "\n" + to);
        return true;
    };
    const QString oldDatabase = prefix + ".sqlite3";
    if (!moveAside(finalPath, oldDatabase) || !moveAside(finalPath + "-wal", prefix + "-wal") || !moveAside(finalPath + "-shm", prefix + "-shm")) {
        rollback();
        return false;
    }
    if (!QDir().mkpath(QFileInfo(finalPath).absolutePath())) {
        error = QCoreApplication::translate("Studio", "Tietohakemiston luominen epäonnistui.");
        rollback();
        return false;
    }
    if (!QFile::rename(staged, finalPath)) {
        rollback();
        error = QCoreApplication::translate("Studio", "Palautetun tietokannan käyttöönotto epäonnistui.");
        return false;
    }
    QFile::setPermissions(finalPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}
} // namespace
BackupInfo inspectBackup(const QString& path) {
    BackupInfo result;
    result.path = QFileInfo(path).absoluteFilePath();
    sqlite3* db = nullptr;
    const QFileInfo input(path);
    if (input.isSymLink() || !input.isFile() || QFileInfo::exists(path + ".manifest.json") ||
        sqlite3_open_v2(path.toUtf8(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        result.error = QCoreApplication::translate("Studio", "Vain uuden mallin tietokantavarmuuskopio voidaan palauttaa.");
        return result;
    }
    sqlite3_stmt* statement = nullptr;
    QJsonObject metadata;
    if (sqlite3_prepare_v2(db, "SELECT key,value FROM meta WHERE key IN ('schema','portableBackupFormat','portableBackupSource')", -1, &statement, nullptr) ==
        SQLITE_OK)
        while (sqlite3_step(statement) == SQLITE_ROW)
            metadata[QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)))] =
                QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)));
    sqlite3_finalize(statement);
    result.connectionSource = metadata["portableBackupSource"].toString();
    result.complete = metadata["schema"].toString() == "2" && metadata["portableBackupFormat"].toString() == "1" &&
                      (result.connectionSource == "juggluco" || result.connectionSource == "nightscout") && integrityCheck(db, result.error);
    for (const auto* sql : {"SELECT id,time,mmol FROM glucose LIMIT 0", "SELECT id,time,value FROM amounts LIMIT 0", "SELECT path,sha256 FROM import_chunks LIMIT 0"}) {
        sqlite3_stmt* required = nullptr;
        if (sqlite3_prepare_v2(db,sql,-1,&required,nullptr) != SQLITE_OK) result.complete = false;
        sqlite3_finalize(required);
    }
    sqlite3_close(db);
    if (!result.complete && result.error.isEmpty())
        result.error = QCoreApplication::translate("Studio", "Vain uuden mallin tietokantavarmuuskopio voidaan palauttaa.");
    return result;
}
bool exportDatabaseBackup(const QString& source, const QString& destination, const QString& sourceKind, QString* error) {
    QString message;
    auto fail = [&](const QString& reason) {
        if (error) *error = reason;
        return false;
    };
    const QFileInfo target(destination), input(source);
    if (destination.isEmpty() || !QDir::isAbsolutePath(destination) || target.isSymLink() ||
        (!target.canonicalFilePath().isEmpty() && target.canonicalFilePath() == input.canonicalFilePath()) || destination == source + "-wal" ||
        destination == source + "-shm" || (sourceKind != "juggluco" && sourceKind != "nightscout"))
        return fail(QCoreApplication::translate("Studio", "Varmuuskopion kohde ei kelpaa. Valitse erillinen tiedosto."));
    // Do not replace an active SQLite database or leave stale companions.
    if (QFileInfo::exists(destination + "-wal") || QFileInfo::exists(destination + "-shm") || QFileInfo::exists(destination + ".manifest.json"))
        return fail(QCoreApplication::translate("Studio", "Kohteeseen liittyy tietokannan lisätiedostoja. Valitse uusi tiedostonimi."));
    QTemporaryFile staged(target.absolutePath() + "/.glucoid-backup-XXXXXX");
    if (!staged.open()) return fail(QCoreApplication::translate("Studio", "Tallennuskohteeseen ei voi kirjoittaa."));
    const auto stagedPath = staged.fileName();
    staged.close();
    if (!copyDatabase(source, stagedPath, message)) return fail(message);
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(stagedPath.toUtf8(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return fail(QCoreApplication::translate("Studio", "Varmuuskopion avaaminen epäonnistui."));
    }
    const QByteArray metadata =
        sourceKind == "nightscout"
            ? "INSERT INTO meta(key,value) VALUES('portableBackupSource','nightscout') ON CONFLICT(key) DO UPDATE SET value=excluded.value"
            : "INSERT INTO meta(key,value) VALUES('portableBackupSource','juggluco') ON CONFLICT(key) DO UPDATE SET value=excluded.value";
    bool ok = execSql(db, "PRAGMA journal_mode=DELETE", message) && execSql(db, metadata.constData(), message) &&
              execSql(db, "INSERT INTO meta(key,value) VALUES('portableBackupFormat','1') ON CONFLICT(key) DO UPDATE SET value='1'", message) &&
              integrityCheck(db, message);
    sqlite3_close(db);
    if (!ok) return fail(message);
    const auto inspected = inspectBackup(stagedPath);
    if (!inspected.complete) return fail(inspected.error);
    QFile content(stagedPath);
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!content.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return fail(QCoreApplication::translate("Studio", "Tallennuskohteeseen ei voi kirjoittaa."));
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    while (!content.atEnd()) {
        const auto bytes = content.read(1024 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            return fail(QCoreApplication::translate("Studio", "Varmuuskopion kirjoittaminen epäonnistui."));
    }
    if (!output.commit()) return fail(QCoreApplication::translate("Studio", "Varmuuskopion tallentaminen epäonnistui."));
    return true;
}
BackupRestoreResult restoreBackup(const QString& path, const QString& targetRoot, bool replaceExisting) {
    BackupRestoreResult result;
    const auto info = inspectBackup(path);
    if (!info.complete) {
        result.error = info.error;
        return result;
    }
    QFile connectionFile(targetRoot + "/connection.json");
    QJsonObject current;
    if (connectionFile.open(QIODevice::ReadOnly)) current = QJsonDocument::fromJson(connectionFile.readAll()).object();
    const auto source = current.value("source").toString("juggluco");
    if (source != info.connectionSource || (source == "nightscout" && current.value("address").toString().isEmpty())) {
        result.error = QCoreApplication::translate("Studio", "Valitse varmuuskopion tietolähde asetuksista ennen palautusta.");
        return result;
    }
    const auto root = QFileInfo(targetRoot).absoluteFilePath();
    const auto finalDatabase = source == "nightscout" ? nightscoutDatasetPath(root, current["address"].toString()) : root + "/studio.sqlite3";
    if (QFileInfo(finalDatabase).isSymLink() || (QFileInfo::exists(finalDatabase) && !replaceExisting)) {
        result.error = QCoreApplication::translate("Studio", "Kohdehakemistossa on jo tietokanta. Valitse uusi hakemisto.");
        return result;
    }
    if (!QDir().mkpath(root)) {
        result.error = QCoreApplication::translate("Studio", "Palautushakemiston luominen epäonnistui.");
        return result;
    }
    QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const auto staged = root + "/.restore-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".sqlite3";
    if (!copyDatabase(info.path, staged, result.error) || !restoreDatabaseFile(staged, finalDatabase, result.error)) {
        QFile::remove(staged);
        return result;
    }
    result.ok = true;
    result.dataPath = finalDatabase;
    return result;
}
QString restoreRequestPath(const QString& root) {
    return root + "/restore.pending";
}

bool writeRestoreRequest(const QString& root, const QString& backupPath, QString* error) {
    QSaveFile file(restoreRequestPath(root));
    if (!QDir().mkpath(root) || !file.open(QIODevice::WriteOnly)) {
        if (error) *error = QCoreApplication::translate("Studio", "Palautuspyynnön tallentaminen epäonnistui.");
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes =
        QJsonDocument(QJsonObject{{"backup", QFileInfo(backupPath).absoluteFilePath()}, {"requested", double(QDateTime::currentMSecsSinceEpoch())}}).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QCoreApplication::translate("Studio", "Palautuspyynnön tallentaminen epäonnistui.");
        return false;
    }
    return true;
}

QString readRestoreRequest(const QString& root) {
    const QFileInfo request(restoreRequestPath(root));
    if (request.isSymLink() || !request.isFile()) return {};
    QFile file(request.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().value("backup").toString();
}

void clearRestoreRequest(const QString& root) {
    QFile::remove(restoreRequestPath(root));
}
} // namespace Studio
