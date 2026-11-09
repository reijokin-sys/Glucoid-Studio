// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport.h"
#include "wireguard.h"
#include <QHostAddress>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QNetworkReply>
#include <QSslSocket>
#include <QUrl>
#include <QRegularExpression>

namespace Studio {
Transport::Transport(QObject* parent) : QObject(parent) {}
void Transport::get(const Connection& connection, const QString& path, std::function<void(QByteArray, QString)> done) {
    constexpr qsizetype limit = 64 * 1024 * 1024;
    if (busy) {
        done({}, "Tiedonsiirto on jo käynnissä.");
        return;
    }
    if (!path.startsWith("/x/") || path.contains('\r') || path.contains('\n')) {
        done({}, "Virheellinen vientipolku.");
        return;
    }
    if (connection.auth != "none" && connection.secret.isEmpty()) {
        done({}, "Avainrengas on lukittu tai yhteystunnus puuttuu.");
        return;
    }
    const QByteArray auth = connection.auth == "none" ? QByteArray() : QCryptographicHash::hash(connection.secret.toUtf8(), QCryptographicHash::Sha1).toHex();
    busy = true;
    if (connection.mode == "usb") {
        auto* process = new QProcess(this);
        auto* timer = new QTimer(process);
        timer->setSingleShot(true);
        auto bytes = std::make_shared<QByteArray>();
        auto timedOut = std::make_shared<bool>(false);
        QStringList args;
        if (!connection.serial.isEmpty()) args << "-s" << connection.serial;
        args << "shell" << "toybox" << "nc" << "-w" << "20" << "127.0.0.1" << "17580";
        connect(timer, &QTimer::timeout, process, [process, timedOut] {
            *timedOut = true;
            process->kill();
        });
        connect(process, &QProcess::readyReadStandardOutput, process, [process, bytes, timedOut] {
            bytes->append(process->readAllStandardOutput());
            if (bytes->size() > limit) {
                *timedOut = true;
                process->kill();
            }
        });
        connect(process, &QProcess::started, process, [process, path, auth] {
            QByteArray request = "GET " + path.toUtf8() + " HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n";
            if (!auth.isEmpty()) request += "api-secret: " + auth + "\r\n";
            request += "\r\n";
            process->write(request);
            process->closeWriteChannel();
        });
        auto finished = std::make_shared<bool>(false);
        auto finish = [this, process, timer, bytes, timedOut, finished, done](int code) {
            if (*finished) return;
            *finished = true;
            timer->stop();
            busy = false;
            bytes->append(process->readAllStandardOutput());
            process->deleteLater();
            if (*timedOut || code != 0) {
                done({}, "USB-siirto epäonnistui tai aikakatkaistiin. Tarkista yhteys.");
                return;
            }
            const int split = bytes->indexOf("\r\n\r\n");
            if (split < 0) {
                done({}, "USB-lähde ei palauttanut HTTP-vastausta.");
                return;
            }
            const auto headers = bytes->left(split).split('\n');
            const auto status = headers.value(0).trimmed().split(' ');
            auto body = bytes->mid(split + 4);
            if (status.size() > 1 && status[1] == "413") {
                done({}, "HTTP 413");
                return;
            }
            if (status.size() < 2 || status[1] != "200") {
                done({}, "Juggluco hylkäsi USB-haun. Tarkista tunnistautuminen ja palvelin.");
                return;
            }
            qint64 length = -1;
            bool chunked = false;
            for (auto h : headers) {
                h = h.trimmed().toLower();
                if (h.startsWith("content-length:")) {
                    bool ok = false;
                    length = h.mid(15).trimmed().toLongLong(&ok);
                    if (!ok) length = -2;
                }
                if (h.startsWith("transfer-encoding:")) chunked = true;
            }
            if (chunked || length < 0 || body.size() != length || body.size() > limit) {
                done({}, "Vientivastaus on vajaa tai sen kehystys ei ole tuettu.");
                return;
            }
            done(body, {});
        };
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
                [finish](int c, QProcess::ExitStatus s) { finish(s == QProcess::NormalExit ? c : -1); });
        connect(process, &QProcess::errorOccurred, process, [finish](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) finish(-1);
        });
        process->setProgram("adb");
        process->setArguments(args);
        process->start();
        timer->start(25000);
        return;
    }
    const bool guarded = connection.mode == "wireguard";
    const QUrl base(connection.address);
    if (!base.isValid() || (base.scheme() != "https" && base.scheme() != "http") || !base.userInfo().isEmpty() || base.hasQuery() || base.hasFragment() ||
        (!base.path().isEmpty() && base.path() != "/")) {
        busy = false;
        done({}, "Anna palvelimen http/https-juuriosoite ilman tunnuksia tai kyselyparametreja.");
        return;
    }
    // Raw HTTP/1.0 request over a socket: the phone's server compares the api-secret
    // header name case-sensitively, and Qt's HTTP stack sends it as "Api-Secret".
    const QUrl url = base.resolved(QUrl(path));
    const bool tls = url.scheme() == "https";
    QHostAddress destination;
    if (guarded && (!destination.setAddress(url.host()) || !destination.scopeId().isEmpty())) {
        busy = false;
        done({}, "WireGuard-yhteys tarvitsee puhelimen numeerisen tunneliosoitteen.");
        return;
    }
    const quint16 port = quint16(url.port(tls ? 443 : 80));
    QTcpSocket* socket = tls ? new QSslSocket(this) : new QTcpSocket(this);
    if (guarded) {
        const auto error = bindWireguardSocket(*socket, connection.wireguardInterface, destination.protocol() == QAbstractSocket::IPv6Protocol);
        if (!error.isEmpty()) {
            socket->deleteLater();
            busy = false;
            done({}, error);
            return;
        }
    }
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    auto bytes = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, socket, timer, bytes, finished, done](const QString& error) {
        if (*finished) return;
        *finished = true;
        timer->stop();
        busy = false;
        socket->deleteLater();
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        const int split = bytes->indexOf("\r\n\r\n");
        if (split < 0) {
            done({}, "Palvelin ei palauttanut HTTP-vastausta.");
            return;
        }
        const auto headers = bytes->left(split).split('\n');
        const auto status = headers.value(0).trimmed().split(' ');
        auto body = bytes->mid(split + 4);
        if (status.size() < 2 || status[1] != "200") {
            done({}, status.value(1) == "413" ? QString("HTTP 413") : QString("Palvelin hylkäsi haun (HTTP %1).").arg(status.value(1).toInt()));
            return;
        }
        qint64 length = -1;
        bool chunked = false;
        for (auto h : headers) {
            h = h.trimmed().toLower();
            if (h.startsWith("content-length:")) {
                bool ok = false;
                length = h.mid(15).trimmed().toLongLong(&ok);
                if (!ok) length = -2;
            }
            if (h.startsWith("transfer-encoding:")) chunked = true;
        }
        if (chunked || length < 0 || body.size() != length || body.size() > limit) {
            done({}, "Vastaus on vajaa tai sen kehystys ei ole tuettu.");
            return;
        }
        done(body, {});
    };
    auto send = [socket, url, auth, guarded, name = connection.wireguardInterface, finish] {
        if (guarded) {
            const auto error = verifyWireguardSocket(*socket, name);
            if (!error.isEmpty()) {
                finish(error);
                socket->abort();
                return;
            }
        }
        QByteArray target = url.path(QUrl::FullyEncoded).toUtf8();
        if (url.hasQuery()) target += "?" + url.query(QUrl::FullyEncoded).toUtf8();
        QByteArray host = url.host().toUtf8();
        if (url.port() > 0) host += ":" + QByteArray::number(url.port());
        QByteArray request = "GET " + target + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n";
        if (!auth.isEmpty()) request += "api-secret: " + auth + "\r\n";
        request += "\r\n";
        socket->write(request);
    };
    connect(socket, &QTcpSocket::readyRead, socket, [socket, bytes, finish] {
        bytes->append(socket->readAll());
        if (bytes->size() > 64 * 1024 * 1024) {
            finish("Vastaus ylittää kokorajan.");
            socket->abort();
        }
    });
    connect(socket, &QTcpSocket::disconnected, socket, [finish] { finish({}); });
    connect(socket, &QTcpSocket::errorOccurred, socket, [finish](QAbstractSocket::SocketError e) {
        // The server closes the connection after the response; that is not a failure.
        if (e == QAbstractSocket::RemoteHostClosedError) return;
        finish("Verkkohaku epäonnistui. Tarkista vaadittu yhteys, palvelin ja tunnistautuminen.");
    });
    connect(timer, &QTimer::timeout, socket, [socket, finish] {
        socket->abort();
        finish("Verkkohaku aikakatkaistiin.");
    });
    if (tls) {
        auto* ssl = static_cast<QSslSocket*>(socket);
        connect(ssl, &QSslSocket::encrypted, ssl, [send] { send(); });
        ssl->connectToHostEncrypted(url.host(), port);
    } else {
        connect(socket, &QTcpSocket::connected, socket, [send] { send(); });
        socket->connectToHost(url.host(), port);
    }
    timer->start(20000);
}
} // namespace Studio
