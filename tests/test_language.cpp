#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "language.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <QScopeGuard>
#include <QLocale>

class LanguageTest : public QObject {
    Q_OBJECT
    void attach(LanguageManager& language) {
        QCoreApplication::instance()->setProperty("studioLanguageController", QVariant::fromValue<QObject*>(&language));
    }
  private slots:
    void dayChartLabelsFollowUiLanguageInsteadOfSystemLocale() {
        const auto previous=QLocale();QLocale::setDefault(QLocale("fi_FI"));
        const auto restoreLocale=qScopeGuard([&]{QLocale::setDefault(previous);});
        QTemporaryDir dir;LanguageManager language(dir.path());attach(language);
        Backend backend(dir.path());
        const auto today=QDate::fromString(backend.today(),Qt::ISODate);
        QCOMPARE(backend.currentDayChart()["dateLabel"].toString(),QLocale("en_GB").toString(today,"ddd d.M.yyyy"));
        const auto first=backend.dayComparisonCharts().first().toMap();
        QCOMPARE(first["dateLabel"].toString(),QLocale("en_GB").toString(QDate::fromString(first["date"].toString(),Qt::ISODate),"ddd d.M.yyyy"));
        QVERIFY(language.setLanguage("fi"));
        QCOMPARE(backend.currentDayChart()["dateLabel"].toString(),QLocale("fi_FI").toString(today,"ddd d.M.yyyy"));
    }

    void cleanup() { QCoreApplication::instance()->setProperty("studioLanguageController", QVariant()); }
    void defaultEnglishAndPersistentFinnish() {
        QTemporaryDir dir;
        {
            LanguageManager language(dir.path());
            QVERIFY(language.ready());
            QCOMPARE(language.language(), QString("en"));
            QCOMPARE(QCoreApplication::translate("Studio", "Asetukset"), QString("Settings"));
            QVERIFY(!language.setLanguage("sv"));
            QCOMPARE(language.language(), QString("en"));
            QVERIFY(language.setLanguage("fi"));
            QCOMPARE(QCoreApplication::translate("Studio", "Asetukset"), QString("Asetukset"));
        }
        {
            LanguageManager language(dir.path());
            QCOMPARE(language.language(), QString("fi"));
            QVERIFY(language.setLanguage("en"));
        }
        LanguageManager language(dir.path());
        QCOMPARE(language.language(), QString("en"));
        QFile settings(dir.path()+"/ui-language.json"); QVERIFY(settings.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(settings.readAll()).object()["language"].toString(), QString("en"));
        QCOMPARE(settings.permissions() & (QFileDevice::ReadGroup|QFileDevice::ReadOther), QFileDevice::Permissions{});
    }
    void qmlRetranslatesAndBackendKeepsDataAndConnection() {
        QTemporaryDir dir;
        LanguageManager language(dir.path()); attach(language);
        Backend backend(dir.path());
        backend.seedDemo();
        QFile connection(dir.path()+"/connection.json"); QVERIFY(connection.open(QIODevice::WriteOnly));
        const QByteArray bytes="{\"source\":\"juggluco\",\"mode\":\"usb\",\"auth\":\"none\"}\n";
        QCOMPARE(connection.write(bytes), qint64(bytes.size())); connection.close();
        Studio::FixtureStore store(dir.path()+"/studio.sqlite3");
        const auto revision=store.revision();
        QQmlEngine engine;
        connect(&language,&LanguageManager::languageChanged,&engine,&QQmlEngine::retranslate);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject { property string label: qsTranslate(\"Studio\", \"Asetukset\") }",QUrl());
        QScopedPointer<QObject> object(component.create()); QVERIFY2(object,qPrintable(component.errorString()));
        QCOMPARE(object->property("label").toString(),QString("Settings"));
        QVERIFY(backend.setLanguage("fi"));
        QCOMPARE(object->property("label").toString(),QString("Asetukset"));
        QCOMPARE(backend.status(),QString("Käyttöliittymän kieli vaihdettu."));
        QVERIFY(backend.setLanguage("en"));
        QCOMPARE(object->property("label").toString(),QString("Settings"));
        QCOMPARE(backend.status(),QString("Interface language changed."));
        QCOMPARE(store.revision(),revision);
        QVERIFY(connection.open(QIODevice::ReadOnly)); QCOMPARE(connection.readAll(),bytes);
    }
    void languageChangeDiscardsOlderBackgroundPresentation() {
        QTemporaryDir dir;
        LanguageManager language(dir.path()); attach(language);
        Backend backend(dir.path()); backend.seedDemo(); backend.enableBackgroundWork();
        backend.setAnalysisWindow(90,QString());
        QVERIFY(backend.summaryBusy());
        QVERIFY(backend.setLanguage("fi"));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.summaryBusy(),30000);
        QCOMPARE(backend.language(),QString("fi"));
        QCOMPARE(backend.analysis()["days"].toInt(),90);
        const QTimeZone zone("Europe/Helsinki");
        const auto expected=QLocale("fi_FI").toString(QDateTime::currentDateTime(zone).date(),"ddd d.M.yyyy");
        QCOMPARE(backend.currentDayChart()["dateLabel"].toString(),expected);
        QVERIFY(backend.setLanguage("en"));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.summaryBusy(),30000);
        QCOMPARE(backend.currentDayChart()["dateLabel"].toString(),QLocale("en_GB").toString(QDateTime::currentDateTime(zone).date(),"ddd d.M.yyyy"));
    }
};
QTEST_GUILESS_MAIN(LanguageTest)
#include "test_language.moc"
