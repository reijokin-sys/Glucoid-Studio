// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "automatic_fixture.h"
#include "target_fixture.h"
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
using namespace AutomaticFixture;
class AutomaticUiTest:public QObject {
 Q_OBJECT
 struct Fixture {
    QTemporaryDir dir;Store db{dir.path()+"/studio.sqlite3"};Data data;
    Fixture(){
        QList<Amount> amounts;for(auto v:data.amounts){auto e=v.toObject();Amount a;a.key=e["id"].toString();a.source="synthetic";a.time=qint64(e["time"].toDouble());a.value=e["value"].toDouble();a.label=e["kind"].toString();amounts<<a;}
        if(!db.ingest(data.readings,amounts,start()-8LL*86400000,start()+8LL*86400000,false))qFatal("Synthetic ingest failed");
        db.mapLabel("carbs","carbs","");db.mapLabel("rapid","rapid","Synthetic rapid");db.mapLabel("basal","basal","Synthetic basal");
        db.setMeta("automaticMealSettings",QString::fromUtf8(QJsonDocument(settings()).toJson()));
    }
 };
 static QQuickItem *item(QQuickItem *root,const QString &name){if(root->objectName()==name)return root;for(auto *child:root->childItems())if(auto *found=item(child,name))return found;return nullptr;}
 static void load(QQmlApplicationEngine&engine,Backend&b,int width=1280,bool research=false){
    b.setAnalysisWindow(7,"2026-10-06");engine.rootContext()->setContextProperty("studio",&b);engine.rootContext()->setContextProperty("appVersion",QStringLiteral(STUDIO_APP_VERSION));engine.rootContext()->setContextProperty("demoMode",true);engine.rootContext()->setContextProperty("uiTestMode",true);engine.rootContext()->setContextProperty("uiTestWidth",width);
    engine.load(QUrl::fromLocalFile(QStringLiteral(STUDIO_SOURCE_DIR)+(research?"/qml/ResearchMain.qml":"/qml/Main.qml")));
 }
private slots:
 void initTestCase(){
    QPalette pal=QGuiApplication::palette();
    pal.setColor(QPalette::Window,QColor("#12161a"));pal.setColor(QPalette::Base,QColor("#12161a"));
    pal.setColor(QPalette::WindowText,QColor("#eef2f5"));pal.setColor(QPalette::Text,QColor("#eef2f5"));
    QGuiApplication::setPalette(pal);
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


 void newerRequestsAndSourceCorrectionsReplaceOlderResults(){
    Fixture f;Backend b(f.dir.path());b.setAnalysisWindow(7,"2026-10-06");b.startAutomaticAnalysis();b.setAutomaticPeriod(28);b.setAutomaticPeriod(84);
    QTRY_COMPARE_WITH_TIMEOUT(b.automaticMeals().value("status").toString(),QString("ready"),15000);QCOMPARE(b.automaticMeals().value("settings").toMap().value("periodDays").toInt(),84);
    const auto key=b.automaticMeals().value("requestKey");QVERIFY(f.db.setDeleted("u0-0",true));b.refresh();QTRY_VERIFY_WITH_TIMEOUT(b.automaticMeals().value("status")=="ready"&&b.automaticMeals().value("requestKey")!=key,15000);
    QCOMPARE(b.automaticMeals().value("sourceRevision").toString(),f.db.revision());QCOMPARE(b.automaticMeals().value("groups").toList()[0].toMap().value("pairedMeals").toInt(),5);QVERIFY(f.db.episodes().isEmpty());
 }
 void targetEditsDoNotApproveOrChangeTreatment(){
    Fixture f;auto p=profiles()[0].toObject();QVERIFY(f.db.saveProfile(p)>0);const auto before=f.db.profiles();Backend b(f.dir.path());b.setAnalysisWindow(7,"2026-10-06");b.startAutomaticAnalysis();
    QVERIFY(!b.saveAutomaticTargets(9,8,4));QCOMPARE(b.automaticSettings().value("targetLow").toDouble(),6.0);
    QVERIFY(b.saveAutomaticTargets(6,9,4));QTRY_COMPARE_WITH_TIMEOUT(b.automaticMeals().value("targetHigh").toDouble(),9.0,15000);QCOMPARE(f.db.profiles(),before);QVERIFY(f.db.episodes().isEmpty());
    Backend reopened(f.dir.path());QCOMPARE(reopened.automaticSettings().value("targetHigh").toDouble(),9.0);
 }
 void mainSettingsOnlyShowCurrentFunctions(){
    Fixture f;Backend b(f.dir.path());const auto settings=b.automaticSettings();const auto profiles=f.db.profiles();
    QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,4));
    auto *connection=item(window->contentItem(),"connectionDetails");
    auto *mapping=item(window->contentItem(),"labelMappingSettings");
    QVERIFY(connection&&mapping);QTRY_VERIFY(mapping->isVisible());
    QVERIFY(!connection->property("expanded").toBool());
    for(auto name:{"nightTargetSettings","legacyTargetSettings","automaticTargetLow","automaticHypo","saveAutomaticTargets","overnightSummary","observationSummary"})
        QVERIFY2(!item(window->contentItem(),name),name);
    QVERIFY(!b.automaticBusy());QVERIFY(b.automaticMeals().isEmpty());
    QCOMPARE(b.automaticSettings(),settings);QCOMPARE(f.db.profiles(),profiles);
    QTRY_COMPARE_WITH_TIMEOUT(b.dailyAnalysis().value("status").toString(),QString("ready"),15000);
    QVERIFY(b.saveSourceConnection("nightscout","network","https://nightscout.example","token","",""));
    QTRY_VERIFY(!mapping->isVisible());
 }
 void settingsButtonSavesThroughRealQml(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b,1280,true);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,4));
    auto *targets=item(window->contentItem(),"researchTargetSettings");QVERIFY(targets);
    QVERIFY(!targets->property("expanded").toBool());QVERIFY(QMetaObject::invokeMethod(targets,"toggle"));
    item(window->contentItem(),"automaticTargetLow")->setProperty("text","6,2");
    item(window->contentItem(),"automaticTargetHigh")->setProperty("text","8,2");
    item(window->contentItem(),"automaticHypo")->setProperty("text","4");
    auto *button=item(window->contentItem(),"saveAutomaticTargets");QVERIFY(button);QTRY_VERIFY(button->isVisible());
    QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
    QTRY_COMPARE(b.automaticSettings().value("targetLow").toDouble(),6.2);
    QTRY_COMPARE(b.automaticMeals().value("targetHigh").toDouble(),8.2);
    auto *card=item(window->contentItem(),"overnightSummary");QVERIFY(card);
    Backend reopened(f.dir.path());QCOMPARE(reopened.automaticSettings().value("targetHigh").toDouble(),8.2);
 }
 void descriptiveNightSummaryPreservesReportedBasalContext_data(){
    QTest::addColumn<int>("width");QTest::newRow("wide")<<1280;QTest::newRow("narrow")<<900;
 }
 void descriptiveNightSummaryPreservesReportedBasalContext(){
    QFETCH(int,width);
    Fixture f;f.db.setMeta("reportedBasalHistory",QString::fromUtf8(QJsonDocument(reportedSettings()["reportedBasalHistory"].toArray()).toJson()));
    const auto before=f.db.query("SELECT * FROM amounts");const auto profiles=f.db.profiles();Backend b(f.dir.path());
    const auto settings=b.automaticSettings();QQmlApplicationEngine engine;load(engine,b,width);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QVERIFY(!item(window->contentItem(),"overnightSummary"));QVERIFY(!item(window->contentItem(),"observationSummary"));
    QVERIFY(!b.automaticBusy());QVERIFY(b.automaticMeals().isEmpty());
    auto *nightMetric=item(window->contentItem(),"nightGlucoseMetric");QVERIFY(nightMetric&&nightMetric->isVisible());
    QCOMPARE(nightMetric->property("value").toString(),QLocale("fi_FI").toString(b.analysis().value("nightMean").toDouble(),'f',1));
    QVERIFY(nightMetric->property("note").toString().contains("Aikapainotettu"));
    QVERIFY(nightMetric->property("note").toString().contains("kattavuus"));
    QTRY_VERIFY(nightMetric->width()>150);
    const auto metricRect=nightMetric->mapRectToScene(nightMetric->boundingRect());
    QVERIFY(metricRect.left()>=0&&metricRect.right()<=window->width());
    auto *metrics=item(window->contentItem(),"summaryMetrics");QVERIFY(metrics);
    QTRY_VERIFY(metricRect.top()>=metrics->mapToScene(QPointF(0,metrics->height())).y());
    QVERIFY(qAbs(nightMetric->width()-metrics->width())<1);
    QTRY_COMPARE_WITH_TIMEOUT(b.dailyAnalysis().value("status").toString(),QString("ready"),15000);
    const auto groups=b.dailyAnalysis().value("groups").toList();QCOMPARE(groups.size(),7);
    QVERIFY(groups[6].toMap().value("label").toString().contains("00:00"));
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,3));
    QTRY_VERIFY(item(window->contentItem(),"dailyGlucose6")->isVisible());
    QCOMPARE(b.automaticSettings(),settings);QCOMPARE(f.db.query("SELECT * FROM amounts"),before);QCOMPARE(f.db.profiles(),profiles);
 }
 void closedDuringCalculationPreservesData(){Fixture f;const auto before=f.db.query("SELECT * FROM amounts");{Backend b(f.dir.path());b.startAutomaticAnalysis();}QCOMPARE(f.db.query("SELECT * FROM amounts"),before);QVERIFY(f.db.episodes().isEmpty());}
 void chartViewsExplainThemselves(){Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(b.automaticMeals().isEmpty());
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,0));QTest::qWait(100);auto *summaryLegend=item(window->contentItem(),"profileLegendSummary");QVERIFY(summaryLegend&&summaryLegend->isVisible());
    auto *nightMetric=item(window->contentItem(),"nightGlucoseMetric");QVERIFY(nightMetric&&nightMetric->isVisible());
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,1));QTest::qWait(100);auto *profileLegend=item(window->contentItem(),"profileLegend");QVERIFY(profileLegend&&profileLegend->isVisible());
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));QTest::qWait(100);auto *dayLegend=item(window->contentItem(),"dayLegend");QVERIFY(dayLegend&&dayLegend->isVisible());
    auto *title=item(window->contentItem(),"selectedDayTitle"),*previousTitle=item(window->contentItem(),"previousDayTitle");QVERIFY(title&&title->property("text").toString().startsWith("Valittu päivä · "));QVERIFY(previousTitle&&previousTitle->property("text").toString().startsWith("Edellinen päivä · "));
    auto *field=item(window->contentItem(),"dayDate");QVERIFY(field);const QString shown=field->property("text").toString();QVERIFY(!shown.isEmpty());const qint64 before=qint64(b.chart().value("from").toDouble());
    auto *next=item(window->contentItem(),"nextDay");QVERIFY(next&&next->property("enabled").toBool()&&QMetaObject::invokeMethod(next,"clicked"));QTRY_COMPARE(qint64(b.chart().value("from").toDouble()),before+86400000LL);QTRY_COMPARE(field->property("text").toString(),QDate::fromString(shown,Qt::ISODate).addDays(1).toString(Qt::ISODate));
    auto *previousButton=item(window->contentItem(),"previousDay");QVERIFY(previousButton&&QMetaObject::invokeMethod(previousButton,"clicked"));QTRY_COMPARE(qint64(b.chart().value("from").toDouble()),before);
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
    QFETCH(int,width);QTemporaryDir dir;Store store(dir.path()+"/studio.sqlite3");const QDate anchor(2026,11,8);const QTimeZone zone("Europe/Helsinki");
    QVERIFY(store.setMeta("analysisSettings",QString::fromUtf8(QJsonDocument(QJsonObject{{"timezone","Europe/Helsinki"},{"low",4},{"high",10}}).toJson())));
    QList<Reading> rows;
    for(int offset=0;offset<4;++offset)for(int i=0;i<4;++i){Reading r;r.key=QString("chart-%1-%2").arg(offset).arg(i);r.sensor="synthetic";r.time=anchor.addDays(-offset).startOfDay(zone).toMSecsSinceEpoch()+i*300000;r.mmol=i==0?2:i==2?12:6;rows<<r;}
    QVERIFY(store.ingest(rows,{},rows.last().time-86400000,rows.first().time+86400000,false));
    Backend b(dir.path());QQmlApplicationEngine engine;load(engine,b,width);QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    b.setWindow(anchor.toString(Qt::ISODate),1);QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));
    auto *profile=item(window->contentItem(),"profileTab");QVERIFY(profile);QCOMPARE(profile->property("text").toString(),QString("Vuorokausiprofiili"));
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
    QCOMPARE(week1->property("text").toString(),QString("1 vk"));
    QCOMPARE(week4->property("text").toString(),QString("4 vk"));
    QCOMPARE(months3->property("text").toString(),QString("3 kk"));
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
    QTemporaryDir dir;Store db(dir.path()+"/studio.sqlite3");
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
 void gmiCardShowsEstimateAndShortPeriodCaveat(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,0));QTest::qWait(100);
    auto *card=item(window->contentItem(),"gmiMetric");QVERIFY(card);
    const auto analysis=b.analysis();
    QVERIFY(analysis.value("gmiAvailable").toBool());
    QVERIFY(qAbs(analysis.value("gmi").toDouble()-6.327)<0.02);
    QVERIFY(qAbs(analysis.value("gmiMmol").toDouble()-45.65109)<0.2);
    QCOMPARE(analysis.value("gmiAdequate").toBool(),false);
    QCOMPARE(card->property("value").toString(),QLocale(QLocale::Finnish,QLocale::Finland).toString(6.327,'f',1));
    QCOMPARE(card->property("unit").toString(),QString("%"));
    QVERIFY(card->property("note").toString().contains(QString::fromUtf8("suuntaa-antava")));
 }
 void summaryReferenceFlagsAndColors(){
    Fixture f;Backend b(f.dir.path());b.setAnalysisWindow(7,"2026-10-06");
    const auto ref=b.analysis().value("reference").toMap();
    QCOMPARE(ref.value("meanInRange").toBool(),true);
    QCOMPARE(ref.value("cvInRange").toBool(),true);
    QCOMPARE(ref.value("coverageInRange").toBool(),true);
    QCOMPARE(ref.value("gmiInRange").toBool(),false);
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
    QCOMPARE(color("gmiMetric"),QColor("#eef2f5"));
    QCOMPARE(color("targetMetric"),QColor("#eef2f5"));
    QQmlExpression amber(qmlContext(window),window,"referenceColor(true,false)");
    QCOMPARE(amber.evaluate().value<QColor>(),QColor("#f2c66d"));
    QQmlExpression neutral(qmlContext(window),window,"referenceColor(false,true)");
    QCOMPARE(neutral.evaluate().value<QColor>(),QColor("#eef2f5"));
 }
 void chartsDefaultToCurrentDay(){
    Fixture f;Backend b(f.dir.path());QQmlApplicationEngine engine;load(engine,b);QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
    QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,2));QTest::qWait(100);
    auto *title=item(window->contentItem(),"selectedDayTitle");QVERIFY(title);
    const auto expected=QLocale(QLocale::Finnish,QLocale::Finland).toString(QDate::currentDate(),QStringLiteral("ddd d.M.yyyy"));
    QCOMPARE(title->property("text").toString(),QStringLiteral("Valittu päivä · ")+expected);
 }
};
QTEST_MAIN(AutomaticUiTest)
#include "test_automatic_ui.moc"
