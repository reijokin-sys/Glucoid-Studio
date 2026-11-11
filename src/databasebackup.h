// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace Studio {
struct BackupInfo {
    QString path, connectionSource, error;
    bool complete = false;
};
struct BackupRestoreResult {
    bool ok = false;
    QString dataPath, error;
};
BackupInfo inspectBackup(const QString& path);
bool exportDatabaseBackup(const QString& source, const QString& destination, const QString& sourceKind, QString* error = nullptr);
BackupRestoreResult restoreBackup(const QString& path, const QString& targetRoot, bool replaceExisting);
QString restoreRequestPath(const QString& root);
bool writeRestoreRequest(const QString& root, const QString& path, QString* error = nullptr);
QString readRestoreRequest(const QString& root);
void clearRestoreRequest(const QString& root);
} // namespace Studio
