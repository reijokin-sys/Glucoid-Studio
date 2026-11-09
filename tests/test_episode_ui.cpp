// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.h"
#include "response_fixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>

class EpisodeUiTest:public QObject{
    Q_OBJECT
    static QQuickItem *item(QQuickItem *root,const QString &name){
        if(root->objectName()==name)return root;
        for(auto *child:root->childItems())if(auto *found=item(child,name))return found;
        return nullptr;
    }
    struct Fixture {
        QTemporaryDir dir;
        Store store{dir.path()+"/studio.sqlite3"};
        qint64 at=QDateTime(QDateTime::currentDateTime(QTimeZone("Europe/Helsinki")).addDays(-2).date(),QTime(8,0),QTimeZone("Europe/Helsinki")).toMSecsSinceEpoch();
        QString carbs,rapid;
        Fixture(bool missingHypo=false){
            auto p=profile();if(missingHypo)p.remove("hypo");store.saveProfile(p);
            QList<Reading> rows;for(int minute=-240;minute<=360;minute+=5){Reading r;r.key=QString::number(minute);r.time=at+minute*60000;r.sensor="synthetic";r.mmol=6;r.original=6;r.originalUnit="mmol/L";rows<<r;}
            store.ingest(rows,{},at-240*60000,at+361*60000,false);
            carbs=store.addLocal(at,40,"carbs","","ateria");rapid=store.addLocal(at,4,"rapid","Synthetic rapid","ateria");
            for(int day=0;day<3;++day)store.addLocal(at-day*86400000LL-10*3600000,16,"basal","Synthetic basal","perus");
        }
    };
    static void load(QQmlApplicationEngine &engine,Backend &backend){
        engine.rootContext()->setContextProperty("studio",&backend);
        engine.rootContext()->setContextProperty("demoMode",true);
        engine.rootContext()->setContextProperty("uiTestMode",true);
        engine.rootContext()->setContextProperty("uiTestWidth",1280);
        engine.load(QUrl::fromLocalFile(QStringLiteral(STUDIO_SOURCE_DIR)+"/qml/ResearchMain.qml"));
    }
    static void select(QQuickWindow *window,const Fixture &f,bool v2){
        QMetaObject::invokeMethod(window,"showView",Q_ARG(QVariant,3));
        window->setProperty("selectedIds",QVariantMap{{f.carbs,true},{f.rapid,true}});
        item(window->contentItem(),"episodeStart")->setProperty("text",QDateTime::fromMSecsSinceEpoch(f.at,QTimeZone("Europe/Helsinki")).toString("yyyy-MM-dd HH:mm"));
        item(window->contentItem(),"episodeV2")->setProperty("checked",v2);
    }
    static void confirm(QQuickWindow *window){
        for(auto name:{"episodeReviewed","episodeComplete","episodeContext","episodeBasalStable","episodeBasalComplete","episodeEndStable"})item(window->contentItem(),name)->setProperty("checked",true);
    }
    static void click(QQuickWindow *window){
        auto *button=item(window->contentItem(),"saveReviewedEpisode");
        QVERIFY(button);QTRY_VERIFY(button->isVisible());
        QCoreApplication::processEvents();
        const auto point=button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint();
        QVERIFY2(QRect(QPoint(),window->size()).contains(point),"Save button must be on screen for the real mouse event");
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);
    }
private slots:
    void buttonSavesAndPersists_data(){QTest::addColumn<bool>("v2");QTest::newRow("legacy")<<false;QTest::newRow("response-v2")<<true;}
    void buttonSavesAndPersists(){
        QFETCH(bool,v2);Fixture f;Backend backend(f.dir.path());QCOMPARE(backend.saveResponsePolicy(policy().toVariantMap(),true)["status"].toString(),"saved");
        QQmlApplicationEngine engine;load(engine,backend);QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        select(window,f,v2);confirm(window);QTest::qWait(100);click(window);
        QTRY_COMPARE(window->property("episodeResult").toString(),QString("Jakso tallennettu."));QCOMPARE(f.store.episodes().size(),1);
        auto e=f.store.episodes()[0].toObject();QCOMPARE(e["methodVersion"].toInt()==2,v2);QCOMPARE(e["rapid"].toInt(),4);QCOMPARE(e["carbs"].toInt(),40);QCOMPARE(e["reviewer"].toString(),profile()["reviewer"].toString());
        QVERIFY(e["reviewed"].toBool()&&e["complete"].toBool()&&e["contextClear"].toBool());QCOMPARE(window->property("selectedIds").toMap().size(),0);QVERIFY(!item(window->contentItem(),"episodeReviewed")->property("checked").toBool());QVERIFY(item(window->contentItem(),"savedEpisodes")->property("expanded").toBool());
        Store reloaded(f.dir.path()+"/studio.sqlite3");QCOMPARE(reloaded.episodes().size(),1);QCOMPARE(reloaded.query("SELECT * FROM audit WHERE kind='episode-reviewed'").size(),1);
    }
    void screenshotCaseExplainsProfileAndWritesNothing(){
        Fixture f(true);Backend backend(f.dir.path());QQmlApplicationEngine engine;load(engine,backend);QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        select(window,f,true);confirm(window);QTest::qWait(100);click(window);
        QTRY_VERIFY(window->property("episodeResult").toString().startsWith("Jaksoa ei tallennettu."));auto message=window->property("episodeResult").toString();QVERIFY(message.contains("matalan glukoosin raja"));QVERIFY(!message.contains("muutosehdotusta"));QVERIFY(!message.contains(": hypo"));QVERIFY(window->property("episodeNeedsProfile").toBool());QVERIFY(item(window->contentItem(),"episodeOpenProfile")->isVisible());QVERIFY(f.store.episodes().isEmpty());
    }
    void changingEpisodeInvalidatesConfirmations_data(){
        QTest::addColumn<QString>("change");for(auto name:{"selection","time","kind","method","source","profile"})QTest::newRow(name)<<QString(name);
    }
    void changingEpisodeInvalidatesConfirmations(){
        QFETCH(QString,change);Fixture f;Backend backend(f.dir.path());QQmlApplicationEngine engine;load(engine,backend);QVERIFY(!engine.rootObjects().isEmpty());auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        select(window,f,true);confirm(window);QVERIFY(item(window->contentItem(),"episodeReviewed")->property("checked").toBool());
        if(change=="selection")window->setProperty("selectedIds",QVariantMap{});
        if(change=="time")item(window->contentItem(),"episodeStart")->setProperty("text","2026-10-01 08:00");
        if(change=="kind")item(window->contentItem(),"episodeKind")->setProperty("currentIndex",1);
        if(change=="method")item(window->contentItem(),"episodeV2")->setProperty("checked",false);
        if(change=="source"){f.store.addLocal(f.at+86400000,1,"other","","muu");backend.refresh();}
        if(change=="profile"){f.store.saveProfile(profile());backend.refresh();}
        for(auto name:{"episodeReviewed","episodeComplete","episodeContext","episodeBasalStable","episodeBasalComplete","episodeEndStable"})QVERIFY2(!item(window->contentItem(),name)->property("checked").toBool(),name);
        QVERIFY(f.store.episodes().isEmpty());
    }
};
QTEST_MAIN(EpisodeUiTest)
#include "test_episode_ui.moc"
