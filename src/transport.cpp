// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport.h"
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QNetworkReply>
#include <QSslSocket>
#include <QUrl>
#include <QRegularExpression>

namespace Studio {
Connection glucoidConnection() {
    QFile f(QDir::homePath() + "/.config/plasma-org.kde.plasma.desktop-appletsrc");
    Connection c;
    if (!f.open(QIODevice::ReadOnly)) return c;
    QMap<QString, QMap<QString, QString>> sections;
    QString current;
    for (const auto& line : QString::fromUtf8(f.readAll()).split('\n')) {
        if (line.startsWith('['))
            current = line;
        else {
            const int pos = line.indexOf('=');
            if (pos > 0) sections[current][line.left(pos)] = line.mid(pos + 1);
        }
    }
    for (auto it = sections.cbegin(); it != sections.cend(); ++it)
        if (it.value().value("plugin") == "fi.reijo.glucoid") {
            const auto v = sections.value(it.key() + "[Configuration][General]");
            c.address = v.value("url");
            c.secret = v.value("token");
            c.auth = v.value("authMode", "token");
            break;
        }
    return c;
}
} // namespace Studio
