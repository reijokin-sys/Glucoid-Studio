#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
using namespace Studio;
class ProductScopeTest : public QObject {
    Q_OBJECT
private slots:
    void onlyCurrentBackendOperationsAreExposed() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        for (const char* name : {"automaticMeals", "automaticSettings", "automaticBusy", "response", "responseCandidates", "profile", "diary", "episodes"})
            QCOMPARE(backend.metaObject()->indexOfProperty(name), -1);
        for (const char* signature : {"openResearchView()", "startAutomaticAnalysis(bool)", "bolus(QVariantMap)", "assess(QString,QString)", "saveReport()"})
            QCOMPARE(backend.metaObject()->indexOfMethod(signature), -1);
        QVERIFY(backend.metaObject()->indexOfMethod("backup()") >= 0);
        QVERIFY(backend.metaObject()->indexOfMethod("refreshDailyAnalysis()") >= 0);
        QVERIFY(!backend.analysis().contains("insulin"));
    }
    void historicalRecordsSurviveViewsAndRestorableBackup() {
        QTemporaryDir dir;
        FixtureStore store(dir.path()+"/studio.sqlite3");
        QVERIFY(store.run("INSERT INTO profiles(data) VALUES(?)", {"{\"synthetic\":true,\"approved\":false}"}));
        QVERIFY(store.run("INSERT INTO episodes VALUES('synthetic','{}','retained')"));
        QVERIFY(store.run("INSERT INTO calculations(time,kind,data,revision) VALUES(1,'synthetic','{}','retained')"));
        QVERIFY(store.run("INSERT INTO analysis_runs VALUES('synthetic',1,'retained','{}','{}')"));
        QVERIFY(store.setMeta("automaticMealSettings", "{\"synthetic\":true}"));
        const QStringList tables{"profiles","episodes","calculations","analysis_runs"};
        QMap<QString,QJsonArray> before;
        for(const auto& table:tables) before[table]=store.query("SELECT * FROM "+table);
        QVERIFY(QDir().mkpath(dir.path()+"/reports"));
        QFile report(dir.path()+"/reports/synthetic.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write("{\"synthetic\":true}\n");report.close();
        Backend backend(dir.path());backend.setAnalysisWindow(28,{});backend.refresh();
        const auto backup=backend.backupTo(dir.path()+"/chosen.sqlite3");QVERIFY(!backup.isEmpty());
        for(const auto& table:tables) QCOMPARE(store.query("SELECT * FROM "+table),before[table]);
        QCOMPARE(store.meta("automaticMealSettings"),QString("{\"synthetic\":true}"));
        QVERIFY(report.open(QIODevice::ReadOnly));QCOMPARE(report.readAll(),QByteArray("{\"synthetic\":true}\n"));
        QProcess restore;
        restore.start("python3", {QStringLiteral(STUDIO_SOURCE_DIR)+"/tools/restore-backup.py","--backup",backup,"--data-dir",dir.path()+"/restored"});
        QVERIFY(restore.waitForFinished());QCOMPARE(restore.exitCode(),0);
        FixtureStore restored(dir.path()+"/restored/studio.sqlite3");QVERIFY(restored.good());
        for(const auto& table:tables) QCOMPARE(restored.query("SELECT * FROM "+table),before[table]);
    }
    void existingObservationKeepsOnlyItsVisibleGlucoseResult() {
        const QTimeZone zone("UTC");
        const QDate day=QDate::currentDate().addDays(-2);
        const auto start=day.startOfDay(zone).toMSecsSinceEpoch();
        QJsonObject profile{{"schema",1},{"researchOnly",true},{"historicalConfirmed",true},{"timezone","UTC"},
            {"effectiveFrom",double(start+3600000)},{"rapidMedicine","Synthetic rapid"},{"basalMedicine","Synthetic basal"},
            {"basalDose",12},{"basalTime","22:00"},{"targetLow",4},{"targetHigh",10},
            {"slots",QJsonArray{QJsonObject{{"name","all"},{"startMinute",0},{"icr",10}}}}};
        const QList<Interval> data{{start,start+7200000,6,"stream","synthetic"}};
        const auto result=observeProfile(profile,data,day,1,zone);
        QCOMPARE(result["status"].toString(),QString("observing"));
        QCOMPARE(result["glucose"].toObject()["inRange"].toDouble(),1.0);
        QVERIFY(qAbs(result["glucose"].toObject()["coverage"].toDouble()-1.0/23)<1e-12);
        QVERIFY(!result.contains("daily"));QVERIFY(!result.contains("basalComparedDays"));
        QCOMPARE(observeProfile(profile,data,day,1,QTimeZone("Europe/Helsinki"))["status"].toString(),QString("timezone_mismatch"));
        QVERIFY(!observeProfile({},data,day,1,zone)["active"].toBool());
    }
};
QTEST_GUILESS_MAIN(ProductScopeTest)
#include "test_product_scope.moc"
