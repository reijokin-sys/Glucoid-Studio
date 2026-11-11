// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "backend.h"
#include "databasebackup.h"
#include <QFileDialog>
#include "language.h"
#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QCryptographicHash>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QPalette>
#include <QQuickStyle>
#include <QMessageBox>
#include <QProgressDialog>
#include <QAbstractButton>
#include <QPushButton>

static void applyDarkPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor("#12161a"));
    p.setColor(QPalette::WindowText, QColor("#eef2f5"));
    p.setColor(QPalette::Base, QColor("#12161a"));
    p.setColor(QPalette::AlternateBase, QColor("#1c2227"));
    p.setColor(QPalette::Text, QColor("#eef2f5"));
    p.setColor(QPalette::Button, QColor("#242b31"));
    p.setColor(QPalette::ButtonText, QColor("#eef2f5"));
    p.setColor(QPalette::Highlight, QColor("#5fd0b0"));
    p.setColor(QPalette::HighlightedText, QColor("#12161a"));
    p.setColor(QPalette::PlaceholderText, QColor("#6b7883"));
    p.setColor(QPalette::ToolTipBase, QColor("#242b31"));
    p.setColor(QPalette::ToolTipText, QColor("#eef2f5"));
    p.setColor(QPalette::Link, QColor("#7ab8ec"));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor("#6b7883"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6b7883"));
    QGuiApplication::setPalette(p);
}

class Activator : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "fi.reijo.GlucoidStudio.Activation")
  public:
    QQuickWindow* window = nullptr;
  public slots:
    void Open() {
        if (window) {
            window->show();
            window->raise();
            window->requestActivate();
        }
    }
};
enum class StartupChoice { Continue, Quit };

static bool restoreWithProgress(const QString& backupPath, const QString& root, bool replaceExisting) {
    QProgressDialog progress(QCoreApplication::translate("Studio", "Tarkistetaan ja palautetaan varmuuskopiota…"), QString(), 0, 0);
    progress.setWindowTitle(QCoreApplication::translate("Studio", "Varmuuskopion palautus"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents();
    const auto result = Studio::restoreBackup(backupPath, root, replaceExisting);
    progress.close();
    if (!result.ok) {
        QMessageBox::critical(nullptr, QCoreApplication::translate("Studio", "Varmuuskopion palautus epäonnistui"),
                              result.error + QStringLiteral("\n\n") + QCoreApplication::translate("Studio", "Nykyistä tietokantaa ei muutettu."));
        return false;
    }
    return true;
}

static StartupChoice startupRecovery(const QString& root, bool missing, const QString& error, bool allowFresh) {
    while (true) {
        QMessageBox box(QMessageBox::Critical,
                        missing ? QCoreApplication::translate("Studio", "Tietokantaa ei löytynyt")
                                : QCoreApplication::translate("Studio", "Tietokantaa ei voitu avata"),
                        missing ? QCoreApplication::translate("Studio", "Glucoid Studion tietokantaa ei löytynyt. Sovellus ei voi jatkaa "
                                                                        "nykyisellä tiedostolla.")
                                : QCoreApplication::translate("Studio", "Glucoid Studio ei voi käynnistyä, koska tietokanta on "
                                                                        "korruptoitunut tai sitä ei voi avata."),
                        QMessageBox::NoButton, nullptr);
        box.setInformativeText((missing ? QCoreApplication::translate("Studio", "Odotettu tietokanta: %1").arg(Backend::databasePath(root))
                                        : QCoreApplication::translate("Studio", "Tietokanta: %1").arg(Backend::databasePath(root))) +
                               QStringLiteral("\n") +
                               (allowFresh
                                    ? QStringLiteral("\n") + QCoreApplication::translate("Studio", "Voit myös luoda uuden tyhjän tietokannan. "
                                                                                                   "Nykyistä tiedostoa ei korvata ilman erillistä valintaa.")
                                    : QString()));
        box.setDetailedText(error.isEmpty() ? Backend::databasePath(root) : error);
        QAbstractButton* restore = box.addButton(QCoreApplication::translate("Studio", "Palauta varmuuskopio…"), QMessageBox::AcceptRole);
        QAbstractButton* fresh =
            allowFresh ? box.addButton(QCoreApplication::translate("Studio", "Luo uusi tyhjä tietokanta"), QMessageBox::DestructiveRole) : nullptr;
        box.addButton(QCoreApplication::translate("Studio", "Lopeta"), QMessageBox::RejectRole);
        box.exec();
        auto* clicked = box.clickedButton();
        if (restore && clicked == restore) {
            const auto selected =
                QFileDialog::getOpenFileName(nullptr, QCoreApplication::translate("Studio", "Valitse palautettava tietokanta"),
                                             QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                             QCoreApplication::translate("Studio", "SQLite-tietokanta (*.sqlite3 *.db);;Kaikki tiedostot (*)"));
            if (selected.isEmpty()) continue;
            if (QMessageBox::question(nullptr, QCoreApplication::translate("Studio", "Palauta varmuuskopio"),
                                      QCoreApplication::translate(
                                          "Studio", "Korvataanko nykyinen tietokanta valitulla kopiolla? Nykyinen tietokanta säilytetään turvakopiona.")) !=
                QMessageBox::Yes)
                continue;
            if (restoreWithProgress(selected, root, true)) return StartupChoice::Continue;
            continue;
        }
        if (fresh && clicked == fresh) return StartupChoice::Continue;
        return StartupChoice::Quit;
    }
}

int main(int argc, char** argv) {
    bool collector = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--collect" || QByteArray(argv[i]) == "--collect-once") collector = true;
    std::unique_ptr<QCoreApplication> app;
    if (collector)
        app = std::make_unique<QCoreApplication>(argc, argv);
    else
        app = std::make_unique<QApplication>(argc, argv);
    if (!collector) {
        applyDarkPalette();
        QQuickStyle::setStyle(QStringLiteral("Fusion"));
    }
    QCoreApplication::setApplicationName("glucoid-studio");
    QCoreApplication::setApplicationVersion(GLUCOID_VERSION);
    QCoreApplication::setOrganizationDomain("fi.reijo");
    if (!collector) {
        QGuiApplication::setApplicationDisplayName("Glucoid Studio");
        // Wayland groups windows by this desktop-file id, not by their title.
        QGuiApplication::setDesktopFileName("fi.reijo.glucoidstudio");
        QGuiApplication::setWindowIcon(QIcon(":/packaging/fi.reijo.glucoidstudio.png"));
    }
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", QCoreApplication::translate("Studio", "Paikallisen yksityisen tietohakemiston polku."), "path"});
    parser.addOption({"collect", QCoreApplication::translate("Studio", "Kerää taustalla 60 sekunnin välein.")});
    parser.addOption({"collect-once", QCoreApplication::translate("Studio", "Tee yksi tietohaku ja lopeta.")});
    parser.addOption({"import-glucoid", QCoreApplication::translate("Studio", "Tuo yhteys Glucoidista ja tallenna tunnus avainrenkaaseen.")});
    parser.addOption({"demo", QCoreApplication::translate("Studio", "Avaa erillinen keinotekoinen esimerkkiaineisto.")});
    parser.addOption({"backup-only", QCoreApplication::translate("Studio", "Varmuuskopioi tietokanta valittuun tiedostoon ja lopeta.")});
    parser.addOption({"import-folder", QCoreApplication::translate("Studio", "Tuo alkuperäiset TSV-viennit paikallisesta kansiosta ja lopeta."), "path"});
    parser.addPositionalArgument("url", "glucoid-studio://today", "[url]");
    parser.addOption({"backup-file", QCoreApplication::translate("Studio", "Varmuuskopion tallennustiedosto (--backup-only)."), "path"});
    parser.process(*app);
    if (parser.positionalArguments().size() > 1) {
        QTextStream(stderr) << QCoreApplication::translate("Studio", "Virheellinen avauspyyntö.\n");
        return 2;
    }
    if (!parser.positionalArguments().isEmpty()) {
        const QUrl url(parser.positionalArguments()[0]);
        if (url.scheme() != "glucoid-studio" || url.host() != "today" || !url.path().isEmpty() || url.hasQuery() || url.hasFragment() ||
            !url.userInfo().isEmpty()) {
            QTextStream(stderr) << QCoreApplication::translate("Studio", "Tuntematon avauspyyntö.\n");
            return 2;
        }
    }
    const bool synthetic = parser.isSet("demo");
    QString root = parser.value("data-dir");
    if (root.isEmpty()) {
#ifdef STUDIO_LOCAL_DATA_ROOT
        const QString base = QStringLiteral(STUDIO_LOCAL_DATA_ROOT);
#else
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#endif
        root = base + (synthetic ? "/demo" : "/live");
    }
    root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const bool headless = collector || parser.isSet("backup-only") || parser.isSet("import-folder");
    LanguageManager language(root);
    if (!language.ready()) {
        QTextStream(stderr) << "The English translation catalog could not be loaded.\n";
        if (!headless)
            QMessageBox::critical(nullptr, QStringLiteral("Glucoid Studio"),
                                  QCoreApplication::translate("Studio", "Kielitiedostojen lataaminen epäonnistui. Sovellus ei voi käynnistyä."));
        return 1;
    }
    app->setProperty("studioLanguageController", QVariant::fromValue<QObject*>(&language));
    const auto pendingRestore = Studio::readRestoreRequest(root);
    if (pendingRestore.isEmpty() && QFileInfo::exists(Studio::restoreRequestPath(root))) Studio::clearRestoreRequest(root);
    if (!pendingRestore.isEmpty()) {
        bool restored = false;
        if (headless) {
            const auto result = Studio::restoreBackup(pendingRestore, root, true);
            restored = result.ok;
            if (!restored) QTextStream(stderr) << result.error << "\n";
        } else {
            restored = restoreWithProgress(pendingRestore, root, true);
            if (!restored)
                QMessageBox::warning(nullptr, QCoreApplication::translate("Studio", "Varmuuskopion palautus epäonnistui"),
                                     QCoreApplication::translate("Studio", "Palautuspyyntö poistettiin. Nykyinen tietokanta säilytettiin."));
        }
        Studio::clearRestoreRequest(root);
        if (!restored && headless) return 1;
    }
    if (!QFileInfo::exists(Backend::databasePath(root))) {
        const bool knownData = QFile::exists(root + "/connection.json");
        if (knownData) {
            if (headless) {
                QTextStream(stderr) << QCoreApplication::translate("Studio", "Tietokantaa ei löytynyt: %1\n").arg(Backend::databasePath(root));
                return 1;
            }
            if (startupRecovery(root, true, QCoreApplication::translate("Studio", "Tietokantaa ei löytynyt: %1").arg(Backend::databasePath(root)), true) ==
                StartupChoice::Quit)
                return 1;
        }
    }
    auto backend = std::make_unique<Backend>(root);
    if (!backend->healthy()) {
        const auto error = backend->databaseError();
        if (headless) {
            QTextStream(stderr) << QCoreApplication::translate("Studio", "Tietokannan avaaminen epäonnistui: %1\n").arg(error);
            return 1;
        }
        backend.reset();
        if (startupRecovery(root, false, error, false) == StartupChoice::Quit) return 1;
        backend = std::make_unique<Backend>(root);
        if (!backend->healthy()) {
            QMessageBox::critical(nullptr, QCoreApplication::translate("Studio", "Tietokantaa ei voitu avata"),
                                  QCoreApplication::translate("Studio", "Tietokannan avaaminen epäonnistui myös palautuksen jälkeen."));
            return 1;
        }
    }
    if (parser.isSet("backup-only")) {
        const auto target = parser.value("backup-file");
        // Headless maintenance must also choose an explicit new file.
        if (target.isEmpty() || QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            QTextStream(stderr) << "--backup-only requires --backup-file with a new absolute filename.\n";
            return 1;
        }
        return backend->backupTo(target).isEmpty() ? 1 : 0;
    }
    if (parser.isSet("import-folder")) return backend->importFolder(QFileInfo(parser.value("import-folder")).absoluteFilePath()) ? 0 : 1;
    if (parser.isSet("import-glucoid")) backend->importGlucoid();
    if (collector) {
        QTimer timer;
        timer.setInterval(60000);
        QObject::connect(&timer, &QTimer::timeout, backend.get(), &Backend::sync);
        QObject::connect(backend.get(), &Backend::syncFinished, app.get(), [&](bool ok) {
            QTextStream(stdout) << (ok ? "Sync OK\n" : "Sync unavailable; history preserved\n");
            if (parser.isSet("collect-once")) app->exit(ok ? 0 : 1);
        });
        if (!parser.isSet("collect-once")) timer.start();
        QTimer::singleShot(0, backend.get(), &Backend::sync);
        return app->exec();
    }
    const QString service =
        QStringLiteral("fi.reijo.GlucoidStudio.s") + QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(service)) {
        QDBusInterface other(service, "/Activation", "fi.reijo.GlucoidStudio.Activation", bus);
        other.call("Open");
        return other.isValid() ? 0 : 1;
    }
    QQmlApplicationEngine engine;
    QObject::connect(&language, &LanguageManager::languageChanged, &engine, &QQmlEngine::retranslate);
    engine.setInitialProperties({{"studio", QVariant::fromValue(backend.get())},
                                 {"appVersion", QCoreApplication::applicationVersion()},
                                 {"demoMode", synthetic},
                                 {"uiTestMode", false},
                                 {"uiTestWidth", 0}});
    if (synthetic) backend->seedDemo();
    backend->enableBackgroundWork();
    engine.loadFromModule("GlucoidStudio", "Main");
    if (engine.rootObjects().isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Glucoid Studio"),
                              QCoreApplication::translate("Studio", "Käyttöliittymän lataaminen epäonnistui. Sovellus ei voi käynnistyä."));
        return 1;
    }
    Activator activation;
    activation.window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    bus.registerObject("/Activation", &activation, QDBusConnection::ExportAllSlots);
    QString seenRevision = backend->dataRevision();
    QObject::connect(backend.get(), &Backend::syncFinished, backend.get(), [&](bool) { seenRevision = backend->dataRevision(); });
    QTimer changes;
    changes.setInterval(2000);
    QObject::connect(&changes, &QTimer::timeout, backend.get(), [&] {
        if (backend->busy()) return;
        backend->refreshSourceChoice();
        auto revision = backend->dataRevision();
        if (revision != seenRevision) {
            seenRevision = revision;
            backend->refresh();
        }
    });
    changes.start();
    QTimer automaticImport;
    automaticImport.setInterval(300000);
    QObject::connect(&automaticImport, &QTimer::timeout, backend.get(), [&] {
        if (!backend->busy() && backend->connection().value("configured").toBool()) backend->sync();
    });
    if (!synthetic) {
        automaticImport.start();
        QTimer::singleShot(1000, backend.get(), [&] {
            if (!backend->busy() && backend->connection().value("configured").toBool()) backend->sync();
        });
    }
    return app->exec();
}
#include "main.moc"
