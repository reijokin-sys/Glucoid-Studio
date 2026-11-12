// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "backend.h"
#include "databasebackup.h"
#include "syncpolicy.h"
#include "daychart.h"
#include "observation.h"
#include "wireguard.h"
#include <QHostAddress>
#include <KWallet>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QApplication>
#include <QFileDialog>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QProcess>
#include <QTimeZone>
#include <QCryptographicHash>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QScopeGuard>
#include <QtConcurrent/QtConcurrentRun>

using namespace Studio;
// General CGM reference values, not personal treatment targets. The upper mean
// bound is the estimated average glucose of HbA1c 7%; the lower bound is the
// consensus hypoglycaemia threshold. TIR, CV and coverage follow the 2019
// international CGM consensus; GMI follows the ADA general HbA1c target.
static constexpr double referenceMeanLow = 3.9, referenceMeanHigh = 8.6;
static constexpr double referenceGmiTarget = 7.0, referenceTirTarget = 0.70;
static constexpr double referenceCvTarget = 36.0, referenceCoverageTarget = 0.70;
static QString encoded(const QJsonObject& o) {
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}
namespace {
QLocale studioLocale(const QString& language) {
    return QLocale(language == "fi" ? QStringLiteral("fi_FI") : QStringLiteral("en_GB"));
}
Connection readConnection(const QString& path) {
    QFile file(path);
    Connection result;
    if (!file.open(QIODevice::ReadOnly)) return result;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    result.source = object["source"].toString("juggluco");
    if (result.source != "juggluco" && result.source != "nightscout") result.source = "juggluco";
    result.mode = object["mode"].toString("usb");
    result.address = object["address"].toString();
    result.auth = object["auth"].toString("apisecret");
    result.serial = object["serial"].toString();
    if (result.source == "juggluco" && result.mode != "usb") {
        result.mode = "wireguard";
        result.wireguardInterface = object["wireguardInterface"].toString("wg0");
    } else
        result.wireguardInterface.clear();
    return result;
}
QString sourceDatabase(const QString& root, const Connection& connection) {
    return connection.source == "nightscout" ? nightscoutDatasetPath(root, connection.address) : root + "/studio.sqlite3";
}
QString walletKey(const Connection& connection) {
    return connection.source == "nightscout" ? QFileInfo(nightscoutDatasetPath({}, connection.address)).dir().dirName() : QString("juggluco");
}
QJsonObject connectionObject(const Connection& connection) {
    return {{"source", connection.source}, {"mode", connection.mode},     {"address", connection.address},
            {"auth", connection.auth},     {"serial", connection.serial}, {"wireguardInterface", connection.wireguardInterface}};
}
bool saveConnectionFile(const QString& path, const Connection& connection) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(connectionObject(connection)).toJson();
    return file.write(bytes) == bytes.size() && file.commit();
}
} // namespace
QString Backend::databasePath(const QString& root) {
    return QDir::cleanPath(sourceDatabase(root, readConnection(root + "/connection.json")));
}
Backend::Backend(const QString& root, QObject* parent, Importer::Fetch jugglucoFetch)
    : QObject(parent), root(root), db(sourceDatabase(root, readConnection(root + "/connection.json"))), transport(this),
      jugglucoFetch(std::move(jugglucoFetch)), config(readConnection(root + "/connection.json")) {
    if (auto* controller = QCoreApplication::instance()->property("studioLanguageController").value<QObject*>())
        connect(controller, SIGNAL(languageChanged()), this, SLOT(retranslateUi()));
    m_status = db.good() ? QString() : db.error();
    connect(&summaryWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = summaryWatcher.result();
        summaryRunning = false;
        const bool current = runningSummaryKey == summaryRequestedKey && result.value("path").toString() == db.path() &&
                             (!result.value("ok").toBool() || result.value("revision").toString() == db.revision());
        if (current && result.value("ok").toBool()) {
            summarySucceeded = true;
            completedSummaryKey = runningSummaryKey;
            m_analysis = result.value("analysis").toMap();
            m_currentDayChart = result.value("currentDay").toMap();
            m_dayComparisonCharts = result.value("days").toList();
            m_currentReading = result.value("currentReading").toMap();
            emit currentReadingChanged();
            requestDailyAnalysis();
        } else if (current) {
            summarySucceeded = false;
            m_status = QCoreApplication::translate("Studio", "Näkymän laskenta epäonnistui. Aiempi näkymä säilyi.");
        }
        if (summaryPending || !current) {
            summaryPending = false;
            summaryRequestedKey.clear();
            requestSummary();
        }
        emit changed();
    });
    connect(&backupWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = backupWatcher.result();
        backupRunning = false;
        const auto path = result.value("path").toString();
        backupSucceeded = !path.isEmpty();
        m_backupStatus = result.value("status").toString();
        emit changed();
    });
    refresh();
    connect(&dailyWatcher, &QFutureWatcher<QJsonObject>::finished, this, [this] {
        auto result = dailyWatcher.result();
        dailyRunning = false;
        const bool current = runningDailyKey == dailyRequestedKey && (result["status"] == "error" || result["sourceRevision"].toString() == db.revision());
        if (current) {
            result["requestKey"] = runningDailyKey;
            dailyResult = result;
        }
        if (dailyPending || !current) {
            dailyPending = false;
            dailyRequestedKey.clear();
            requestDailyAnalysis();
        } else
            emit changed();
    });
}
Backend::~Backend() {
    // Workers own their SQLite connections and never capture this Backend.
    summaryWatcher.waitForFinished();
    backupWatcher.waitForFinished();
    dailyWatcher.waitForFinished();
}
Backend::Backend(const QString& root, const QString& databasePath, SnapshotReader) : QObject(nullptr), root(root), db(databasePath, true), transport(this) {}

QString Backend::language() const {
    auto* controller = QCoreApplication::instance()->property("studioLanguageController").value<QObject*>();
    return controller ? controller->property("language").toString() : QStringLiteral("en");
}
QString Backend::chooseRestoreBackup() {
    return QFileDialog::getOpenFileName(nullptr, QCoreApplication::translate("Studio", "Valitse palautettava tietokanta"), QString(),
                                        QCoreApplication::translate("Studio", "SQLite-tietokanta (*.sqlite3 *.db);;Kaikki tiedostot (*)"));
}
bool Backend::scheduleRestore(const QString& backupPath) {
    const auto backup = Studio::inspectBackup(backupPath);
    if (!backup.complete) {
        m_backupStatus = QCoreApplication::translate("Studio", "Varmuuskopion tarkistus epäonnistui: %1")
                             .arg(backup.error.isEmpty() ? QCoreApplication::translate("Studio", "vajaa varmuuskopio") : backup.error);
        emit changed();
        return false;
    }
    QString error;
    if (!Studio::writeRestoreRequest(root, backup.path, &error)) {
        m_backupStatus = error;
        emit changed();
        return false;
    }
    m_backupStatus = QCoreApplication::translate("Studio", "Varmuuskopio tarkistettu. Sovellus käynnistyy uudelleen palautusta varten.");
    emit changed();
    return true;
}
void Backend::restartForRestore() {
    if (Studio::readRestoreRequest(root).isEmpty()) {
        m_backupStatus = QCoreApplication::translate("Studio", "Palautuspyyntöä ei löytynyt.");
        emit changed();
        return;
    }
    const auto executable = QCoreApplication::applicationFilePath();
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"),
                                 {QStringLiteral("-c"), QStringLiteral("sleep 2; exec \"$0\" --data-dir \"$1\""), executable, root})) {
        m_backupStatus = QCoreApplication::translate("Studio", "Sovelluksen käynnistäminen palautusta varten epäonnistui.");
        emit changed();
        return;
    }
    QCoreApplication::quit();
}
QString Backend::formatTimestamp(qint64 at, const QString& format) const {
    const QTimeZone zone(analysisSettings().value("timezone").toString().toUtf8());
    if (!zone.isValid()) return {};
    return studioLocale(language()).toString(QDateTime::fromMSecsSinceEpoch(at, zone), format);
}
QString Backend::today() const {
    const QTimeZone zone(analysisSettings().value("timezone").toString().toUtf8());
    return zone.isValid() ? QDateTime::currentDateTime(zone).date().toString(Qt::ISODate) : QString();
}
bool Backend::setLanguage(const QString& language) {
    auto* controller = QCoreApplication::instance()->property("studioLanguageController").value<QObject*>();
    bool ok = false;
    if (controller) QMetaObject::invokeMethod(controller, "setLanguage", Q_RETURN_ARG(bool, ok), Q_ARG(QString, language));
    if (!ok) {
        m_status = QCoreApplication::translate("Studio", "Kielivalintaa ei voitu tallentaa.");
        emit changed();
    }
    return ok;
}
void Backend::retranslateUi() {
    // Keep selections and data intact; rebuild only presentation snapshots.
    completedSummaryKey.clear();
    summaryRequestedKey.clear();
    dailyRequestedKey.clear();
    m_analysis.clear();
    m_currentReading.clear();
    m_currentDayChart.clear();
    m_dayComparisonCharts.clear();
    dailyResult = {};
    m_status = QCoreApplication::translate("Studio", "Käyttöliittymän kieli vaihdettu.");
    if (!m_backupStatus.isEmpty())
        m_backupStatus = backupRunning     ? QCoreApplication::translate("Studio", "Varmuuskopioidaan taustalla…")
                         : backupSucceeded ? QCoreApplication::translate("Studio", "Varmuuskopio valmis.")
                                           : QCoreApplication::translate("Studio", "Varmuuskopioinnissa havaittiin virhe.");
    refresh();
    emit currentReadingChanged();
    emit changed();
}
void Backend::enableBackgroundWork() {
    backgroundWork = true;
}
QString Backend::summaryKey() const {
    return language() + "|" + db.path() + "|" + db.revision() + "|" + QString::number(analysisDays) + "|" + analysisLastDate + "|" + dateWindow + "|" +
           encoded(QJsonObject::fromVariantMap(analysisSettings())) + "|" + encoded(db.observationProfile()) + "|" +
           QString::number(QDateTime::currentMSecsSinceEpoch() / 30000);
}
void Backend::requestSummary() {
    const auto key = summaryKey();
    if (key == summaryRequestedKey || (!summaryRunning && key == completedSummaryKey)) return;
    summaryRequestedKey = key;
    if (summaryRunning)
        summaryPending = true;
    else
        launchSummary();
}
void Backend::launchSummary() {
    summarySucceeded = false;
    summaryRunning = true;
    runningSummaryKey = summaryRequestedKey;
    const auto path = db.path(), dataRoot = root, date = dateWindow, lastDate = analysisLastDate;
    const int period = analysisDays;
    summaryWatcher.setFuture(QtConcurrent::run([path, dataRoot, date, lastDate, period] {
        Backend snapshot(dataRoot, path, SnapshotReader{});
        QVariantMap result{{"path", path}, {"ok", false}};
        if (!snapshot.db.good() || !snapshot.db.run("BEGIN")) return result;
        const auto revision = snapshot.db.revision(); // Pins one WAL read snapshot for every calculation.
        snapshot.dateWindow = date;
        snapshot.analysisDays = period;
        snapshot.analysisLastDate = lastDate;
        snapshot.refresh();
        result.insert("revision", revision);
        result.insert("ok", snapshot.db.good());
        result.insert("analysis", snapshot.m_analysis);
        result.insert("currentDay", snapshot.m_currentDayChart);
        result.insert("days", snapshot.m_dayComparisonCharts);
        result.insert("currentReading", snapshot.m_currentReading);
        snapshot.db.run("ROLLBACK");
        return result;
    }));
    emit changed();
}
QVariantMap Backend::backupSnapshot(const QString& path, const QString& destination, const QString& source) {
    QString error;
    if (!Studio::exportDatabaseBackup(path, destination, source, &error))
        return {{"path", ""}, {"status", QCoreApplication::translate("Studio", "Varmuuskopiointi epäonnistui: %1").arg(error)}};
    return {{"path", destination}, {"status", QCoreApplication::translate("Studio", "Tietokannan varmuuskopio tallennettu: %1").arg(destination)}};
}
void Backend::refreshDailyAnalysis() {
    dailyEnabled = true;
    requestDailyAnalysis();
}
void Backend::requestDailyAnalysis() {
    if (!dailyEnabled || !db.available()) return;
    const QTimeZone zone(analysisSettings().value("timezone").toString().toUtf8());
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto date = QDateTime::fromMSecsSinceEpoch(now, zone).date().toString(Qt::ISODate);
    const auto key = language() + ":" + db.revision() + ":" + QString::fromUtf8(zone.id()) + ":" + date;
    if (key == dailyRequestedKey) return;
    dailyRequestedKey = key;
    pendingDailyZone = zone;
    pendingDailyNow = now;
    dailyResult = {{"status", "computing"}};
    if (dailyRunning)
        dailyPending = true;
    else
        launchDailyAnalysis();
    emit changed();
}
void Backend::launchDailyAnalysis() {
    dailyRunning = true;
    runningDailyKey = dailyRequestedKey;
    const auto path = db.path();
    const auto zone = pendingDailyZone;
    const auto now = pendingDailyNow;
    dailyWatcher.setFuture(QtConcurrent::run([path, zone, now] { return dailyPeriodSnapshot(path, zone, now); }));
}
QVariantMap Backend::connection() const {
    auto result = connectionObject(config).toVariantMap();
    result["configured"] = !db.meta("connectionConfigured").isEmpty();
    const auto warnings = config.source == "nightscout" ? db.query("SELECT count(*) AS n FROM meta WHERE key GLOB 'nightscoutUnsupported:*'") : QJsonArray();
    result["unsupportedEvents"] = warnings.isEmpty() ? 0 : warnings[0].toObject()["n"].toInt();
    return result;
}
QVariantMap Backend::savedConnection(const QString& source) const {
    if (source == config.source) return connection();
    if (source != "juggluco" && source != "nightscout") return {};
    const QString file = root + "/connection-" + source + ".json";
    if (QFile::exists(file)) return connectionObject(readConnection(file)).toVariantMap();
    Connection empty;
    empty.source = source;
    if (source == "nightscout") {
        empty.mode = "network";
        empty.auth = "token";
    }
    return connectionObject(empty).toVariantMap();
}
QString Backend::secret(bool allowPrompt) {
    if (config.auth == "none") return {};
    const auto name = KWallet::Wallet::NetworkWallet();
    if (!allowPrompt && !KWallet::Wallet::isOpen(name)) return {};
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(name, 0, KWallet::Wallet::Synchronous));
    if (!wallet || !wallet->setFolder("GlucoidStudio")) return {};
    QString value;
    if (wallet->readPassword(walletKey(config), value) != 0) return {};
    return value;
}
bool Backend::writeSecret(const QString& value, const QString& key) {
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous));
    if (!wallet) return false;
    if (!wallet->hasFolder("GlucoidStudio") && !wallet->createFolder("GlucoidStudio")) return false;
    return wallet->setFolder("GlucoidStudio") && wallet->writePassword(key.isEmpty() ? walletKey(config) : key, value) == 0;
}
bool Backend::writeConnection() {
    return saveConnectionFile(root + "/connection.json", config);
}
void Backend::saveConnection(const QString& mode, const QString& address, const QString& auth, const QString& secret, const QString& serial) {
    saveSourceConnection("juggluco", mode, address, auth, secret, serial);
}
void Backend::clearSourceResults() {
    updateSucceeded = false;
    dailyRequestedKey.clear();
    runningDailyKey.clear();
    dailyResult = {};
    dailyPending = false;
    dateWindow.clear();
    analysisLastDate.clear();
}
bool Backend::saveSourceConnection(const QString& source, const QString& mode, const QString& address, const QString& auth, const QString& secret,
                                   const QString& serial, const QString& wireguardInterface) {
    auto fail = [this](const QString& why) {
        m_status = why;
        emit changed();
        return false;
    };
    if (m_busy || dailyRunning) return fail(QCoreApplication::translate("Studio", "Odota käynnissä olevan haun tai analyysin valmistumista."));
    if (source != "juggluco" && source != "nightscout") return fail(QCoreApplication::translate("Studio", "Tuntematon tietolähde."));
    Connection next;
    next.source = source;
    next.mode = mode;
    next.address = address.trimmed();
    next.auth = auth;
    next.serial = serial;
    if (source == "nightscout") {
        const auto error = nightscoutAddressError(next.address);
        if (!error.isEmpty()) return fail(error);
        if (mode != "network" || auth != "token")
            return fail(QCoreApplication::translate("Studio", "Nightscout käyttää HTTPS-verkkoyhteyttä ja lukutunnusta."));
        next.serial.clear();
        next.wireguardInterface.clear();
    } else {
        if ((mode != "usb" && mode != "network" && mode != "wireguard") || !QStringList{"apisecret", "token", "none"}.contains(auth))
            return fail(QCoreApplication::translate("Studio", "Tuntematon yhteystapa."));
        const QUrl url(next.address);
        if (mode != "usb" && (!url.isValid() || url.host().isEmpty() || !QStringList{"http", "https"}.contains(url.scheme()) || !url.userInfo().isEmpty() ||
                              url.hasQuery() || url.hasFragment() || (!url.path().isEmpty() && url.path() != "/")))
            return fail(QCoreApplication::translate("Studio", "Anna Jugglucon juuriosoite ilman tunnuksia."));
        next.wireguardInterface.clear();
        if (mode != "usb") {
            next.mode = "wireguard";
            next.wireguardInterface = wireguardInterface.trimmed();
            const auto error = wireguardNameError(next.wireguardInterface);
            if (!error.isEmpty()) return fail(error);
            QHostAddress target;
            if (!target.setAddress(url.host()) || !target.scopeId().isEmpty())
                return fail(QCoreApplication::translate("Studio", "Anna puhelimen numeerinen WireGuard-tunneliosoite."));
        }
    }
    QLockFile lock(root + "/sync.lock");
    if (!lock.tryLock(0)) return fail(QCoreApplication::translate("Studio", "Toinen tuonti on käynnissä."));
    if (auth != "none" && !secret.isEmpty() && !writeSecret(secret, walletKey(next)))
        return fail(QCoreApplication::translate("Studio", "Tunnuksen tallennus avainrenkaaseen epäonnistui."));
    const auto previous = config;
    const auto previousPath = db.path();
    if (!saveConnectionFile(root + "/connection-" + previous.source + ".json", previous))
        return fail(QCoreApplication::translate("Studio", "Aiemman lähdeasetuksen säilyttäminen epäonnistui."));
    if (!db.switchDatabase(sourceDatabase(root, next)))
        return fail(QCoreApplication::translate("Studio", "Valitun lähteen paikallisen aineiston avaaminen epäonnistui."));
    config = next;
    if (!db.setMeta("connectionConfigured", "1") || !saveConnectionFile(root + "/connection-" + source + ".json", config) || !writeConnection()) {
        config = previous;
        db.switchDatabase(previousPath);
        return fail(QCoreApplication::translate("Studio", "Lähdeasetuksen tallentaminen epäonnistui. Aiempi valinta säilyi."));
    }
    clearSourceResults();
    m_status = QCoreApplication::translate("Studio", "Tietolähde tallennettu. Vain valittu lähde hakee tietoja; aiemman lähteen historia säilyi.");
    refresh();
    return true;
}
bool Backend::reloadSource() {
    QFile active(root + "/connection.json");
    if (!active.open(QIODevice::ReadOnly)) return false;
    const auto document = QJsonDocument::fromJson(active.readAll());
    const auto source = document.object()["source"].toString("juggluco");
    if (!document.isObject() || (source != "juggluco" && source != "nightscout")) return false;
    const auto next = readConnection(root + "/connection.json");
    if (connectionObject(next) == connectionObject(config)) return true;
    if (dailyRunning) {
        m_status = QCoreApplication::translate("Studio", "Lähdeasetus muuttui. Odota analyysin valmistumista ja päivitä uudelleen.");
        emit changed();
        return false;
    }
    if (!db.switchDatabase(sourceDatabase(root, next))) {
        m_status = QCoreApplication::translate("Studio", "Valitun lähteen avaaminen epäonnistui.");
        emit changed();
        return false;
    }
    config = next;
    clearSourceResults();
    refresh();
    return true;
}
void Backend::refreshSourceChoice() {
    if (m_busy || dailyRunning || !QFile::exists(root + "/connection.json")) return;
    QLockFile lock(root + "/sync.lock");
    if (lock.tryLock(0)) reloadSource();
}
void Backend::importGlucoid() {
    auto c = glucoidConnection();
    if (c.address.isEmpty()) {
        m_status = QCoreApplication::translate("Studio", "Glucoidin yhteysasetusta ei löytynyt.");
        emit changed();
        return;
    }
    saveConnection("usb", c.address, c.auth, c.secret, config.serial);
}
void Backend::setWindow(const QString& date, int days) {
    dateWindow = date;
    Q_UNUSED(days)
    refresh();
}
void Backend::refresh() {
    if (backgroundWork) {
        requestSummary();
        return;
    }
    refreshAnalysis();
    emit changed();
}

void Backend::sync() {
    startSync(false);
}
void Backend::syncAll() {
    startSync(true);
}
void Backend::startSync(bool full) {
    if (m_busy) return;
    updateSucceeded = false;
    emit changed();
    syncLock = std::make_unique<QLockFile>(root + "/sync.lock");
    syncLock->setStaleLockTime(120000);
    if (!syncLock->tryLock(0)) {
        m_status = QCoreApplication::translate("Studio", "Toinen tuonti on käynnissä.");
        emit changed();
        emit syncFinished(false);
        return;
    }
    if (!reloadSource() || db.meta("connectionConfigured").isEmpty()) {
        syncLock->unlock();
        syncLock.reset();
        m_status = QCoreApplication::translate("Studio", "Määritä valitun lähteen yhteys.");
        emit changed();
        emit syncFinished(false);
        return;
    }
    // A held lock cannot be stale while its owning process is alive.
    db.run("UPDATE import_runs SET status='partial',error='Tuontiprosessi keskeytyi' WHERE status='running'");
    m_busy = true;
    config.secret = secret(qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr);
    m_status = QCoreApplication::translate("Studio", "Aloitetaan tuonti…");
    emit changed();
    if (config.source == "nightscout") {
        nightscoutImporter = std::make_unique<NightscoutImporter>(db, config.address, config.secret);
        nightscoutImporter->setBackgroundWrites(backgroundWork);
        connect(nightscoutImporter.get(), &NightscoutImporter::progress, this, [this](const QString& text) {
            m_status = text;
            emit changed();
        });
        connect(nightscoutImporter.get(), &NightscoutImporter::finished, this, [this](bool ok, const QString& text) {
            finishSync(ok ? QString() : text);
            m_status = text;
            emit changed();
        });
        // API v3 history includes source edits and tombstones. Full sync rewinds
        // cursors without deleting local history or changing the selected source.
        if (full && (!db.setMeta("nightscoutCursor:entries", "full") || !db.setMeta("nightscoutCursor:treatments", "full"))) {
            finishSync(QCoreApplication::translate("Studio", "Historiatarkistuksen aloitus epäonnistui."));
            return;
        }
        nightscoutImporter->start();
        return;
    }
    Importer::Fetch fetch =
        jugglucoFetch ? jugglucoFetch : Importer::Fetch([this](const QString& path, auto done) { transport.get(config, path, std::move(done)); });
    importer = std::make_unique<Importer>(db, std::move(fetch));
    importer->setBackgroundWrites(backgroundWork);
    connect(importer.get(), &Importer::progress, this, [this](const QString& text) {
        m_status = text;
        emit changed();
    });
    connect(importer.get(), &Importer::finished, this, [this](bool ok, const QString& text) {
        finishSync(ok ? QString() : text);
        m_status = text;
        emit changed();
    });
    const auto last = db.meta("lastSourceUntil").toLongLong(), to = QDateTime::currentSecsSinceEpoch() * 1000;
    const qint64 from = full ? 0 : !last ? std::max(1000LL, to - initialImportHistoryMs) : std::max(1000LL, std::min(last, to - correctionHistoryMs));
    importer->start(from, to, config.mode);
}
void Backend::finishSync(const QString& error) {
    updateSucceeded = error.isEmpty();
    m_busy = false;
    config.secret.clear();
    if (syncLock) {
        syncLock->unlock();
        syncLock.reset();
    }
    m_status = error.isEmpty() ? QCoreApplication::translate("Studio", "Historia päivitetty.") : error;
    if (!error.isEmpty()) db.setMeta("connectionError", error);
    refresh();
    emit syncFinished(error.isEmpty());
}
bool Backend::mapLabel(const QString& label, const QString& kind, const QString& medicine) {
    bool ok = db.mapLabel(label, kind, medicine);
    m_status = ok ? QCoreApplication::translate("Studio", "Merkintäluokka tallennettu.")
                  : QCoreApplication::translate("Studio", "Luokan tallennus epäonnistui.");
    refresh();
    return ok;
}
QString Backend::backup() {
    if (backupRunning || !db.available()) return {};
    // Native system save dialog comes FIRST. No snapshot or temporary file
    // exists until the user has accepted a destination (and overwrite prompt).
    const auto source = config.source == "nightscout" ? QStringLiteral("Nightscout") : QStringLiteral("Juggluco");
    const auto suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Glucoid-" + source + "-" +
                           QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".sqlite3";
    const auto destination = QFileDialog::getSaveFileName(nullptr, QCoreApplication::translate("Studio", "Tallenna tietokannan varmuuskopio"), suggested,
                                                          QCoreApplication::translate("Studio", "SQLite-tietokanta (*.sqlite3)"));
    if (destination.isEmpty()) return {};
    return backupTo(destination);
}
QString Backend::backupTo(const QString& destination) {
    if (destination.isEmpty() || backupRunning || !db.available()) return {};
    backupSucceeded = false;
    const auto path = db.path(), source = config.source;
    if (backgroundWork) {
        backupRunning = true;
        m_backupStatus = QCoreApplication::translate("Studio", "Varmuuskopioidaan taustalla…");
        backupWatcher.setFuture(QtConcurrent::run([path, destination, source] { return backupSnapshot(path, destination, source); }));
        emit changed();
        return {};
    }
    const auto result = backupSnapshot(path, destination, source);
    backupSucceeded = !result.value("path").toString().isEmpty();
    m_backupStatus = result.value("status").toString();
    emit changed();
    return result.value("path").toString();
}
void Backend::seedDemo() {
    if (!db.meta("lastSync").isEmpty()) {
        m_status = QCoreApplication::translate("Studio", "Esimerkkiaineisto sallitaan vain tyhjään tietokantaan.");
        emit changed();
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch() * 1000, from = now - 30LL * 86400000;
    QList<Reading> rs;
    QList<Amount> as;
    for (qint64 t = from; t < now; t += 5 * 60000) {
        if ((t - from) % (3 * 86400000) > 86400000 && ((t - from) / 60000) % 1440 > 600 && ((t - from) / 60000) % 1440 < 650) continue;
        Reading r;
        r.time = t;
        r.sensor = "synthetic";
        r.key = "demo:" + QString::number(t);
        r.mmol = 7 + 2 * std::sin(double(t - from) / 3600000) + 0.6 * std::sin(double(t - from) / 150000);
        r.original = r.mmol;
        r.originalUnit = "mmol/L";
        rs.append(r);
    }
    for (int d = 0; d < 30; ++d) {
        Amount a;
        a.source = "synthetic";
        a.time = now - d * 86400000LL - 4 * 3600000;
        a.label = QCoreApplication::translate("Studio", "Esimerkki HH");
        a.value = 45;
        a.key = fingerprint(a) + ":0";
        as.append(a);
        a.label = QCoreApplication::translate("Studio", "Esimerkki pikainsuliini");
        a.value = 4;
        a.key = fingerprint(a) + ":0";
        as.append(a);
    }
    db.ingest(rs, as, from - 1, now + 1, true);
    db.mapLabel(QCoreApplication::translate("Studio", "Esimerkki HH"), "carbs", "");
    db.mapLabel(QCoreApplication::translate("Studio", "Esimerkki pikainsuliini"), "rapid", QCoreApplication::translate("Studio", "Synteettinen esimerkki"));
    m_status = QCoreApplication::translate("Studio", "Keinotekoinen esimerkkiaineisto ladattu. Ei henkilökohtaisia hoitoasetuksia.");
    refresh();
}

QVariantMap Backend::analysisSettings() const {
    auto p = QJsonDocument::fromJson(db.meta("analysisSettings").toUtf8()).object();
    if (!p.contains("timezone")) p["timezone"] = "Europe/Helsinki";
    if (!p.contains("maxGapMinutes")) p["maxGapMinutes"] = 20;
    if (!p.contains("fillHistory")) p["fillHistory"] = false;
    return p.toVariantMap();
}
bool Backend::saveAnalysisSettings(const QVariantMap& settings) {
    auto p = QJsonObject::fromVariantMap(settings);
    auto zone = QTimeZone(p["timezone"].toString().toUtf8());
    const double lo = number(p, "low"), hi = number(p, "high"), gap = number(p, "maxGapMinutes");
    if (!zone.isValid() || !std::isfinite(gap) || gap < 1 || gap > 60 || ((std::isfinite(lo) || std::isfinite(hi)) && !(lo > 0 && hi > lo))) {
        m_status = QCoreApplication::translate("Studio", "Tarkista aikavyöhyke, katkosraja ja tavoitealueen molemmat rajat.");
        emit changed();
        return false;
    }
    db.setMeta("analysisZone", p["timezone"].toString());
    bool ok = db.setMeta("analysisSettings", encoded(p));
    refresh();
    return ok;
}
void Backend::setAnalysisWindow(int days, const QString& lastDate) {
    if (days != 7 && days != 14 && days != 28 && days != 90) return;
    const auto d = QDate::fromString(lastDate, Qt::ISODate);
    const auto zone = QTimeZone(analysisSettings().value("timezone").toString().toUtf8());
    if (!lastDate.isEmpty() && (!d.isValid() || d >= QDateTime::currentDateTime(zone).date())) {
        m_status = QCoreApplication::translate("Studio", "Valitse valmistunut kalenteripäivä muodossa vvvv-kk-pp.");
        emit changed();
        return;
    }
    analysisDays = days;
    analysisLastDate = lastDate;
    refreshAnalysis();
    emit changed();
}
void Backend::refreshAnalysis() {
    if (backgroundWork) {
        requestSummary();
        return;
    }
    const auto settings = QJsonObject::fromVariantMap(analysisSettings());
    const auto zone = QTimeZone(settings["timezone"].toString().toUtf8());
    auto last = QDate::fromString(analysisLastDate, Qt::ISODate);
    const auto today = QDateTime::currentDateTime(zone).date();
    if (!last.isValid() || last >= today) last = today.addDays(-1);
    const auto first = last.addDays(1 - analysisDays), previous = first.addDays(-analysisDays);
    const qint64 from = QDateTime(previous, QTime(0, 0), zone).toMSecsSinceEpoch(), to = QDateTime(last.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();
    const auto stream = db.readings(from, to, "stream"), history = db.readings(from, to, "history");
    const int gap = qRound(number(settings, "maxGapMinutes") * 60);
    auto data = selectSeries(stream, history, from, to, settings["fillHistory"].toBool(), gap);
    // Standard glucose summaries are independent of the research-only switches.
    const auto summaryStream = db.readings(from - 1200000, to, "stream"), summaryHistory = db.readings(from - 1200000, to, "history");
    auto result = summaryAnalysis(summaryStream, summaryHistory, first, analysisDays, zone, number(settings, "low"), number(settings, "high"));
    result["previous"] = summaryAnalysis(summaryStream, summaryHistory, previous, analysisDays, zone, number(settings, "low"), number(settings, "high"));
    // GMI is an estimate of HbA1c from the same time-weighted mean. The 14-day
    // and 70% coverage rule marks whether the estimate has a conventional basis.
    if (result.contains("mean")) {
        const double mean = result["mean"].toDouble();
        result["gmi"] = gmiPercent(mean);
        result["gmiMmol"] = gmiMmolMol(mean);
        result["gmiAvailable"] = true;
        result["gmiAdequate"] = analysisDays >= 14 && result["coverage"].toDouble() >= 0.70;
        result["gmiDays"] = analysisDays;
    } else
        result["gmiAvailable"] = false;
    QJsonObject reference{{"meanLow", referenceMeanLow},     {"meanHigh", referenceMeanHigh}, {"gmiTarget", referenceGmiTarget},
                          {"tirTarget", referenceTirTarget}, {"cvTarget", referenceCvTarget}, {"coverageTarget", referenceCoverageTarget}};
    reference["meanInRange"] = result.contains("mean") && result["mean"].toDouble() >= referenceMeanLow && result["mean"].toDouble() <= referenceMeanHigh;
    reference["gmiInRange"] = result.contains("gmi") && result["gmi"].toDouble() < referenceGmiTarget;
    reference["tirInRange"] = result.contains("inRange") && result["inRange"].toDouble() >= referenceTirTarget;
    reference["cvInRange"] = result.contains("cv") && result["cv"].toDouble() <= referenceCvTarget;
    reference["coverageInRange"] = result["coverage"].toDouble() >= referenceCoverageTarget;
    reference["nightInRange"] =
        result.contains("nightMean") && result["nightMean"].toDouble() >= referenceMeanLow && result["nightMean"].toDouble() <= referenceMeanHigh;
    result["reference"] = reference;
    result["observation"] = observeProfile(db.observationProfile(), data, first, analysisDays, zone);
    result["lastImport"] = db.meta("lastCompleteImport").toDouble();
    const auto latest = db.query("SELECT max(time) AS t FROM glucose WHERE series_kind='stream'");
    if (!latest.isEmpty()) result["lastReading"] = latest[0].toObject()["t"];
    const auto latestAmount = db.query("SELECT max(time) AS t FROM amounts WHERE active=1 AND local=0");
    if (!latestAmount.isEmpty()) result["lastAmount"] = latestAmount[0].toObject()["t"];
    // Short current-day report is separate from complete-day aggregates.
    const qint64 dayStart = QDateTime(today, QTime(0, 0), zone).toMSecsSinceEpoch(), now = QDateTime::currentMSecsSinceEpoch();
    result["today"] = summaryAnalysis(db.readings(dayStart - 1200000, now), db.readings(dayStart - 1200000, now, "history"), today, 1, zone,
                                      number(settings, "low"), number(settings, "high"));
    // Front page: the current local calendar day, stream first with history gaps.
    const qint64 dayEnd = QDateTime(today.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();
    m_currentDayChart =
        currentDayChartData(db.readings(dayStart - summaryPreRollMs, dayEnd, "stream"), db.readings(dayStart - summaryPreRollMs, dayEnd, "history"), today,
                            zone, number(settings, "low"), number(settings, "high"))
            .toVariantMap();
    m_analysis = result.toVariantMap();
    m_currentDayChart["dateLabel"] = studioLocale(language()).toString(today, "ddd d.M.yyyy");
    m_dayComparisonCharts = buildDayComparisonCharts(settings, zone, today);
    refreshCurrentReading();
    summarySucceeded = db.good();
}
// Previous local calendar day with its own time axis. Research chart switches
// remain independent of the standard summary policy.

// Four local days, standard stream/history policy and the front-page pre-roll.
QVariantList Backend::buildDayComparisonCharts(const QJsonObject& settings, const QTimeZone& zone, const QDate& today) const {
    const auto comparisonDay = QDate::fromString(dateWindow, Qt::ISODate);
    const auto anchor = comparisonDay.isValid() ? comparisonDay : today;
    const auto comparisonFrom = anchor.addDays(-3).startOfDay(zone).toMSecsSinceEpoch();
    const auto comparisonTo = anchor.addDays(1).startOfDay(zone).toMSecsSinceEpoch();
    const auto comparisonStream = db.readings(comparisonFrom - summaryPreRollMs, comparisonTo, "stream");
    const auto comparisonHistory = db.readings(comparisonFrom - summaryPreRollMs, comparisonTo, "history");
    const auto comparisonAmounts = db.amounts(comparisonFrom, comparisonTo);
    QVariantList charts;
    for (int offset = 0; offset < 4; ++offset) {
        auto dayChart =
            currentDayChartData(comparisonStream, comparisonHistory, anchor.addDays(-offset), zone, number(settings, "low"), number(settings, "high"));
        dayChart["dateLabel"] = studioLocale(language()).toString(anchor.addDays(-offset), "ddd d.M.yyyy");
        QJsonArray markers;
        const auto from = qint64(dayChart["from"].toDouble()), to = qint64(dayChart["to"].toDouble());
        for (const auto& value : comparisonAmounts) {
            auto entry = value.toObject();
            const auto time = qint64(entry["time"].toDouble());
            if (time >= from && time < to) {
                entry["timeText"] = QDateTime::fromMSecsSinceEpoch(time, zone).toString("dd.MM.yyyy HH:mm");
                markers.append(entry);
            }
        }
        dayChart["markers"] = markers;
        charts.append(dayChart.toVariantMap());
    }
    return charts;
}
void Backend::refreshCurrentReading() {
    if (backgroundWork) {
        requestSummary();
        return;
    }
    const auto conf = QJsonObject::fromVariantMap(analysisSettings());
    const QTimeZone zone(conf["timezone"].toString().toUtf8());
    const auto now = QDateTime::currentMSecsSinceEpoch();
    auto read = [&](qint64 from) {
        return currentGlucose(db.readings(from, now + 60000), db.readings(from, now + 60000, "history"), now, zone, number(conf, "low"), number(conf, "high"),
                              number(conf, "hypo"))
            .toVariantMap();
    };
    auto value = read(now - 24LL * 3600000);
    if (!value["hasData"].toBool()) value = read(now - 30LL * 86400000);
    if (m_currentReading != value) {
        m_currentReading = value;
        emit currentReadingChanged();
    }
    requestDailyAnalysis();
}
bool Backend::importFolder(const QString& path) {
    if (m_busy) return false;
    if (config.source != "juggluco") {
        m_status = QCoreApplication::translate("Studio", "TSV-kansiotuonti kuuluu Juggluco-lähteeseen. Valitse se ensin tietolähteeksi.");
        emit changed();
        return false;
    }
    QDir folder(path);
    if (!folder.isAbsolute() || !folder.exists()) {
        m_status = QCoreApplication::translate("Studio", "Valitse paikallinen vientikansio.");
        emit changed();
        return false;
    }
    syncLock = std::make_unique<QLockFile>(root + "/sync.lock");
    if (!syncLock->tryLock(0)) {
        m_status = QCoreApplication::translate("Studio", "Toinen tuonti on käynnissä.");
        emit changed();
        return false;
    }
    struct Piece {
        QString kind;
        QByteArray bytes;
    };
    QList<Piece> pieces;
    qint64 from = std::numeric_limits<qint64>::max(), to = 0;
    QString error;
    for (auto kind : QStringList{"stream", "history", "scans", "amounts"}) {
        QString filename = folder.filePath(kind + "-full.tsv");
        if (!QFile::exists(filename)) filename = folder.filePath(kind + ".tsv");
        if (!QFile::exists(filename)) continue;
        QFile f(filename);
        if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024 * 1024) {
            error = QCoreApplication::translate("Studio", "Vientitiedoston lukeminen epäonnistui tai se on liian suuri.");
            break;
        }
        auto bytes = f.readAll();
        if (kind == "amounts") {
            auto p = parseAmounts(bytes);
            error = p.error;
            for (const auto& r : p.rows) {
                from = std::min(from, r.time);
                to = std::max(to, r.time + 1000);
            }
        } else {
            auto p = parseStream(bytes, kind);
            error = p.error;
            for (const auto& r : p.rows) {
                from = std::min(from, r.time);
                to = std::max(to, r.time + 1000);
            }
        }
        if (!error.isEmpty()) break;
        pieces << Piece{kind, bytes};
    }
    if (pieces.isEmpty() || from >= to || !error.isEmpty()) {
        syncLock->unlock();
        syncLock.reset();
        m_status = error.isEmpty() ? QCoreApplication::translate("Studio", "Kansiossa ei ole kelvollisia alkuperäisiä TSV-vientejä.") : error;
        emit changed();
        return false;
    }
    const QString run = db.beginImport(from, to, "files-additive");
    bool ok = !run.isEmpty();
    for (const auto& p : pieces) {
        if (!ok) break;
        // File boundaries cannot prove source deletions; every piece still has provenance.
        ok = db.importChunk(run, p.kind, p.bytes, from, to, false);
    }
    if (!db.endImport(run, ok, ok ? QCoreApplication::translate("Studio", "Lisäävä tiedostotuonti; puuttuvia lähdetapahtumia ei poisteta.") : db.error()))
        ok = false;
    syncLock->unlock();
    syncLock.reset();
    m_status = ok ? QCoreApplication::translate("Studio", "Tiedostot tuotu. Puuttuvia vanhoja merkintöjä ei poistettu.") : db.error();
    refresh();
    return ok;
}
