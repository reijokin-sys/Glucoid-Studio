#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include <QFileDialog>
#include <QApplication>
#include <QTimer>
#include "language.h"
#include "mainfixture.h"
#include "qmltestfixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonDocument>
#include <QDate>
#include <QTimeZone>
#include <QColor>
#include <QLocale>
#include <cmath>
using namespace UiFixture;
class MainUiTest:public QObject {
 Q_OBJECT
 QTemporaryDir languageDir;LanguageManager *language=nullptr;
 struct Fixture {
    QTemporaryDir dir;FixtureStore db{dir.path()+"/studio.sqlite3"};Data data;
    Fixture(){
        QList<Amount> amounts;for(auto v:data.amounts){auto e=v.toObject();Amount a;a.key=e["id"].toString();a.source="synthetic";a.time=qint64(e["time"].toDouble());a.value=e["value"].toDouble();a.label=e["kind"].toString();amounts<<a;}
        if(!db.ingest(data.readings,amounts,start()-8LL*86400000,start()+8LL*86400000,false))qFatal("Synthetic ingest failed");
        db.mapLabel("carbs","carbs","");db.mapLabel("rapid","rapid","Synthetic rapid");db.mapLabel("basal","basal","Synthetic basal");
    }
 };
 static QQuickItem *item(QQuickItem *root,const QString &name){if(root->objectName()==name)return root;for(auto *child:root->childItems())if(auto *found=item(child,name))return found;return nullptr;}
 static void load(QQmlApplicationEngine&engine,Backend&b,int width=1280,bool demoMode=true){
    b.setAnalysisWindow(7,"2026-10-06");registerStudioQmlTypes();
    setStudioQmlProperties(engine,b,QStringLiteral(STUDIO_APP_VERSION),demoMode,true,width);
    engine.load(QUrl::fromLocalFile(QStringLiteral(STUDIO_SOURCE_DIR)+"/qml/Main.qml"));
 }
private slots:
 void initTestCase(){
    language=new LanguageManager(languageDir.path());
    QVERIFY(language->ready());
    QCOMPARE(language->language(),QString("en"));
    QCoreApplication::instance()->setProperty("studioLanguageController",QVariant::fromValue<QObject*>(language));
    QPalette pal=QGuiApplication::palette();
    pal.setColor(QPalette::Window,QColor("#12161a"));pal.setColor(QPalette::Base,QColor("#12161a"));
    pal.setColor(QPalette::WindowText,QColor("#eef2f5"));pal.setColor(QPalette::Text,QColor("#eef2f5"));
    QGuiApplication::setPalette(pal);
 }
 void cleanupTestCase(){
    QCoreApplication::instance()->setProperty("studioLanguageController",QVariant());
    delete language;language=nullptr;
 }

 void dailyAnalysisRendersSevenPeriodsWithoutMealModels_data(){
    QTest::addColumn<int>("width");QTest::newRow("wide")<<1280;QTest::newRow("medium")<<900;QTest::newRow("narrow")<<640;
 }
 void dailyAnalysisRendersSevenPeriodsWithoutMealModels(){
    QFETCH(int,width);
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b,width);
    QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QTRY_COMPARE_WITH_TIMEOUT(b.dailyAnalysis().value("status").toString(),QString("ready"),15000);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,3));
    auto *view=item(window->contentItem(),"dailyAnalysisView");QVERIFY(view&&view->isVisible());
    QCOMPARE(b.dailyAnalysis().value("groups").toList().size(),7);
    auto *grid=item(window->contentItem(),"dailyPeriodGrid");QVERIFY(grid);
    const int expectedColumns=width>=1150?3:width>=700?2:1;
    QCOMPARE(grid->property("columns").toInt(),expectedColumns);
    auto *first=item(window->contentItem(),"dailyPeriodCard0"),*second=item(window->contentItem(),"dailyPeriodCard1");
    QVERIFY(first&&second);QTRY_VERIFY(first->width()>0&&second->width()>0);
    if(expectedColumns>1){
        QTRY_VERIFY(qAbs(first->mapToScene(QPointF()).y()-second->mapToScene(QPointF()).y())<1);
        QVERIFY(second->mapToScene(QPointF()).x()>first->mapToScene(QPointF()).x());
    } else {
        QTRY_VERIFY(second->mapToScene(QPointF()).y()>first->mapToScene(QPointF()).y());
    }
    for(int i=0;i<7;++i){
        auto *card=item(window->contentItem(),"dailyPeriodCard"+QString::number(i));QVERIFY(card);
        QTRY_VERIFY(card->width()>0);const auto rect=card->mapRectToScene(card->boundingRect());
        QVERIFY(rect.left()>=-1&&rect.right()<=window->width()+1);
        QVERIFY(item(window->contentItem(),"dailyGlucose"+QString::number(i)));
        QVERIFY(item(window->contentItem(),"dailyRatio"+QString::number(i)));
    }
    auto *totals=item(window->contentItem(),"dailyAmountTotals0"),*details=item(window->contentItem(),"dailyCalculationDetails0");
    QVERIFY(totals&&details);QVERIFY(!totals->isVisible());
    QVERIFY(QMetaObject::invokeMethod(details,"toggle"));QTRY_VERIFY(totals->isVisible());
    QVERIFY(QMetaObject::invokeMethod(details,"toggle"));QTRY_VERIFY(!totals->isVisible());
    QVERIFY(!item(window->contentItem(),"theoreticalDose0"));
    QVERIFY(!item(window->contentItem(),"dayComparisonView"));
    const auto key=b.dailyAnalysis().value("requestKey");
    QVERIFY(f.db.setDeleted("u0-0",true));b.refreshDailyAnalysis();
    QTRY_VERIFY_WITH_TIMEOUT(b.dailyAnalysis().value("status")=="ready"&&b.dailyAnalysis().value("requestKey")!=key,15000);
 }
 void mainViewsWithoutNutrition(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b,900);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    const auto expectedTitle=QString("Glucoid Studio %1").arg(QStringLiteral(STUDIO_APP_VERSION));
    QCOMPARE(window->property("title").toString(),expectedTitle);
    auto *page=item(window->contentItem(),"mainApplicationPage");QVERIFY(page);QCOMPARE(page->property("title").toString(),expectedTitle);
    auto *tabs=item(window->contentItem(),"mainTabs");QVERIFY(tabs);QCOMPARE(tabs->property("count").toInt(),3);
    QVERIFY(!item(window->contentItem(),"nutritionView"));
    QVERIFY(!item(window->contentItem(),"nutritionProtein"));
    QVERIFY(!item(window->contentItem(),"nutritionProteinEquivalent"));
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,3));QTest::qWait(100);
    QCOMPARE(tabs->property("currentIndex").toInt(),1);
    QVERIFY(item(window->contentItem(),"dailyAnalysisView"));
    QVERIFY(!item(window->contentItem(),"dayComparisonView"));
 }

 void mainSettingsOnlyShowCurrentFunctions(){
    Fixture f;QVERIFY(f.db.run("UPDATE amounts SET source='juggluco-v3',kind=label"));Backend b(f.dir.path());const auto profiles=f.db.query("SELECT * FROM profiles");
    QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,4));
    auto *connection=item(window->contentItem(),"connectionDetails");
    auto *mapping=item(window->contentItem(),"labelMappingSettings");
    QVERIFY(connection&&mapping);QTRY_VERIFY(mapping->isVisible());
    mapping->setProperty("expanded",true);
    auto *sourceClass=item(window->contentItem(),"jugglucoSourceClass");QVERIFY(sourceClass);
    QVERIFY(!sourceClass->property("currentIndex").isValid()); // class is read-only, not a second selector.
    QVERIFY(!sourceClass->property("text").toString().isEmpty());
    QVERIFY(!sourceClass->property("text").toString().contains("Update the category"));
    auto *classNote=item(window->contentItem(),"jugglucoClassSourceNote");QVERIFY(classNote);
    const QString classText=classNote->property("text").toString();
    QVERIFY(classText.contains("Juggluco"));QVERIFY(!classText.contains("Update the category"));
    auto *renameNote=item(window->contentItem(),"labelNameFrozenNote");QVERIFY(renameNote);
    QVERIFY(renameNote->property("text").toString().contains("label"));
    QVERIFY(!connection->property("expanded").toBool());
    for(auto name:{"nightTargetSettings","legacyTargetSettings","automaticTargetLow","automaticHypo","saveAutomaticTargets","overnightSummary","observationSummary"})
        QVERIFY2(!item(window->contentItem(),name),name);

    QCOMPARE(f.db.query("SELECT * FROM profiles"),profiles);
    QTRY_COMPARE_WITH_TIMEOUT(b.dailyAnalysis().value("status").toString(),QString("ready"),15000);
    QVERIFY(b.saveSourceConnection("nightscout","network","https://nightscout.example","token","",""));
    QTRY_VERIFY(!mapping->isVisible());
 }

 void chartViewsExplainThemselves(){Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,0));QTest::qWait(100);auto *summaryLegend=item(window->contentItem(),"profileLegendSummary");QVERIFY(summaryLegend&&summaryLegend->isVisible());
    auto *nightMetric=item(window->contentItem(),"nightGlucoseMetric");QVERIFY(nightMetric&&nightMetric->isVisible());
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,1));QTest::qWait(100);auto *profileLegend=item(window->contentItem(),"profileLegend");QVERIFY(profileLegend&&profileLegend->isVisible());
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));QTest::qWait(100);auto *dayLegend=item(window->contentItem(),"dayLegend");QVERIFY(dayLegend&&dayLegend->isVisible());
    auto *title=item(window->contentItem(),"selectedDayTitle"),*previousTitle=item(window->contentItem(),"previousDayTitle");QVERIFY(title&&title->property("text").toString().startsWith("Selected day · "));QVERIFY(previousTitle&&previousTitle->property("text").toString().startsWith("Previous day · "));
    auto *field=item(window->contentItem(),"dayDate");QVERIFY(field);const QString shown=field->property("text").toString();QVERIFY(!shown.isEmpty());const qint64 before=qint64(b.dayComparisonCharts().first().toMap().value("from").toDouble());
    auto *next=item(window->contentItem(),"nextDay");QVERIFY(next&&next->property("enabled").toBool()&&QMetaObject::invokeMethod(next,"clicked"));QTRY_COMPARE(qint64(b.dayComparisonCharts().first().toMap().value("from").toDouble()),before+86400000LL);QTRY_COMPARE(field->property("text").toString(),QDate::fromString(shown,Qt::ISODate).addDays(1).toString(Qt::ISODate));
    auto *previousButton=item(window->contentItem(),"previousDay");QVERIFY(previousButton&&QMetaObject::invokeMethod(previousButton,"clicked"));QTRY_COMPARE(qint64(b.dayComparisonCharts().first().toMap().value("from").toDouble()),before);
 }
 void dayNavigationIgnoresUnsubmittedDateText(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    b.setWindow("2026-10-06",1);QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));
    auto *field=item(window->contentItem(),"dayDate");QVERIFY(field);field->setProperty("text","kesken");
    auto *next=item(window->contentItem(),"nextDay");QVERIFY(next);QVERIFY(QMetaObject::invokeMethod(next,"clicked"));
    QTRY_COMPARE(b.dayComparisonCharts().first().toMap()["date"].toString(),QString("2026-10-07"));
 }
 void dayComparisonShowsFourScrollableColoredCharts_data(){
    QTest::addColumn<int>("width");QTest::newRow("wide")<<1280;QTest::newRow("narrow")<<900;
 }
 void dayComparisonShowsFourScrollableColoredCharts(){
    QFETCH(int,width);QTemporaryDir dir;FixtureStore store(dir.path()+"/studio.sqlite3");const QDate anchor(2026,11,8);const QTimeZone zone("Europe/Helsinki");
    QVERIFY(store.setMeta("analysisSettings",QString::fromUtf8(QJsonDocument(QJsonObject{{"timezone","Europe/Helsinki"},{"low",4},{"high",10}}).toJson())));
    QList<Reading> rows;
    for(int offset=0;offset<4;++offset)for(int i=0;i<4;++i){Reading r;r.key=QString("chart-%1-%2").arg(offset).arg(i);r.sensor="synthetic";r.seriesKind="history";r.time=anchor.addDays(-offset).startOfDay(zone).toMSecsSinceEpoch()+i*300000;r.mmol=i==0?2:i==2?12:6;rows<<r;}
    QVERIFY(store.ingest(rows,{},rows.last().time-86400000,rows.first().time+86400000,false));
    Backend b(dir.path());QQmlApplicationEngine engine;load(engine,b,width);QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    b.setWindow(anchor.toString(Qt::ISODate),1);QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));
    auto *profile=item(window->contentItem(),"profileTab");QVERIFY(profile);QCOMPARE(profile->property("text").toString(),QString("Daily profile"));
    for(int i=0;i<4;++i){
        auto *chart=item(window->contentItem(),i==0?"glucoseChart":"comparisonChart"+QString::number(i));QVERIFY(chart);
        QVERIFY(chart->property("highlightOutsideTarget").toBool());QTRY_VERIFY(chart->height()>=320);
        const auto rect=chart->mapRectToScene(chart->boundingRect());QVERIFY(rect.left()>=-1&&rect.right()<=window->width()+1);
        const auto model=chart->property("chartData").toMap();QCOMPARE(model["date"].toString(),anchor.addDays(-i).toString(Qt::ISODate));
    }
    auto *first=item(window->contentItem(),"glucoseChart");
    QQmlExpression colored(QQmlEngine::contextForObject(first),first,"created.some(s => s.visible && s.color.toString() === '#ff8b7a')");
    QTRY_VERIFY(colored.evaluate().toBool());QVERIFY(!colored.hasError());
    auto *scroll=item(window->contentItem(),"dayComparisonScroll");QVERIFY(scroll);
    auto *flick=scroll->property("contentItem").value<QObject*>();QVERIFY(flick);
    QTRY_VERIFY(flick->property("contentHeight").toDouble()>scroll->height());
    flick->setProperty("contentY",flick->property("contentHeight").toDouble()-scroll->height());QVERIFY(flick->property("contentY").toDouble()>0);
    auto *last=item(window->contentItem(),"comparisonChart3");QVERIFY(last);
    QTRY_VERIFY(last->mapRectToScene(last->boundingRect()).bottom()<=scroll->mapRectToScene(scroll->boundingRect()).bottom()+1);
    auto *next=item(window->contentItem(),"nextDay");QVERIFY(QMetaObject::invokeMethod(next,"clicked"));
    QTRY_COMPARE(b.dayComparisonCharts().first().toMap()["date"].toString(),anchor.addDays(1).toString(Qt::ISODate));QTRY_COMPARE(flick->property("contentY").toDouble(),0.0);
    auto *today=item(window->contentItem(),"comparisonToday");QVERIFY(today);QVERIFY(QMetaObject::invokeMethod(today,"clicked"));
    QTRY_COMPARE(b.dayComparisonCharts().first().toMap()["date"],b.currentDayChart()["date"]);
 }
 void forcedDarkThemeUsesDarkSurfaces(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QCOMPARE(window->property("color").value<QColor>(),QColor("#12161a"));
    QTest::qWait(400);
    const QImage shot=window->grabWindow();
    QVERIFY2(!shot.isNull(),"window grab failed");
    const QColor corner=shot.pixelColor(3,3);
    QVERIFY2(corner==QColor("#12161a"),qPrintable(corner.name()));
 }
 void summaryPeriodButtonsAndCurrentDayChart(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,0));QTest::qWait(100);
    auto *week1=item(window->contentItem(),"analysisPeriod7"),*week4=item(window->contentItem(),"analysisPeriod28"),*months3=item(window->contentItem(),"analysisPeriod90");
    QVERIFY(week1&&week4&&months3);
    QCOMPARE(week1->property("text").toString(),QString("1 wk"));
    QCOMPARE(week4->property("text").toString(),QString("4 wk"));
    QCOMPARE(months3->property("text").toString(),QString("3 mo"));
    QCOMPARE(week1->property("active").toBool(),true);QCOMPARE(week4->property("active").toBool(),false);
    QVERIFY(QMetaObject::invokeMethod(week4,"clicked"));QTRY_COMPARE(b.analysis().value("days").toInt(),28);
    QCOMPARE(week4->property("active").toBool(),true);QCOMPARE(week1->property("active").toBool(),false);
    QVERIFY(QMetaObject::invokeMethod(months3,"clicked"));QTRY_COMPARE(b.analysis().value("days").toInt(),90);
    QCOMPARE(months3->property("active").toBool(),true);QCOMPARE(week4->property("active").toBool(),false);
    auto *chart=item(window->contentItem(),"summaryCurrentDayChart");QVERIFY(chart&&chart->isVisible());
    auto *profileLegend=item(window->contentItem(),"profileLegendSummary");QVERIFY(profileLegend);
    QVERIFY(chart->mapToScene(QPointF(0,chart->height())).y()<=profileLegend->mapToScene(QPointF()).y());
    const QTimeZone zone("Europe/Helsinki");const QDate today=QDate::currentDate();
    QCOMPARE(qint64(b.currentDayChart().value("from").toDouble()),QDateTime(today,QTime(0,0),zone).toMSecsSinceEpoch());
    QCOMPARE(qint64(b.currentDayChart().value("to").toDouble()),QDateTime(today.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch());
 }
 void currentDayChartPaintsTodaysReadings(){
    QTemporaryDir dir;FixtureStore db(dir.path()+"/studio.sqlite3");
    const QTimeZone zone("Europe/Helsinki");const QDate today=QDateTime::currentDateTime(zone).date();
    const qint64 dayStart=QDateTime(today,QTime(0,0),zone).toMSecsSinceEpoch(),dayEnd=QDateTime(today.addDays(1),QTime(0,0),zone).toMSecsSinceEpoch();
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    QList<Reading> readings;for(qint64 t=dayStart;t<dayEnd&&t<=now;t+=300000){Reading r;r.time=t;r.key=QString::number(t);r.mmol=6.5+double((t-dayStart)/3600000)/10.0;r.original=r.mmol;r.originalUnit="mmol/L";r.sensor="synthetic";readings<<r;}
    QVERIFY(!readings.isEmpty());QVERIFY(db.ingest(readings,{},dayStart-1,dayEnd+1,true));
    Backend b(dir.path());
    QVERIFY(b.saveAnalysisSettings(QVariantMap{{"timezone","Europe/Helsinki"},{"low",6.0},{"high",8.0},{"maxGapMinutes",20},{"fillHistory",false}}));
    QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,0));QTest::qWait(100);
    auto *chart=item(window->contentItem(),"summaryCurrentDayChart");QVERIFY(chart);
    QCOMPARE(qint64(b.currentDayChart().value("from").toDouble()),dayStart);
    QCOMPARE(qint64(b.currentDayChart().value("to").toDouble()),dayEnd);
    QCOMPARE(b.currentDayChart().value("low").toDouble(),6.0);
    QCOMPARE(b.currentDayChart().value("high").toDouble(),8.0);
    auto colorOf=[&](const QString &expression){
        QQmlExpression query(qmlContext(chart),chart,expression);
        return query.evaluate().value<QColor>();
    };
    QCOMPARE(colorOf("colorForValue(7)"),QColor("#5fd0b0"));
    QCOMPARE(colorOf("colorForValue(9)"),QColor("#ff8b7a"));
    QCOMPARE(colorOf("colorForValue(5)"),QColor("#ff8b7a"));
    QTRY_VERIFY_WITH_TIMEOUT(chart->property("paintedPoints").toInt()>0,5000);
 }
 void gmiCardShowsEstimateAndShortPeriodCaveat_data(){
    QTest::addColumn<double>("mean");QTest::addColumn<double>("gmi");QTest::addColumn<QString>("color");
    QTest::newRow("week-below-target")<<7.0<<6.327<<QString("#5fd0b0");
    QTest::newRow("week-above-target")<<10.0<<(3.31+0.02392*180.182)<<QString("#f2c66d");
 }
 void gmiCardShowsEstimateAndShortPeriodCaveat(){
    QFETCH(double,mean);QFETCH(double,gmi);QFETCH(QString,color);
    Fixture f;
    auto readings=f.data.readings;for(auto &r:readings){r.mmol=mean;r.original=mean;}
    QVERIFY(f.db.ingest(readings,{},start()-8LL*86400000,start()+8LL*86400000,false));
    Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,0));QTest::qWait(100);
    auto *card=item(window->contentItem(),"gmiMetric");QVERIFY(card);
    const auto analysis=b.analysis();
    QVERIFY(analysis.value("gmiAvailable").toBool());
    QVERIFY(qAbs(analysis.value("gmi").toDouble()-gmi)<0.02);
    QCOMPARE(analysis.value("gmiAdequate").toBool(),false);
    QCOMPARE(card->property("value").toString(),QLocale("en_GB").toString(gmi,'f',1));
    QCOMPARE(card->property("unit").toString(),QString("%"));
    QCOMPARE(card->property("valueColor").value<QColor>(),QColor(color));
    QVERIFY(card->property("note").toString().contains(QString::fromUtf8("indicative")));
 }
 void summaryReferenceFlagsAndColors(){
    Fixture f;Backend b(f.dir.path());b.setAnalysisWindow(7,"2026-10-06");
    const auto ref=b.analysis().value("reference").toMap();
    QCOMPARE(ref.value("meanInRange").toBool(),true);
    QCOMPARE(ref.value("cvInRange").toBool(),true);
    QCOMPARE(ref.value("coverageInRange").toBool(),true);
    QCOMPARE(ref.value("gmiInRange").toBool(),true);
    QCOMPARE(ref.value("tirInRange").toBool(),false);
    QCOMPARE(ref.value("nightInRange").toBool(),true);
    QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,0));QTest::qWait(100);
    auto color=[&](const QString &name){auto *card=item(window->contentItem(),name);return card?card->property("valueColor").value<QColor>():QColor();};
    QCOMPARE(color("meanMetric"),QColor("#5fd0b0"));
    QCOMPARE(color("cvMetric"),QColor("#5fd0b0"));
    QCOMPARE(color("coverageMetric"),QColor("#5fd0b0"));
    QCOMPARE(color("nightGlucoseMetric"),QColor("#5fd0b0"));
    QCOMPARE(color("gmiMetric"),QColor("#5fd0b0"));
    QCOMPARE(color("targetMetric"),QColor("#eef2f5"));
    QQmlExpression amber(qmlContext(window),window,"referenceColor(true,false)");
    QCOMPARE(amber.evaluate().value<QColor>(),QColor("#f2c66d"));
    QQmlExpression neutral(qmlContext(window),window,"referenceColor(false,true)");
    QCOMPARE(neutral.evaluate().value<QColor>(),QColor("#eef2f5"));
 }
 void timestampsRenderInAnalysisTimezone(){
    Fixture f;Backend b(f.dir.path());
    QVariantMap settings{{"timezone","UTC"},{"low",4},{"high",10},{"maxGapMinutes",20},{"fillHistory",false}};
    QVERIFY(b.saveAnalysisSettings(settings));
    QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    const auto at=QDateTime(QDate(2026,11,1),QTime(0,30),QTimeZone("Europe/Helsinki")).toMSecsSinceEpoch();
    auto evaluate=[&](const QString &expression){
        QQmlExpression e(qmlContext(window),window,expression);
        return e.evaluate().toString();
    };
    // Helsinki 1.11.2026 00:30 is 31.10.2026 22:30 in the configured UTC zone.
    QCOMPARE(evaluate(QString("stamp(%1)").arg(at)),QString("31.10.2026 22:30"));
    QCOMPARE(evaluate(QString("isoDate(%1)").arg(at)),QString("2026-10-31"));
    QCOMPARE(evaluate(QString("longDate(%1)").arg(at)),QString("Sat 31.10.2026"));
    QCOMPARE(evaluate(QString("shiftDay(%1,1)").arg(at)),QString("2026-11-01"));
 }
 void chartsDefaultToCurrentDay(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));QTest::qWait(100);
    auto *title=item(window->contentItem(),"selectedDayTitle");QVERIFY(title);
    const auto expected=QLocale("en_GB").toString(QDate::currentDate(),QStringLiteral("ddd d.M.yyyy"));
    QCOMPARE(title->property("text").toString(),QStringLiteral("Selected day · ")+expected);
 }
 void settingsReloadSavedConnectionOnEveryOpen(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);
    QVERIFY(!engine.rootObjects().isEmpty());auto *window=engine.rootObjects().first();
    auto *settings=window->findChild<QObject*>("simpleSettings");QVERIFY(settings);
    QVERIFY(QMetaObject::invokeMethod(settings,"open"));
    QTRY_VERIFY(settings->property("opened").toBool());
    auto *serial=window->findChild<QObject*>("sourceSerial");QVERIFY(serial);
    serial->setProperty("text","unsaved synthetic edit");
    QVERIFY(QMetaObject::invokeMethod(settings,"close"));QTRY_VERIFY(!settings->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(settings,"open"));QTRY_VERIFY(settings->property("opened").toBool());
    QTRY_COMPARE(serial->property("text").toString(),b.savedConnection("juggluco")["serial"].toString());
 }
 void singleGlucosePointHasVisibleMarker(){
    QTemporaryDir dir;FixtureStore store(dir.path()+"/studio.sqlite3");
    const QDate day(2026,11,8);const QTimeZone zone("UTC");
    const auto from=day.startOfDay(zone).toMSecsSinceEpoch();
    Reading point;point.key="synthetic-single";point.sensor="synthetic";point.seriesKind="history";point.time=from+3600000;point.mmol=12;point.original=12;point.originalUnit="mmol/L";
    QVERIFY(store.ingest({point},{},from,from+86400000,false));
    QVERIFY(store.setMeta("analysisSettings",QString::fromUtf8(QJsonDocument(QJsonObject{{"timezone","UTC"},{"low",4},{"high",10}}).toJson())));
    Backend b(dir.path());QQmlApplicationEngine engine;load(engine,b);
    QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    b.setWindow(day.toString(Qt::ISODate),1);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));
    auto *chart=item(window->contentItem(),"glucoseChart");QVERIFY(chart);
    const auto model=chart->property("chartData").toMap();
    QCOMPARE(model["segments"].toList().size(),1);
    auto marker=[&]{QQmlExpression query(qmlContext(chart),chart,"created.some(s => s.visible && s.count === 1 && s.pointDelegate !== null)");return query.evaluate().toBool();};
    QTRY_VERIFY_WITH_TIMEOUT(marker(),5000);
 }
 void settingsExposeManualBackupAndFileRestore(){
    Fixture f;Backend b(f.dir.path());
    QQmlApplicationEngine engine;load(engine,b,1280,false);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    auto *settings=window->findChild<QObject*>("simpleSettings");QVERIFY(settings);
    QVERIFY(QMetaObject::invokeMethod(settings,"open"));
    QTRY_VERIFY_WITH_TIMEOUT(settings->property("opened").toBool(),5000);
    QVERIFY(!window->findChild<QObject*>("backupEnabledSwitch"));
    QVERIFY(!window->findChild<QObject*>("backupChoice"));
    QVERIFY(window->findChild<QObject*>("backupNowButton"));
    auto *restore=window->findChild<QObject*>("restoreBackupButton");QVERIFY(restore);
    QCOMPARE(restore->property("enabled").toBool(),true);
    QVERIFY(QMetaObject::invokeMethod(settings,"close"));
 }
 void systemSaveDialogComesBeforeCopyAndCancellationCreatesNothing(){
    Fixture f;Backend b(f.dir.path());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true); // offscreen automation only
    const auto before=QDir(f.dir.path()).entryList(QDir::Files|QDir::Hidden);
    bool sawDialog=false,untouched=false;
    QTimer::singleShot(0,[&]{
        for(auto *widget:QApplication::topLevelWidgets()){
            if(auto *dialog=qobject_cast<QFileDialog*>(widget)){
                sawDialog=true;
                QCOMPARE(dialog->acceptMode(),QFileDialog::AcceptSave);
                QVERIFY(!dialog->testOption(QFileDialog::DontConfirmOverwrite));
                untouched=QDir(f.dir.path()).entryList(QDir::Files|QDir::Hidden)==before;
                dialog->reject();
            }
        }
    });
    QVERIFY(b.backup().isEmpty());QVERIFY(sawDialog);QVERIFY(untouched);
    QCOMPARE(QDir(f.dir.path()).entryList(QDir::Files|QDir::Hidden),before);
    QVERIFY(b.backupStatus().isEmpty());
 }
 void systemSaveDialogAcceptedPathIsUsed(){
    Fixture f;Backend b(f.dir.path());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
    const auto selected=f.dir.path()+"/chosen-by-user.sqlite3";
    bool sawDialog=false;
    QTimer::singleShot(0,[&]{
        for(auto *widget:QApplication::topLevelWidgets()){
            if(auto *dialog=qobject_cast<QFileDialog*>(widget)){
                sawDialog=true;
                QVERIFY(!QFile::exists(selected));
                dialog->selectFile(selected);
                QMetaObject::invokeMethod(dialog,"accept");
            }
        }
    });
    QCOMPARE(b.backup(),selected);QVERIFY(sawDialog);QVERIFY(QFile::exists(selected));
    QVERIFY(!QFile::exists(selected+".manifest.json"));
 }

};
QTEST_MAIN(MainUiTest)
#include "test_main_ui.moc"
