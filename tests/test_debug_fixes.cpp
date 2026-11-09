// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "daychart.h"
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <ctime>

using namespace Studio;

namespace {
QByteArray streamAt(qint64 time, int number = 1) {
    return "Sensorid\tnr\tUnixTime\tmmol/L\nsynthetic\t"
        + QByteArray::number(number) + '\t' + QByteArray::number(time / 1000) + "\t6.0\n";
}

Reading sample(qint64 time, const QString &kind = "stream") {
    Reading reading;
    reading.time = time;
    reading.key = kind + QString::number(time);
    reading.sensor = "synthetic";
    reading.mmol = reading.original = 6;
    reading.originalUnit = "mmol/L";
    reading.seriesKind = kind;
    return reading;
}

Amount amount(qint64 time, double value) {
    Amount entry;
    entry.source = "synthetic";
    entry.time = time;
    entry.value = value;
    entry.label = "synthetic";
    entry.key = fingerprint(entry) + ":0";
    return entry;
}

QString functionBody(const QString &source, const QString &marker) {
    const auto found = source.indexOf(marker);
    if (found < 0) return {};
    const auto start = source.indexOf('{', found);
    if (start < 0) return {};
    auto end = start;
    int depth = 0;
    do {
        if (source[end] == '{') ++depth;
        if (source[end] == '}') --depth;
        ++end;
    } while (depth && end < source.size());
    return depth ? QString() : source.mid(start, end - start);
}
}

class DebugFixesTest : public QObject {
    Q_OBJECT
private slots:
    void comparisonColorSegmentsPreserveCrossingsAndGaps() {
        QFile file(QStringLiteral(STUDIO_SOURCE_DIR)+"/qml/chartsegments.js");QVERIFY(file.open(QIODevice::ReadOnly));
        auto source=QString::fromUtf8(file.readAll());source.remove(".pragma library");QJSEngine engine;
        QVERIFY(!engine.evaluate(source).isError());
        auto evaluate=[&](const QString&points,double low=4,double high=10){
            auto result=engine.evaluate("JSON.stringify(split("+points+","+QString::number(low)+","+QString::number(high)+"))");
            return QJsonDocument::fromJson(result.toString().toUtf8()).array();
        };
        const auto crossing=evaluate("[[{x:0,y:2},{x:100,y:12}]]");QCOMPARE(crossing.size(),3);
        QCOMPARE(crossing[0].toObject()["outside"].toBool(),true);
        QCOMPARE(crossing[1].toObject()["outside"].toBool(),false);
        QCOMPARE(crossing[2].toObject()["outside"].toBool(),true);
        auto middle=crossing[1].toObject()["points"].toArray();
        QCOMPARE(middle[0].toObject()["x"].toDouble(),20.0);QCOMPARE(middle[0].toObject()["y"].toDouble(),4.0);
        QCOMPARE(middle[1].toObject()["x"].toDouble(),80.0);QCOMPARE(middle[1].toObject()["y"].toDouble(),10.0);
        const auto edges=evaluate("[[{x:0,y:4},{x:100,y:10}]]");QCOMPARE(edges.size(),1);QVERIFY(!edges[0].toObject()["outside"].toBool());
        const auto gaps=evaluate("[[{x:0,y:2},{x:10,y:3}],[{x:100,y:2},{x:110,y:3}]]");QCOMPARE(gaps.size(),2);
        QCOMPARE(gaps[0].toObject()["points"].toArray().last().toObject()["x"].toDouble(),10.0);
        QCOMPARE(gaps[1].toObject()["points"].toArray().first().toObject()["x"].toDouble(),100.0);
        const auto unset=evaluate("[[{x:0,y:2},{x:100,y:12}]]",0,10);QCOMPARE(unset.size(),1);QVERIFY(!unset[0].toObject()["outside"].toBool());
        const auto single=evaluate("[[{x:0,y:12}]]");QCOMPARE(single.size(),1);QVERIFY(single[0].toObject()["outside"].toBool());
    }
    void fourComparisonDaysUseLocalDatesAndPreserveEntries_data() {
        QTest::addColumn<QDate>("anchor");QTest::newRow("spring")<<QDate(2026,3,30);QTest::newRow("autumn")<<QDate(2026,10,26);
    }
    void fourComparisonDaysUseLocalDatesAndPreserveEntries() {
        QFETCH(QDate,anchor);const QTimeZone zone("Europe/Helsinki");QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");
        const QJsonObject settings{{"timezone","Europe/Helsinki"},{"low",4},{"high",10},{"maxGapMinutes",1},{"fillHistory",false}};
        QVERIFY(store.setMeta("analysisSettings",QString::fromUtf8(QJsonDocument(settings).toJson())));
        QList<Reading> rows;
        for(int offset=0;offset<4;++offset){
            const auto at=anchor.addDays(-offset).startOfDay(zone).toMSecsSinceEpoch();
            rows<<sample(at,"history")<<sample(at+60000);
            const auto noon=QDateTime(anchor.addDays(-offset),QTime(12,0),zone).toMSecsSinceEpoch();
            QVERIFY(!store.addLocal(noon,20,"carbs","","synthetic").isEmpty());
            const auto id=store.addLocal(noon+60000,2,"rapid","Synthetic rapid","synthetic");QVERIFY(!id.isEmpty());
            if(offset==2)QVERIFY(store.setDeleted(id,true));
        }
        const auto start=anchor.addDays(-3).startOfDay(zone).toMSecsSinceEpoch(),stop=anchor.addDays(1).startOfDay(zone).toMSecsSinceEpoch();
        QVERIFY(store.ingest(rows,{},start-1,stop,false));
        const auto glucoseBefore=store.query("SELECT * FROM glucose ORDER BY id"),amountsBefore=store.query("SELECT * FROM amounts ORDER BY id");
        Backend backend(dir.path());backend.setWindow(anchor.toString(Qt::ISODate),1);const auto charts=backend.dayComparisonCharts();QCOMPARE(charts.size(),4);
        for(int i=0;i<4;++i){
            const auto day=anchor.addDays(-i);const auto chart=charts[i].toMap();
            QCOMPARE(chart["date"].toString(),day.toString(Qt::ISODate));
            QCOMPARE(qint64(chart["from"].toDouble()),day.startOfDay(zone).toMSecsSinceEpoch());
            QCOMPARE(qint64(chart["to"].toDouble()),day.addDays(1).startOfDay(zone).toMSecsSinceEpoch());
            QVERIFY(!chart["segments"].toList().isEmpty());QCOMPARE(chart["markers"].toList().size(),i==2?1:2);
            for(const auto&entry:chart["markers"].toList()){const auto marker=entry.toMap();QVERIFY(!marker["deleted"].toBool());QVERIFY(marker["timeText"].toString().startsWith(day.toString("dd.MM.yyyy")));}
        }
        const auto dst=charts[1].toMap();QCOMPARE(qint64(dst["to"].toDouble()-dst["from"].toDouble()),qint64(anchor.month()==3?23:25)*3600000);
        auto altered=settings.toVariantMap();altered["maxGapMinutes"]=60;altered["fillHistory"]=true;QVERIFY(backend.saveAnalysisSettings(altered));QCOMPARE(backend.dayComparisonCharts(),charts);
        QCOMPARE(store.query("SELECT * FROM glucose ORDER BY id"),glucoseBefore);QCOMPARE(store.query("SELECT * FROM amounts ORDER BY id"),amountsBefore);
    }
    void dailyAnalysisRecoversAfterMalformedImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const qint64 now = QDateTime::currentSecsSinceEpoch() * 1000;
        const qint64 time = now - 2 * 86400000LL;
        {
            Store setup(dir.path() + "/studio.sqlite3");
            QVERIFY(setup.ingest(parseStream(streamAt(time)).rows, {}, time - 1, now, false));
            QVERIFY(setup.setMeta("connectionConfigured", "1"));
            QVERIFY(setup.setMeta("lastSourceUntil", QString::number(now)));
        }
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        bool broken = true;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n")) return;
                const QUrlQuery query(QUrl(QString::fromUtf8(request->split(' ').value(1))));
                const auto seconds = (time + 60000) / 1000;
                const bool inWindow = seconds >= query.queryItemValue("starttime").toLongLong()
                    && seconds <= query.queryItemValue("endtime").toLongLong();
                const auto body = broken ? QByteArray("broken TSV\n")
                    : request->startsWith("GET /x/amounts")
                        ? QByteArray("Source\tnr\tUnixTime\tValue\tLabel\n")
                        : inWindow ? streamAt(time + 60000, 2)
                            : QByteArray("Sensorid\tnr\tUnixTime\tmmol/L\n");
                socket->write("HTTP/1.0 200 OK\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QFile connection(dir.path() + "/connection.json");
        QVERIFY(connection.open(QIODevice::WriteOnly));
        connection.write(QJsonDocument(QJsonObject{{"mode", "network"}, {"auth", "none"},
            {"address", "http://127.0.0.1:" + QString::number(server.serverPort())}}).toJson());
        connection.close();
        // Inject only the synthetic network transport; production configuration
        // still migrates network -> required WireGuard without a config bypass.
        Transport testTransport;Connection testConnection;testConnection.mode="network";
        testConnection.auth="none";testConnection.address="http://127.0.0.1:"+QString::number(server.serverPort());
        Backend backend(dir.path(),nullptr,[&](const QString &path,auto done){testTransport.get(testConnection,path,std::move(done));});
        backend.refreshDailyAnalysis();
        QTRY_COMPARE_WITH_TIMEOUT(backend.dailyAnalysis()["status"].toString(), QString("ready"), 10000);
        const auto oldRevision = backend.dailyAnalysis()["sourceRevision"].toString();
        QSignalSpy completed(&backend, &Backend::syncFinished);
        backend.sync();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        QVERIFY(!completed.takeFirst()[0].toBool());
        QVERIFY(backend.healthy());
        broken = false;
        backend.sync();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        QVERIFY2(completed.takeFirst()[0].toBool(), qPrintable(backend.status()));
        QVERIFY(backend.healthy());
        QVERIFY(backend.dataRevision() != oldRevision);
        QTRY_COMPARE_WITH_TIMEOUT(backend.dailyAnalysis()["sourceRevision"].toString(), backend.dataRevision(), 10000);
        QCOMPARE(backend.dailyAnalysis()["status"].toString(), QString("ready"));
    }

    void operationErrorsDoNotHideInitializationFailures() {
        QTemporaryDir dir;
        Store missing(dir.path() + "/missing.sqlite3", true);
        QVERIFY(!missing.available());
        QVERIFY(!missing.good());
        Store store(dir.path() + "/studio.sqlite3");
        QVERIFY(store.available());
        QVERIFY(store.good());
        QVERIFY(!store.run("SELECT * FROM nonexistent_synthetic_table"));
        QVERIFY(store.available());
        QVERIFY(!store.good()); // Snapshot chains still detect a SQL error.
        QVERIFY(!store.error().isEmpty());
    }

    void identicalImportsRetainRevisionAndRealChangesInvalidate() {
        QTemporaryDir dir;
        Store store(dir.path() + "/studio.sqlite3");
        const auto first = amount(100000, 1), second = amount(200000, 2), third = amount(300000, 3);
        const QList<Amount> initial{first, second};
        QVERIFY(store.ingest({}, initial, 1, 1000000, true));
        auto revision = store.revision();
        const auto before = store.query("SELECT * FROM amounts ORDER BY id");
        const auto audits = store.query("SELECT * FROM audit WHERE kind='import'").size();
        QVERIFY(store.ingest({}, initial, 1, 1000000, true));
        QCOMPARE(store.revision(), revision);
        QCOMPARE(store.query("SELECT * FROM amounts ORDER BY id"), before);
        QCOMPARE(store.query("SELECT * FROM audit WHERE kind='import'").size(), audits + 1);

        auto reading = sample(500000);
        QVERIFY(store.ingest({reading}, {}, 1, 1000000, false));
        QVERIFY(store.revision() != revision);
        revision = store.revision();
        QVERIFY(store.ingest({reading}, {}, 1, 1000000, false));
        QCOMPARE(store.revision(), revision); // Existing amounts are not new data.
        reading.mmol = 7;
        QVERIFY(store.ingest({reading}, {}, 1, 1000000, false));
        QVERIFY(store.revision() != revision);
        revision = store.revision();

        QVERIFY(store.ingest({}, {first, second, third}, 1, 1000000, true));
        QVERIFY(store.revision() != revision); // Addition.
        revision = store.revision();
        QVERIFY(store.ingest({}, {second, third}, 1, 1000000, true));
        QVERIFY(store.revision() != revision); // Removal.
        revision = store.revision();
        QVERIFY(store.ingest({}, initial, 1, 1000000, true));
        QVERIFY(store.revision() != revision); // Reactivation of the first row.
        revision = store.revision();
        const auto corrected = amount(second.time, 4);
        QVERIFY(store.ingest({}, {first, corrected}, 1, 1000000, true));
        QVERIFY(store.revision() != revision); // Corrected content has a new fingerprint.
        QVERIFY(!store.addLocal(400000, 1, "carbs", "", "synthetic").isEmpty());
        QVERIFY(store.ingest({}, {}, 1, 1000000, true, true));
        const auto remaining = store.amounts(1, 1000000);
        QCOMPARE(remaining.size(), 1);
        QVERIFY(remaining[0].toObject()["local"].toBool());
    }

    void freshnessUsesExactElapsedTime_data() {
        QTest::addColumn<qint64>("age");
        QTest::addColumn<bool>("stale");
        QTest::newRow("fresh") << qint64(0) << false;
        QTest::newRow("exactly-fifteen-minutes") << qint64(900000) << false;
        QTest::newRow("one-millisecond-over") << qint64(900001) << true;
        QTest::newRow("fifteen-minutes-fifty-nine-seconds") << qint64(959000) << true;
        QTest::newRow("sixteen-minutes") << qint64(960000) << true;
    }

    void freshnessUsesExactElapsedTime() {
        QFETCH(qint64, age);
        QFETCH(bool, stale);
        constexpr qint64 now = 2000000;
        const auto result = currentGlucose({sample(now - age)}, {}, now, QTimeZone("UTC"), 4, 10);
        QCOMPARE(result["stale"].toBool(), stale);
        QCOMPARE(result["ageMinutes"].toInt(), int(age / 60000));
        QCOMPARE(result["status"].toString(), stale ? QString("stale") : QString("ok"));
    }

    void chartUsesAnalysisTimezoneAndDst_data() {
        QTest::addColumn<QDate>("day");
        QTest::addColumn<int>("hours");
        QTest::addColumn<int>("ticks");
        QTest::newRow("normal") << QDate(2026, 11, 8) << 24 << 9;
        QTest::newRow("spring") << QDate(2026, 3, 29) << 23 << 8;
        QTest::newRow("autumn") << QDate(2026, 10, 25) << 25 << 10;
    }

    void chartUsesAnalysisTimezoneAndDst() {
        QFETCH(QDate, day);
        QFETCH(int, hours);
        QFETCH(int, ticks);
        const QTimeZone zone("Europe/Helsinki");
        const auto model = currentDayChartData({}, {}, day, zone, 4, 10);
        QCOMPARE(qint64(model["to"].toDouble() - model["from"].toDouble()), hours * 3600000LL);
        const auto labels = model["ticks"].toArray();
        QCOMPARE(labels.size(), ticks);
        QCOMPARE(labels.first().toObject()["label"].toString(), QString("00"));
        QCOMPARE(labels.last().toObject()["label"].toString(), QString("24"));
        QCOMPARE(labels.first().toObject()["time"], model["from"]);
        QCOMPARE(labels.last().toObject()["time"], model["to"]);
        int threes = 0;
        qint64 previous = 0;
        for (const auto &value : labels) {
            const auto tick = value.toObject();
            const auto timestamp = qint64(tick["time"].toDouble());
            QVERIFY(timestamp > previous);
            previous = timestamp;
            if (tick["label"] == "03") ++threes;
            if (tick["label"] != "24")
                QCOMPARE(QDateTime::fromMSecsSinceEpoch(timestamp, zone).toString("HH"), tick["label"].toString());
        }
        QCOMPARE(threes, hours == 23 ? 0 : hours == 25 ? 2 : 1);

        QFile chart(QStringLiteral(STUDIO_SOURCE_DIR) + "/qml/CurrentDayChart.qml");
        QVERIFY(chart.open(QIODevice::ReadOnly));
        const auto source = QString::fromUtf8(chart.readAll());
        const auto body = functionBody(source, "onPaint:");
        QVERIFY(!body.isEmpty());
        // Execute the real paint handler with a recording Canvas in a UTC
        // process. Verify labels and their positions, without a screenshot.
        QJSEngine engine;
        const QString setup = R"JS(
            var labels=[], width=1280, height=260, paintedPoints=0;
            var ctx={reset(){},fillRect(){},strokeRect(){},beginPath(){},moveTo(){},lineTo(){},stroke(){},
                fillText(text,x,y){if(y===height-19)labels.push({label:text,x:x})}};
            function getContext(){return ctx}
            var Theme={};
            var root={leftMargin:48,rightMargin:16,topMargin:18,bottomMargin:42,
                hoveredPoint:null,xFor:function(){return 0},yFor:function(v){return v},targetsSet:function(){return false}};
        )JS";
        auto result = engine.evaluate(setup + "root.chartData="
            + QString::fromUtf8(QJsonDocument(model).toJson(QJsonDocument::Compact))
            + ";(function()" + body + ")();JSON.stringify(labels)");
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        const auto painted = QJsonDocument::fromJson(result.toString().toUtf8()).array();
        QCOMPARE(painted.size(), labels.size());
        for (int i = 0; i < labels.size(); ++i) {
            QCOMPARE(painted[i].toObject()["label"], labels[i].toObject()["label"]);
            const double ratio = (labels[i].toObject()["time"].toDouble() - model["from"].toDouble())
                / (model["to"].toDouble() - model["from"].toDouble());
            QVERIFY(qAbs(painted[i].toObject()["x"].toDouble() - (48 + ratio * (1280 - 48 - 16) - 7)) < 1e-8);
        }
    }

    void chartHoverAndMidnightPrerollUseAnalysisTimezone() {
        const QDate day(2026, 11, 8);
        const QTimeZone zone("Europe/Helsinki");
        const auto from = day.startOfDay(zone).toMSecsSinceEpoch();
        const auto model = currentDayChartData({sample(from - 30000)}, {}, day, zone, 4, 10);
        const auto segments = model["segments"].toArray();
        QCOMPARE(segments.size(), 1);
        const auto point = segments[0].toArray()[0].toObject();
        QCOMPARE(point["x"].toDouble(), double(from));
        QCOMPARE(point["timeText"].toString(), QString("00:00"));
        QVERIFY(model["dateLabel"].toString().contains("8.11.2026"));
        QFile chart(QStringLiteral(STUDIO_SOURCE_DIR) + "/qml/CurrentDayChart.qml");
        QVERIFY(chart.open(QIODevice::ReadOnly));
        const auto body = functionBody(QString::fromUtf8(chart.readAll()), "function pointTimeText(");
        QVERIFY(!body.isEmpty());
        QJSEngine engine;
        auto formatter = engine.evaluate("(function(point)" + body + ")");
        QVERIFY(formatter.isCallable());
        QCOMPARE(formatter.call({engine.toScriptValue(point.toVariantMap())}).toString(), QString("00:00"));
        QCOMPARE(formatter.call({QJSValue(QJSValue::NullValue)}).toString(), QString());
    }

    void standardDayChartIgnoresResearchSettings() {
        QTemporaryDir dir;
        const auto day = QDateTime::currentDateTimeUtc().date();
        const auto start = day.startOfDay(QTimeZone("UTC")).toMSecsSinceEpoch();
        QList<Reading> rows;
        for (int minute : {0, 5, 10}) rows.append(sample(start + minute * 60000, "history"));
        rows.append(sample(start + 360000));
        {
            Store setup(dir.path() + "/studio.sqlite3");
            QVERIFY(setup.ingest(rows, {}, start - 1, start + 86400000, false));
        }
        Backend backend(dir.path());
        QVariantMap settings{{"timezone", "UTC"}, {"low", 4}, {"high", 10},
            {"maxGapMinutes", 1}, {"fillHistory", false}};
        QVERIFY(backend.saveAnalysisSettings(settings));
        const auto chart = backend.currentDayChart();
        int points = 0;
        for (const auto &segment : chart["segments"].toList()) points += segment.toList().size();
        QCOMPARE(points, 5);
        settings["maxGapMinutes"] = 60;
        settings["fillHistory"] = true;
        QVERIFY(backend.saveAnalysisSettings(settings));
        QCOMPARE(backend.currentDayChart(), chart);
    }
};

int main(int argc, char **argv) {
    qputenv("TZ", "UTC");
    tzset();
    QCoreApplication application(argc, argv);
    DebugFixesTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_debug_fixes.moc"
