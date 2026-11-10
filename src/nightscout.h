// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
#include "syncpolicy.h"
#include <QNetworkAccessManager>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QPointer>
#include <functional>

namespace Studio {
QString nightscoutAddressError(const QString& address);
QString nightscoutDatasetPath(const QString& root, const QString& address);
// First import covers about six months. "Check full history" uses the
// Nightscout minimum timestamp instead.
inline constexpr qint64 nightscoutInitialHistoryMs = initialImportHistoryMs;

class NightscoutImporter : public QObject {
    Q_OBJECT
  public:
    NightscoutImporter(Store& store, QString address, QString accessToken, QObject* parent = nullptr, qint64 initialHistoryMs = nightscoutInitialHistoryMs);
    ~NightscoutImporter() override;
    void start();
    void setBackgroundWrites(bool enabled) { backgroundWrites = enabled; }
  signals:
    void progress(const QString& message);
    void finished(bool success, const QString& message);

  private:
    bool backgroundWrites = false;
    QFutureWatcher<QPair<bool, QString>> writeWatcher;
    void storePage(const QByteArray& bytes, const QJsonArray& rows, qint64 next);
    void pageStored(const QJsonArray& rows, qint64 next, bool ok, const QString& error);
    Store& db;
    QString address, accessToken, jwt, run, collection;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> reply;
    qint64 cursor = 0;
    qint64 collectionFrom = 0;
    qint64 initialHistoryMs = nightscoutInitialHistoryMs;
    QSet<QString> seen;
    int limit = 1000, pages = 0, retries = 0;
    bool complete = false, fullHistory = false;
    void request(const QString& relative, bool authorization, std::function<void(QByteArray)> success);
    void beginCollection(const QString& name);
    void nextPage();
    void finish(bool success, const QString& message);
};
} // namespace Studio
