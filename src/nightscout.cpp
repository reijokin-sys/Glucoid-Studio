// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "nightscout.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <memory>

namespace Studio {
// Nightscout API v3 rejects history timestamps before 2000-01-01 with
// "Bad or missing Last-Modified header/parameter". The first import must use
// a valid lower bound instead of 0.
static constexpr qint64 nightscoutMinTimestamp = 946684800000LL;
static QUrl baseUrl(const QString& address) {
    QUrl url(address);
    if ((url.scheme() == "https" && url.port() == 443) || (url.scheme() == "http" && url.port() == 80)) url.setPort(-1);
    if (!url.path().endsWith('/')) url.setPath(url.path() + '/');
    return url;
}

QString nightscoutAddressError(const QString& address) {
    const QUrl url(address);
    const bool loopback = url.host() == "127.0.0.1" || url.host() == "localhost" || url.host() == "::1";
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (url.scheme() != "https" && !(url.scheme() == "http" && loopback)))
        return QCoreApplication::translate("Studio", "Anna Nightscoutin HTTPS-osoite ilman tunnuksia, kyselyä tai fragmenttia. HTTP sallitaan vain localhost-kokeessa.");
    return {};
}

QString nightscoutDatasetPath(const QString& root, const QString& address) {
    const auto digest = QCryptographicHash::hash(baseUrl(address).toEncoded(), QCryptographicHash::Sha256).toHex();
    return root + "/sources/nightscout-" + QString::fromLatin1(digest) + "/studio.sqlite3";
}

NightscoutImporter::NightscoutImporter(Store& store, QString address, QString accessToken, QObject* parent, qint64 initialHistoryMs)
    : QObject(parent), db(store), address(std::move(address)), accessToken(std::move(accessToken)), initialHistoryMs(initialHistoryMs) {}

NightscoutImporter::~NightscoutImporter() {
    writeWatcher.waitForFinished();
    if (reply) {
        reply->disconnect(this);
        reply->abort();
    }
}

void NightscoutImporter::start() {
    const auto error = nightscoutAddressError(address);
    if (!error.isEmpty()) {
        finish(false, error);
        return;
    }
    if (accessToken.trimmed().isEmpty()) {
        finish(false, QCoreApplication::translate("Studio", "Nightscoutin lukutunnus puuttuu avainrenkaasta."));
        return;
    }
    run = db.beginImport(0, QDateTime::currentMSecsSinceEpoch(), "nightscout");
    if (run.isEmpty()) {
        finish(false, QCoreApplication::translate("Studio", "Nightscout-tuonnin aloitus epäonnistui."));
        return;
    }
    emit progress(QCoreApplication::translate("Studio", "Tarkistetaan Nightscoutin käyttöoikeus…"));
    // The official v2 endpoint exchanges the persistent access token for a JWT.
    // Never log request URLs or network error strings: the exchange URL contains
    // the credential. No redirect is followed, including during this exchange.
    request("api/v2/authorization/request/" + QString::fromLatin1(QUrl::toPercentEncoding(accessToken)), false, [this](QByteArray bytes) {
        const auto object = QJsonDocument::fromJson(bytes).object();
        jwt = object["token"].toString();
        accessToken.clear();
        if (jwt.isEmpty() || jwt.contains('\r') || jwt.contains('\n')) {
            finish(false, QCoreApplication::translate("Studio", "Nightscout ei palauttanut kelvollista käyttöoikeutta."));
            return;
        }
        beginCollection("entries");
    });
}

void NightscoutImporter::request(const QString& relative, bool authorization, std::function<void(QByteArray)> success) {
    const QUrl url = baseUrl(address).resolved(QUrl(relative, QUrl::StrictMode));
    QNetworkRequest networkRequest(url);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setTransferTimeout(30000);
    networkRequest.setRawHeader("Accept", "application/json");
    if (authorization) networkRequest.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());
    auto* current = network.get(networkRequest);
    reply = current;
    auto bytes = std::make_shared<QByteArray>();
    auto oversized = std::make_shared<bool>(false);
    connect(current, &QNetworkReply::readyRead, this, [current, bytes, oversized] {
        bytes->append(current->readAll());
        if (bytes->size() > 64 * 1024 * 1024) {
            *oversized = true;
            current->abort();
        }
    });
    connect(current, &QNetworkReply::finished, this, [this, current, bytes, oversized, relative, authorization, success = std::move(success)] {
        bytes->append(current->readAll());
        const int status = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool networkError = current->error() != QNetworkReply::NoError;
        current->deleteLater();
        reply = nullptr;
        if (complete) return;
        if (*oversized || bytes->size() > 64 * 1024 * 1024) {
            finish(false, QCoreApplication::translate("Studio", "Nightscoutin vastaus ylittää kokorajan."));
            return;
        }
        if (!networkError && status == 200) {
            retries = 0;
            success(*bytes);
            return;
        }
        if ((status == 429 || status >= 500 || (networkError && status == 0)) && retries < 2) {
            const int delay = 1000 * (1 << retries++);
            QTimer::singleShot(delay, this, [this, relative, authorization, success] {
                if (!complete) request(relative, authorization, success);
            });
            return;
        }
        if (status == 401 || status == 403)
            finish(false, QCoreApplication::translate("Studio", "Nightscout hylkäsi lukutunnuksen tai lukuoikeus puuttuu."));
        else if (status >= 300 && status < 400)
            finish(false, QCoreApplication::translate("Studio", "Nightscout ohjaa toiseen osoitteeseen. Anna palvelimen lopullinen HTTPS-osoite."));
        else if (status == 404)
            finish(false, QCoreApplication::translate("Studio", "Nightscout API v3 ei ole saatavilla tässä palvelinosoitteessa."));
        else
            finish(false, QCoreApplication::translate("Studio", "Nightscout-yhteys epäonnistui. Tarkista yhteys, HTTPS-varmenne ja palvelin."));
    });
}

void NightscoutImporter::beginCollection(const QString& name) {
    collection = name;
    // History is ordered by server modification time, including edits and
    // tombstones, even for older events. Also revisit 72 hours to catch late
    // records from legacy uploaders. Keep the older cursor after an outage;
    // the overlap must never skip changes not yet imported.
    const QString storedText = db.meta("nightscoutCursor:" + collection);
    const qint64 stored = storedText.toLongLong();
    fullHistory = storedText == "full";
    cursor = fullHistory  ? nightscoutMinTimestamp
             : stored > 0 ? std::max(nightscoutMinTimestamp, std::min(stored, QDateTime::currentMSecsSinceEpoch() - correctionHistoryMs - 1))
                          : std::max(nightscoutMinTimestamp, QDateTime::currentMSecsSinceEpoch() - initialHistoryMs);
    collectionFrom = cursor;
    seen.clear();
    limit = 1000;
    nextPage();
}

void NightscoutImporter::nextPage() {
    if (++pages > 10000) {
        finish(false, QCoreApplication::translate("Studio", "Nightscout-tuonnin sivuraja ylittyi. Valmiit osat säilyivät."));
        return;
    }
    emit progress(QCoreApplication::translate("Studio", "Tuodaan Nightscoutista ") + collection + " · sivu " + QString::number(pages));
    const auto path = QString("api/v3/%1/history/%2?limit=%3&fields=_all").arg(collection).arg(cursor).arg(limit);
    request(path, true, [this](QByteArray bytes) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        const auto object = document.object();
        if (error.error != QJsonParseError::NoError || !object["result"].isArray() || object["status"].toInt() != 200) {
            finish(false, QCoreApplication::translate("Studio", "Nightscout palautti virheellisen API v3 -vastauksen. Valmiit osat säilyivät."));
            return;
        }
        const auto rows = object["result"].toArray();
        qint64 maximum = cursor, previous = cursor;
        for (const auto& value : rows) {
            const auto row = value.toObject();
            double modified = row["srvModified"].toDouble(NAN);
            if (!std::isfinite(modified)) {
                const auto fallback = collection == "entries" ? row["date"] : row["created_at"];
                if (fallback.isDouble())
                    modified = fallback.toDouble();
                else
                    modified = QDateTime::fromString(fallback.toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
            }
            if (!std::isfinite(modified) || modified <= cursor || modified < previous || modified > 9e15 || std::floor(modified) != modified) {
                finish(false, QCoreApplication::translate("Studio", "Nightscoutin historian muutosleima ei kelpaa. Hakurajaa ei siirretty."));
                return;
            }
            previous = qint64(modified);
            maximum = std::max(maximum, qint64(modified));
        }
        // Keep one millisecond of overlap, including between separate imports.
        // A full page containing only the boundary timestamp must be widened;
        // advancing an exclusive cursor would silently lose timestamp ties.
        if (rows.size() >= limit && maximum <= cursor + 1) {
            if (limit >= 100000) {
                finish(false, QCoreApplication::translate("Studio", "Liian monta Nightscout-tapahtumaa samalla muutosleimalla. Hakuraja säilyi."));
                return;
            }
            limit = std::min(100000, limit * 2);
            nextPage();
            return;
        }
        const qint64 next = rows.isEmpty() ? cursor : std::max<qint64>(0, maximum - 1);
        storePage(bytes, rows, next);
    });
}

void NightscoutImporter::storePage(const QByteArray& bytes, const QJsonArray& rows, qint64 next) {
    if (!backgroundWrites) {
        const bool ok = db.importNightscoutPage(run, collection, bytes, next);
        pageStored(rows, next, ok, ok ? QString() : db.error());
        return;
    }
    const auto path = db.path(), runId = run, kind = collection;
    writeWatcher.disconnect(this);
    connect(&writeWatcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, [this, rows, next] {
        const auto result = writeWatcher.result();
        pageStored(rows, next, result.first, result.second);
    });
    const bool reconcile = fullHistory && rows.size() < limit;
    const auto from = collectionFrom;
    QSet<QString> identifiers;
    if (reconcile) {
        identifiers = seen;
        for (const auto& value : rows) {
            const auto row = value.toObject();
            identifiers.insert("nightscout:" + row["identifier"].toString(row["_id"].toString()));
        }
    }
    writeWatcher.setFuture(QtConcurrent::run([path, runId, kind, bytes, next, reconcile, from, identifiers] {
        Store store(path);
        bool ok = store.good() && store.importNightscoutPage(runId, kind, bytes, next);
        if (ok && reconcile) ok = store.reconcileNightscout(kind, from, identifiers);
        return qMakePair(ok, ok ? QString() : store.error());
    }));
}
void NightscoutImporter::pageStored(const QJsonArray& rows, qint64 next, bool ok, const QString& error) {
    if (!ok) {
        finish(false, error);
        return;
    }
    for (const auto& value : rows) {
        const auto row = value.toObject();
        seen.insert("nightscout:" + row["identifier"].toString(row["_id"].toString()));
    }
    cursor = next;
    if (rows.size() >= limit) {
        limit = 1000;
        QTimer::singleShot(0, this, &NightscoutImporter::nextPage);
        return;
    }
    if (fullHistory && !backgroundWrites && !db.reconcileNightscout(collection, collectionFrom, seen)) {
        finish(false, db.error());
        return;
    }
    if (collection == "entries") {
        beginCollection("treatments");
        return;
    }
    const auto warnings = db.query("SELECT count(*) AS n FROM meta WHERE key GLOB 'nightscoutUnsupported:*'");
    const int count = warnings.isEmpty() ? 0 : warnings[0].toObject()["n"].toInt();
    finish(true, count ? QString(QCoreApplication::translate("Studio", "Nightscout-tuonti valmis. %1 tapahtuman annostyyppiä ei tulkittu; alkuperäiset tiedot säilyivät.")).arg(count)
                       : QCoreApplication::translate("Studio", "Nightscout-tuonti valmis. Historia on käytettävissä paikallisesti."));
}
void NightscoutImporter::finish(bool success, const QString& message) {
    if (complete) return;
    complete = true;
    jwt.clear();
    accessToken.clear();
    if (!run.isEmpty()) db.endImport(run, success, message);
    emit finished(success, message);
}
} // namespace Studio
