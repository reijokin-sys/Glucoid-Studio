// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "domain.h"
#include <sqlite3.h>
#include <QSet>
namespace Studio {
class Store {
public:
    explicit Store(const QString &path,bool readOnly=false);
    ~Store();
    Store(const Store&)=delete;
    QString error() const { return m_error; }
    // Initialization state is independent of the last operation's error.
    bool available() const { return db && m_available; }
    // Snapshot readers also use this to detect any error in their read chain.
    bool good() const { return db && m_error.isEmpty(); }
    QString path() const { return filename; }
    bool switchDatabase(const QString &path);
    bool importNightscoutPage(const QString &run, const QString &collection,
        const QByteArray &bytes, qint64 cursor);
    bool reconcileNightscout(const QString &collection,qint64 from,const QSet<QString> &seen);
    bool ingest(const QList<Reading> &readings,const QList<Amount> &amounts,qint64 from,qint64 to,bool authoritative,bool allowEmpty=false);
    QList<Reading> readings(qint64 from,qint64 to,const QString &kind="stream") const;
    QJsonArray amounts(qint64 from,qint64 to,bool includeDeleted=false) const;
    QJsonArray calculationAmounts(qint64 from,qint64 to) const;
    bool setDeleted(const QString &id,bool deleted);
    QJsonArray mappings() const;
    bool mapLabel(const QString &label,const QString &kind,const QString &medicine);
    QString addLocal(qint64 time,double value,const QString &kind,const QString &medicine,const QString &purpose);
    bool annotate(const QString &id,const QJsonObject &annotation);
    QString saveEpisode(QJsonObject episode);
    QJsonArray episodes() const;
    QJsonObject activeProfile() const;
    QJsonObject observationProfile() const;
    int saveProfile(QJsonObject profile);
    QJsonArray profiles() const;
    bool saveCalculation(const QString &kind,const QJsonObject &result);
    QString revision() const;
    QString beginImport(qint64 from,qint64 to,const QString &transport);
    bool importChunk(const QString &run,const QString &kind,const QByteArray &bytes,qint64 from,qint64 to,bool authoritative=true);
    void endImport(const QString &run,bool success,const QString &error);
    QJsonArray imports() const;
    bool exclude(const QString &id,const QString &reason);
    QJsonArray exclusions() const;
    bool confirmDay(const QString &date,bool complete);
    QSet<QString> completeDays() const;
    QSet<QString> artifactKeys(qint64 from,qint64 to)const;
    QJsonArray artifactReviews()const;
    bool reviewArtifact(qint64 from,qint64 to,const QString &reason,const QString &evidence,const QString &reviewer);
    bool deactivateArtifact(const QString&id);
    QJsonObject profileAt(qint64 time) const;
    QJsonObject responseProfileAt(qint64 time) const;

    QString meta(const QString &key) const;
    bool setMeta(const QString &key,const QString &value);
    bool backup(const QString &target);
    bool purgeRetiredComparison();
    QJsonArray query(const QString &sql,const QVariantList &args={}) const;
    bool run(const QString &sql,const QVariantList &args={});
private:
    sqlite3 *db=nullptr;
    bool m_available=false;
    mutable QString m_error;
    QString filename;
};
}
