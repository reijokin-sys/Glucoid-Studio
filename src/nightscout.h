// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <functional>

namespace Studio {
QString nightscoutAddressError(const QString &address);
QString nightscoutDatasetPath(const QString &root, const QString &address);

class NightscoutImporter : public QObject {
    Q_OBJECT
public:
    NightscoutImporter(Store &store, QString address, QString accessToken, QObject *parent=nullptr);
    ~NightscoutImporter() override;
    void start();
signals:
    void progress(const QString &message);
    void finished(bool success, const QString &message);
private:
    Store &db;
    QString address, accessToken, jwt, run, collection;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> reply;
    qint64 cursor=0;
    qint64 collectionFrom=0;
    QSet<QString> seen;
    int limit=1000, pages=0, retries=0;
    bool complete=false;
    void request(const QString &relative, bool authorization,
        std::function<void(QByteArray)> success);
    void beginCollection(const QString &name);
    void nextPage();
    void finish(bool success, const QString &message);
};
}
