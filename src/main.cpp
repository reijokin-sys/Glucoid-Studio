// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "backend.h"
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
#include <QQuickItem>
#include <QJSValue>
#include <QQmlListReference>
#include <QtGraphs/QXYSeries>
#include <QPointer>
#include <QSaveFile>
#include <QJsonDocument>
#include <QPalette>
#include <algorithm>
#include <numeric>
#include <QQuickStyle>
#include <functional>

static QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    const auto children = root->childItems();
    const auto found = std::find_if(children.cbegin(), children.cend(), [&](QQuickItem* child) { return findItem(child, name) != nullptr; });
    return found == children.cend() ? nullptr : findItem(*found, name);
}
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
int main(int argc, char** argv) {
    bool collector = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--collect" || QByteArray(argv[i]) == "--collect-once" || QByteArray(argv[i]) == "--automatic-report") collector = true;
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
    parser.addOption({"smoke-test", QCoreApplication::translate("Studio", "Tarkista käyttöliittymän latautuminen ja lopeta.")});
    parser.addOption(
        {"verify-existing-ui", QCoreApplication::translate("Studio", "Tarkista olemassa olevan historian näkymät lisäämättä esimerkkiaineistoa.")});
    parser.addOption(
        {"activate-observation-profile", QCoreApplication::translate("Studio", "Ota viimeisin hoitoprofiili seurantaan ilman lääkityslaskennan hyväksyntää.")});
    parser.addOption({"automatic-report", QCoreApplication::translate("Studio", "Laske ateria- ja yöraportti tietohakemistoon ilman puhelinhakua.")});
    parser.addOption({"backup-only", QCoreApplication::translate("Studio", "Varmuuskopioi tietokanta ja alkuperäisviennit ja lopeta.")});
    parser.addOption({"research", QCoreApplication::translate("Studio", "Avaa aiemmat manuaaliset tutkimustyökalut.")});
    parser.addOption({"legacy", QCoreApplication::translate("Studio", "Avaa dokumentoidut aiemmat päiväkirja-/reaaliaikatoiminnot.")});
    parser.addOption({"import-folder", QCoreApplication::translate("Studio", "Tuo alkuperäiset TSV-viennit paikallisesta kansiosta ja lopeta."), "path"});
    parser.addOption({"chart-stress-test",
                      QCoreApplication::translate("Studio", "Testaa kuvaajan toistuvat päivitykset ja aikavälien vaihdot keinotekoisella aineistolla.")});
    parser.addPositionalArgument("url", "glucoid-studio://today", "[url]");
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
    const bool synthetic = parser.isSet("demo") || parser.isSet("smoke-test") || parser.isSet("chart-stress-test");
    const bool testing = parser.isSet("smoke-test") || parser.isSet("chart-stress-test") || parser.isSet("verify-existing-ui");
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
    LanguageManager language(root);
    if (!language.ready()) {
        QTextStream(stderr) << "The English translation catalog could not be loaded.\n";
        return 1;
    }
    app->setProperty("studioLanguageController", QVariant::fromValue<QObject*>(&language));
    Backend backend(root);
    if (!backend.healthy()) {
        QTextStream(stderr) << QCoreApplication::translate("Studio", "Tietokannan avaaminen epäonnistui.\n");
        return 1;
    }
    if (parser.isSet("automatic-report")) {
        bool written = false;
        QObject::connect(&backend, &Backend::changed, app.get(), [&] {
            if (written || backend.automaticBusy()) return;
            auto result = QJsonObject::fromVariantMap(backend.automaticMeals());
            if (result["status"] == "computing" || result.isEmpty()) return;
            if (result["status"] != "ready") {
                QTextStream(stderr) << QCoreApplication::translate("Studio", "Raportin laskenta ei onnistunut.\n");
                app->exit(1);
                return;
            }
            QSaveFile out(root + "/automatic-report.json");
            if (!out.open(QIODevice::WriteOnly)) {
                app->exit(1);
                return;
            }
            out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
            written = out.write(bytes) == bytes.size() && out.commit();
            QTextStream(stdout) << (written ? QCoreApplication::translate("Studio", "Automaattinen raportti tallennettu tietohakemistoon.\n")
                                            : QCoreApplication::translate("Studio", "Raportin tallennus epäonnistui.\n"));
            app->exit(written ? 0 : 1);
        });
        QTimer::singleShot(0, &backend, [&] { backend.startAutomaticAnalysis(false); });
        QTimer::singleShot(60000, app.get(), [&] { app->exit(1); });
        return app->exec();
    }
    if (parser.isSet("activate-observation-profile")) {
        auto result = backend.activateObservationProfile(backend.profile());
        QTextStream(stdout) << (result["status"] == "active" ? "Observation activation OK\n" : "Observation activation blocked\n");
        return result["status"] == "active" ? 0 : 1;
    }
    if (parser.isSet("backup-only")) return backend.backup().isEmpty() ? 1 : 0;
    if (parser.isSet("import-folder")) return backend.importFolder(QFileInfo(parser.value("import-folder")).absoluteFilePath()) ? 0 : 1;
    if (parser.isSet("import-glucoid")) backend.importGlucoid();
    if (collector) {
        QTimer timer;
        timer.setInterval(60000);
        QObject::connect(&timer, &QTimer::timeout, &backend, &Backend::sync);
        QObject::connect(&backend, &Backend::syncFinished, app.get(), [&](bool ok) {
            QTextStream(stdout) << (ok ? "Sync OK\n" : "Sync unavailable; history preserved\n");
            if (parser.isSet("collect-once")) app->exit(ok ? 0 : 1);
        });
        if (!parser.isSet("collect-once")) timer.start();
        QTimer::singleShot(0, &backend, &Backend::sync);
        return app->exec();
    }
    const bool simple = !parser.isSet("legacy") && !parser.isSet("research");
    const QString service = QString(simple                     ? "fi.reijo.GlucoidStudio.s"
                                    : parser.isSet("research") ? "fi.reijo.GlucoidStudio.research.s"
                                                               : "fi.reijo.GlucoidStudio.legacy.s") +
                            QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    auto bus = QDBusConnection::sessionBus();
    if (!testing && !bus.registerService(service)) {
        QDBusInterface other(service, "/Activation", "fi.reijo.GlucoidStudio.Activation", bus);
        other.call("Open");
        return other.isValid() ? 0 : 1;
    }
    QQmlApplicationEngine engine;
    QObject::connect(&language, &LanguageManager::languageChanged, &engine, &QQmlEngine::retranslate);
    engine.setInitialProperties({{"studio", QVariant::fromValue(&backend)},
                                 {"appVersion", QCoreApplication::applicationVersion()},
                                 {"demoMode", synthetic},
                                 {"uiTestMode", testing},
                                 {"uiTestWidth", testing ? qEnvironmentVariableIntValue("STUDIO_UI_TEST_WIDTH") : 0}});
    if (synthetic) backend.seedDemo();
    if (simple) backend.enableBackgroundWork();
    engine.loadFromModule("GlucoidStudio", parser.isSet("legacy") ? "LegacyMain" : parser.isSet("research") ? "ResearchMain" : "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    Activator activation;
    activation.window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!testing) bus.registerObject("/Activation", &activation, QDBusConnection::ExportAllSlots);
    QTimer refresh;
    refresh.setInterval(15000);
    QObject::connect(&refresh, &QTimer::timeout, &backend, &Backend::refresh);
    if (parser.isSet("legacy")) refresh.start();
    QString seenRevision = backend.dataRevision();
    QObject::connect(&backend, &Backend::syncFinished, &backend, [&](bool) { seenRevision = backend.dataRevision(); });
    QTimer changes;
    changes.setInterval(2000);
    QObject::connect(&changes, &QTimer::timeout, &backend, [&] {
        if (backend.busy()) return;
        backend.refreshSourceChoice();
        auto revision = backend.dataRevision();
        if (revision != seenRevision) {
            seenRevision = revision;
            backend.refresh();
        }
    });
    if (!testing && !collector) changes.start();
    QTimer automaticImport;
    automaticImport.setInterval(300000);
    QObject::connect(&automaticImport, &QTimer::timeout, &backend, [&] {
        if (!backend.busy() && backend.connection().value("configured").toBool()) backend.sync();
    });
    if (simple && !testing && !synthetic) {
        automaticImport.start();
        QTimer::singleShot(1000, &backend, [&] {
            if (!backend.busy() && backend.connection().value("configured").toBool()) backend.sync();
        });
    }
    if (parser.isSet("chart-stress-test")) {
        QMetaObject::invokeMethod(engine.rootObjects().first(), "showView", Q_ARG(QVariant, parser.isSet("legacy") ? 0 : 2));
        auto* timer = new QTimer(app.get());
        timer->setInterval(400);
        auto step = std::make_shared<int>(0);
        auto checks = std::make_shared<int>(0);
        auto settling = std::make_shared<int>(0);
        auto first = std::make_shared<QPointer<QObject>>();
        auto lastViewport = std::make_shared<QPointer<QObject>>();
        auto frames = std::make_shared<int>(0);
        QObject::connect(activation.window, &QQuickWindow::frameSwapped, app.get(), [frames] { ++*frames; });
        QObject::connect(timer, &QTimer::timeout, app.get(), [&, timer, step, checks, settling, first, lastViewport, frames] {
            auto* component = findItem(activation.window->contentItem(), "glucoseChart");
            QObject* viewport = nullptr;
            if (component) {
                auto v = component->property("graph");
                viewport = v.value<QObject*>();
                if (!viewport) viewport = v.value<QJSValue>().toQObject();
            }
            QQmlListReference series(viewport, "seriesList");
            if (!viewport || !series.count()) {
                if (++*step > 10) {
                    QTextStream(stderr) << "Stress test: chart did not initialize\n";
                    app->exit(1);
                }
                return;
            }
            if (!*first) {
                if (*checks) {
                    QTextStream(stderr) << "Stress test: a live series was destroyed during refresh\n";
                    app->exit(1);
                    return;
                }
                *first = series.at(0);
                *lastViewport = viewport;
            }
            bool retained = false;
            for (qsizetype i = 0; i < series.count(); ++i) retained |= first->data() == series.at(i);
            if (!retained || lastViewport->data() != viewport) {
                QTextStream(stderr) << "Stress test: chart/series identity changed during refresh\n";
                app->exit(1);
                return;
            }
            qsizetype points = 0;
            for (qsizetype i = 0; i < series.count(); ++i)
                if (auto* s = qobject_cast<QXYSeries*>(series.at(i))) points += s->count();
            qsizetype expected = 0;
            const auto chart = backend.chart();
            for (auto seg : chart.value("segments").toList()) expected += seg.toList().size();
            if (chart.value("low").toDouble() > 0) expected += 2;
            if (chart.value("high").toDouble() > 0) expected += 2;
            if (simple && component) expected = component->property("expectedPointCount").toInt();
            // Qt Graphs polishing and the QML debounce are asynchronous.
            // Permit a bounded settling interval without scheduling another change.
            if (points != expected) {
                if (++*settling <= 10) return;
                QTextStream(stderr) << "Stress test: missing/old points after refresh cycle=" << *checks << " actual=" << points << " expected=" << expected
                                    << "\n";
                app->exit(1);
                return;
            }
            *settling = 0;
            ++*checks;
            if (*checks >= 80) {
                const bool ok = *frames > 0;
                QTextStream(stdout) << "Chart stress: 80 updates, stable series, exact points, rendered=" << ok << "\n";
                timer->stop();
                app->exit(ok ? 0 : 1);
                return;
            }
            const int windows[] = {1, 7, 14, 30, 14, 7, 1, 1};
            backend.setWindow("", windows[*checks % 8]);
            if (!parser.isSet("legacy")) {
                const int periods[] = {7, 14, 28};
                backend.setAnalysisWindow(periods[*checks % 3], "");
                QMetaObject::invokeMethod(engine.rootObjects().first(), "showView", Q_ARG(QVariant, *checks % 3));
            }
        });
        timer->start();
    }
    if (parser.isSet("smoke-test") || parser.isSet("verify-existing-ui")) {
        const bool identityOk = QCoreApplication::applicationName() == "glucoid-studio" && QGuiApplication::applicationDisplayName() == "Glucoid Studio" &&
                                QGuiApplication::desktopFileName() == "fi.reijo.glucoidstudio";
        QTextStream(stdout) << "Desktop identity=" << identityOk << "\n";
        const auto iconImage = QGuiApplication::windowIcon().pixmap(256, 256).toImage();
        const bool iconOk = !iconImage.isNull() && iconImage.pixelColor(128, 32) == QColor("#4c0667");
        QTextStream(stdout) << "Studio icon=" << iconOk << "\n";
        if (!identityOk || !iconOk) return 1;
        auto* cycle = new QTimer(app.get());
        cycle->setInterval(350);
        auto index = std::make_shared<int>(0);
        auto renderedFrames = std::make_shared<int>(0);
        auto renderWaits = std::make_shared<int>(0);
        QObject::connect(activation.window, &QQuickWindow::frameSwapped, app.get(), [renderedFrames] { ++*renderedFrames; });
        QObject::connect(cycle, &QTimer::timeout, app.get(), [&, cycle, index, renderedFrames, renderWaits] {
            if (simple && backend.summaryBusy()) {
                if (++*renderWaits > 85) {
                    QTextStream(stderr) << "Background summary timed out\n";
                    app->exit(1);
                }
                return;
            }
            if (simple && *index <= 8) {
                auto* root = engine.rootObjects().first();
                if (*index == 0) {
                    if (backend.automaticBusy() || backend.automaticMeals().value("status") == "computing") {
                        if (++*renderWaits > 85) {
                            QTextStream(stderr) << "Automatic calculation timed out\n";
                            app->exit(1);
                        }
                        return;
                    }
                    *renderWaits = 0;
                    QMetaObject::invokeMethod(root, "showView", Q_ARG(QVariant, 0));
                    ++*index;
                    return;
                }
                if (*index == 1) {
                    auto* tabs = root->findChild<QQuickItem*>("mainTabs");
                    auto* metrics = root->findChild<QQuickItem*>("summaryMetrics");
                    auto fits = [&](QQuickItem* v) {
                        if (!v) return false;
                        auto r = v->mapRectToScene(v->boundingRect());
                        return v->width() > 0 && r.left() >= -1 && r.right() <= activation.window->width() + 1;
                    };
                    auto* week1 = findItem(activation.window->contentItem(), "analysisPeriod7");
                    auto* week4 = findItem(activation.window->contentItem(), "analysisPeriod28");
                    auto* months3 = findItem(activation.window->contentItem(), "analysisPeriod90");
                    const bool periods = week1 && week4 && months3 && week1->property("text").toString() == QCoreApplication::translate("Studio", "1 vk") &&
                                         week4->property("text").toString() == QCoreApplication::translate("Studio", "4 vk") &&
                                         months3->property("text").toString() == QCoreApplication::translate("Studio", "3 kk") &&
                                         week1->property("active").toBool();
                    auto* dayChart = findItem(activation.window->contentItem(), "summaryCurrentDayChart");
                    auto* profileLegend = findItem(activation.window->contentItem(), "profileLegendSummary");
                    const bool currentDay =
                        dayChart && dayChart->isVisible() && profileLegend && dayChart->property("paintedPoints").toInt() > 0 &&
                        dayChart->mapRectToScene(dayChart->boundingRect()).top() < profileLegend->mapRectToScene(profileLegend->boundingRect()).top();
                    auto* gmi = findItem(activation.window->contentItem(), "gmiMetric");
                    const bool gmiOk = gmi && gmi->property("value").toString() != "—" && gmi->property("unit").toString() == "%";
                    auto *meanCard = findItem(activation.window->contentItem(), "meanMetric"), *cvCard = findItem(activation.window->contentItem(), "cvMetric");
                    const bool references = meanCard && cvCard && meanCard->property("valueColor").value<QColor>() == QColor("#5fd0b0") &&
                                            cvCard->property("valueColor").value<QColor>() == QColor("#5fd0b0");
                    auto* page = findItem(activation.window->contentItem(), "mainApplicationPage");
                    const bool versionVisible = root->property("title").toString().contains(QCoreApplication::applicationVersion()) && page &&
                                                page->property("title").toString().contains(QCoreApplication::applicationVersion());
                    const bool ok = tabs && tabs->property("count").toInt() == 3 && fits(tabs) && fits(metrics) && periods && currentDay && gmiOk &&
                                    references && versionVisible && !root->findChild<QObject*>("saveReviewedEpisode");
                    QTextStream(stdout) << "Simple navigation=3 summary-fits=" << ok << " period-buttons=" << periods << " current-day-chart=" << currentDay
                                        << " gmi=" << gmiOk << " references=" << references << " version-visible=" << versionVisible << "\n";
                    if (!ok) {
                        app->exit(1);
                        return;
                    }
                    QMetaObject::invokeMethod(root, "showView", Q_ARG(QVariant, 3));
                    ++*index;
                    return;
                }
                if (*index == 2) {
                    const auto groups = backend.dailyAnalysis().value("groups").toList();
                    const bool ok = backend.dailyAnalysis().value("status") == "ready" && groups.size() == 7;
                    auto* grid = findItem(activation.window->contentItem(), "dailyPeriodGrid");
                    auto *card0 = findItem(activation.window->contentItem(), "dailyPeriodCard0"),
                         *card1 = findItem(activation.window->contentItem(), "dailyPeriodCard1");
                    const int gridWidth = grid ? int(grid->width()) : 0;
                    const int expectedColumns = gridWidth >= 1150 ? 3 : gridWidth >= 700 ? 2 : 1;
                    const bool gridOk = grid && grid->property("columns").toInt() == expectedColumns && card0 && card1 &&
                                        (expectedColumns == 1 ? card1->mapToScene(QPointF()).y() > card0->mapToScene(QPointF()).y()
                                                              : qAbs(card0->mapToScene(QPointF()).y() - card1->mapToScene(QPointF()).y()) < 1);
                    QTextStream(stdout) << "Daily analysis ready=" << ok << " groups=" << groups.size() << " grid=" << gridOk << "\n";
                    if (!ok || !gridOk) {
                        if (++*renderWaits < 85) return;
                        app->exit(1);
                        return;
                    }

                    QMetaObject::invokeMethod(root, "showView", Q_ARG(QVariant, 1));
                    ++*index;
                    return;
                }
                *index = 9;
                *renderedFrames = 0;
                QMetaObject::invokeMethod(root, "showView", Q_ARG(QVariant, 2));
                activation.window->update();
                return;
            }
            if (*index == 8) {
                int cards = 0;
                std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
                    if (item->objectName().startsWith("responseGroupCard") && item->window() == activation.window && item->width() > 0 && item->height() > 0)
                        ++cards;
                    for (auto* child : item->childItems()) visit(child);
                };
                visit(activation.window->contentItem());
                if (!parser.isSet("legacy")) {
                    auto* tracking = engine.rootObjects().first()->findChild<QObject*>("observationSummary");
                    const bool expectedActive = backend.observation().value("active").toBool();
                    const bool bound = tracking && tracking->property("dataActive").toBool() == expectedActive;
                    QTextStream(stdout) << "Observation active=" << expectedActive << " view-bound=" << bound << "\n";
                    if (!bound) {
                        app->exit(1);
                        return;
                    }
                    auto* approval = engine.rootObjects().first()->findChild<QObject*>("approveResponseProfile");
                    auto* scope = engine.rootObjects().first()->findChild<QQuickItem*>("responseEstimationScope");
                    const bool estimationOnly =
                        backend.response().value("status").toString() == "evaluated" && !backend.response().value("medicationApproved").toBool();
                    const bool scopeBound = scope && scope->isVisible() == estimationOnly;
                    QTextStream(stdout) << "Response approval control=" << (approval != nullptr) << " estimation-only=" << estimationOnly
                                        << " scope-bound=" << scopeBound << "\n";
                    if (!approval || !scopeBound) {
                        app->exit(1);
                        return;
                    }
                    bool collapsed = true;
                    for (auto name : {"summaryStatistics", "dailyEntries", "connectionDetails", "profileApprovals", "legacyAssessment", "responsePolicySection",
                                      "responseCandidatesSection"}) {
                        auto* section = engine.rootObjects().first()->findChild<QObject*>(name);
                        collapsed &= section && !section->property("expanded").toBool();
                    }
                    auto* policy = engine.rootObjects().first()->findChild<QObject*>("responsePolicySection");
                    QPointer<QObject> field;
                    std::function<void(QQuickItem*)> findField = [&](QQuickItem* item) {
                        if (item->objectName() == "responsePolicyFieldminimumEpisodes") field = item;
                        for (auto* child : item->childItems()) findField(child);
                    };
                    findField(activation.window->contentItem());
                    const auto fieldText = field ? field->property("text") : QVariant();
                    bool retained = policy && field;
                    if (retained) {
                        retained = QMetaObject::invokeMethod(policy, "toggle") && policy->property("expanded").toBool();
                        retained = QMetaObject::invokeMethod(policy, "toggle") && !policy->property("expanded").toBool() && retained;
                        retained = retained && field && field->property("text") == fieldText;
                    }
                    auto* navigation = engine.rootObjects().first()->findChild<QQuickItem*>("mainTabs");
                    auto* metrics = engine.rootObjects().first()->findChild<QQuickItem*>("summaryMetrics");
                    auto* grid = engine.rootObjects().first()->findChild<QQuickItem*>("responseCards");
                    auto fits = [&](QQuickItem* item) {
                        if (!item) return false;
                        const auto rect = item->mapRectToScene(item->boundingRect());
                        return item->width() > 0 && rect.left() >= -1 && rect.right() <= activation.window->width() + 1;
                    };
                    const bool layout = fits(navigation) && fits(metrics) && fits(grid) && grid->property("columns").toInt() == (grid->width() >= 1000 ? 2 : 1);
                    int labelChars = 0;
                    std::function<void(QQuickItem*)> labels = [&](QQuickItem* item) {
                        if (item->isVisible() && item->inherits("QQuickLabel")) labelChars += item->property("text").toString().size();
                        for (auto* child : item->childItems()) labels(child);
                    };
                    labels(activation.window->contentItem());
                    QTextStream(stdout) << "UI clarity width=" << activation.window->width() << " collapsed=" << collapsed << " fields-retained=" << retained
                                        << " layout-fits=" << layout << " visible-label-chars=" << labelChars << "\n";
                    if (!collapsed || !retained || !layout || labelChars > 3000) {
                        app->exit(1);
                        return;
                    }
                }
                const int expected = backend.response().value("status").toString() == "evaluated" ? 4 : 0;
                QTextStream(stdout) << "Response cards=" << cards << " expected=" << expected << "\n";
                if (cards != expected) {
                    app->exit(1);
                    return;
                }
                *renderedFrames = 0;
                QMetaObject::invokeMethod(engine.rootObjects().first(), "showView", Q_ARG(QVariant, parser.isSet("legacy") ? 0 : 2));
                activation.window->update();
                ++*index;
                return;
            }
            if (*index > 8) {
                auto* graph = findItem(activation.window->contentItem(), "glucoseChart");
                QObject* viewport = nullptr;
                if (graph) {
                    auto value = graph->property("graph");
                    viewport = value.value<QObject*>();
                    if (!viewport) viewport = value.value<QJSValue>().toQObject();
                }
                auto* item = qobject_cast<QQuickItem*>(viewport);
                QQmlListReference series(viewport, "seriesList");
                qsizetype points = 0;
                for (qsizetype i = 0; i < series.count(); ++i)
                    if (auto* line = qobject_cast<QXYSeries*>(series.at(i))) points += line->count();
                const bool attached = item && item->window() == activation.window;
                const bool sized = item && item->width() > 0 && item->height() > 0;
                qsizetype expectedPoints = 0;
                const auto chart = backend.chart();
                const auto segments = chart.value("segments").toList();
                expectedPoints = std::accumulate(segments.cbegin(), segments.cend(), qsizetype(0),
                                                 [](qsizetype count, const QVariant& segment) { return count + segment.toList().size(); });
                if (chart.value("low").toDouble() > 0) expectedPoints += 2;
                if (chart.value("high").toDouble() > 0) expectedPoints += 2;
                if (simple && graph) expectedPoints = graph->property("expectedPointCount").toInt();
                const bool populated = points > 0 && points == expectedPoints;
                const bool visible = item && item->isVisible();
                const auto plot = viewport ? viewport->property("plotArea").toRectF() : QRectF();
                const bool plotted = plot.width() > 0 && plot.height() > 0;
                const bool rendered = *renderedFrames > 0;
                if (!rendered && *renderWaits == 0) {
                    auto rect = item ? item->mapRectToScene(item->boundingRect()) : QRectF();
                    QTextStream(stdout) << "Render geometry exposed=" << activation.window->isExposed() << " window=" << activation.window->width() << "x"
                                        << activation.window->height() << " viewport=" << rect.x() << "," << rect.y() << "," << rect.width() << ","
                                        << rect.height() << "\n";
                }
                if (!rendered && ++*renderWaits <= 10) {
                    activation.window->raise();
                    activation.window->requestActivate();
                    activation.window->update();
                    return;
                }
                auto* agp = engine.rootObjects().first()->findChild<QObject*>("agpCanvas");
                const bool agpReady = parser.isSet("legacy") || (agp && agp->property("paintedBins").toInt() > 0);
                const bool ok = agpReady && graph && attached && sized && populated && visible && plotted && rendered;
                QTextStream(stdout) << "Chart attached=" << attached << " viewport-sized=" << sized << " agp-painted=" << agpReady << " populated=" << populated
                                    << " visible=" << visible << " plot-area=" << plotted << " rendered-frame=" << rendered << "\n";
                QTextStream(stdout) << (ok ? "QML loaded; all views and actual chart viewport ready\n" : "Chart rendering prerequisites failed\n");
                cycle->stop();
                app->exit(ok ? 0 : 1);
                return;
            }
            if (*index == 7 && !parser.isSet("legacy")) backend.analyseResponse(28);
            QMetaObject::invokeMethod(engine.rootObjects().first(), "showView", Q_ARG(QVariant, *index));
            ++*index;
        });
        cycle->start();
    }
    return app->exec();
}
#include "main.moc"
