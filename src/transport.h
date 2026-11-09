// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
namespace Studio {
struct Connection {
    QString mode = "usb", address, secret, auth = "apisecret", serial;
    QString source = "juggluco";
    QString wireguardInterface = "wg0";
};
Connection glucoidConnection();
class Transport : public QObject {
    Q_OBJECT
  public:
    explicit Transport(QObject* parent = nullptr);
    void get(const Connection& connection, const QString& path, std::function<void(QByteArray, QString)> done);
    bool busy = false;

  private:
    QNetworkAccessManager network;
};
} // namespace Studio
