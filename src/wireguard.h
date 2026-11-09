// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
class QAbstractSocket;
namespace Studio {
QString wireguardNameError(const QString &name);
QString bindWireguardSocket(QAbstractSocket &socket, const QString &name, bool ipv6);
QString verifyWireguardSocket(const QAbstractSocket &socket, const QString &name);
}
