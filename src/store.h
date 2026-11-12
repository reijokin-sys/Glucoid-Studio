// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "domain.h"
#include <sqlite3.h>
#include <QSet>
namespace Studio {
class Store {
  public:
    explicit Store(const QString& path, bool readOnly = false);
    ~Store();
    Store(const Store&) = delete;
    QString error() const { return m_error; }
    // Initialization state is independent of the last operation's error.
    bool available() const { return db && m_available; }
    // Snapshot readers also use this to detect any error in their read chain.
    bool good() const { return db && m_error.isEmpty(); }
    QString path() const { return filename; }
    bool switchDatabase(const QString& path);
    bool importNightscoutPage(const QString& runId, const QString& collection, const QByteArray& bytes, qint64 cursor);
    bool reconcileNightscout(const QString& collection, qint64 from, const QSet<QString>& seen);
    // reconcileFrom limits authoritative deactivation (default: the whole
    // requested window). A normal update passes the correction window so an
    // incremental export cannot retire older history it no longer carries.
    bool ingest(const QList<Reading>& readings, const QList<Amount>& amounts, qint64 from, qint64 to, bool authoritative, bool allowEmpty = false,
                qint64 reconcileFrom = -1);
    QList<Reading> readings(qint64 from, qint64 to, const QString& kind = "stream") const;
    QJsonArray amounts(qint64 from, qint64 to, bool includeDeleted = false) const;
    QJsonArray mappings() const;
    bool mapLabel(const QString& label, const QString& kind, const QString& medicine);
    QJsonObject observationProfile() const;
    QString revision() const;
    QString beginImport(qint64 from, qint64 to, const QString& transport);
    bool importChunk(const QString& runId, const QString& kind, const QByteArray& bytes, qint64 from, qint64 to, bool authoritative = true,
                     const QByteArray& sourceClasses = {}, qint64 reconcileFrom = -1);
    bool endImport(const QString& runId, bool success, const QString& message);
    QJsonArray imports() const;

    QString meta(const QString& key) const;
    bool setMeta(const QString& key, const QString& value);
    bool purgeRetiredComparison();
    QJsonArray query(const QString& sql, const QVariantList& args = {}) const;
    bool run(const QString& sql, const QVariantList& args = {});

  private:
    sqlite3* db = nullptr;
    bool m_available = false;
    mutable QString m_error;
    QString filename;
};
} // namespace Studio
