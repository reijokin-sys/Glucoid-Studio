#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dailyanalysis.h"
#include "store.h"
#include <QtTest>
#include <QTemporaryDir>
using namespace Studio;
class DailyAnalysisTest:public QObject {
    Q_OBJECT
    const QTimeZone zone{"UTC"};
    const QDate day{2026,10,1};
    qint64 at(QDate date,int hour=6,int minute=0)const{return QDateTime(date,QTime(hour,minute),zone).toMSecsSinceEpoch();}
    Reading reading(qint64 time,double value,QString kind="stream")const{
        Reading r;r.key=QString::number(time);r.time=time;r.sensor="synthetic";r.mmol=value;r.seriesKind=kind;return r;
    }
    QJsonObject amount(QDate date,double value,QString kind,int hour=6,int minute=0)const{
        return {{"time",double(at(date,hour,minute))},{"value",value},{"kind",kind}};
    }
private slots:
    void averagesDailyRatiosNotPooledTotals(){
        QJsonArray events{amount(day,20,"carbs"),amount(day,1,"rapid"),amount(day,1,"correction",8,59),
            amount(day,100,"basal"),amount(day.addDays(1),80,"carbs"),amount(day.addDays(1),4,"rapid"),
            amount(day.addDays(2),10,"carbs"),amount(day.addDays(3),3,"correction")};
        auto excluded=amount(day,100,"rapid");excluded["analysisExcluded"]=true;events<<excluded;
        auto deleted=amount(day,100,"carbs");deleted["deleted"]=true;events<<deleted;
        auto inactive=amount(day,100,"rapid");inactive["active"]=false;events<<inactive;
        const auto result=dailyPeriodAnalysis({},events,zone,day.addDays(3));
        const auto row=result["groups"].toArray()[0].toObject();
        QCOMPARE(row["ratioPer10g"].toDouble(),.75);
        QCOMPARE(row["ratioDays"].toInt(),2);QCOMPARE(row["carbs"].toDouble(),110.0);
        QCOMPARE(row["units"].toDouble(),9.0);QCOMPARE(row["onlyCarbDays"].toInt(),1);QCOMPARE(row["onlyInsulinDays"].toInt(),1);
        QVERIFY(!row.contains("glucoseMean"));
    }
    void glucoseDaysAreEquallyWeightedAndIndependentOfLogs(){
        QList<Reading> history;
        for(int m=0;m<180;++m)history<<reading(at(day)+m*60000,6,"history");
        history<<reading(at(day.addDays(1)),10,"history");
        auto result=dailyPeriodAnalysis(history,QJsonArray{amount(day.addDays(2),20,"carbs")},zone,day.addDays(2));
        auto row=result["groups"].toArray()[0].toObject();
        QCOMPARE(row["glucoseMean"].toDouble(),8.0);QCOMPARE(row["glucoseDays"].toInt(),2);
        QCOMPARE(row["ratioDays"].toInt(),0);QVERIFY(!row.contains("ratioPer10g"));
        QVERIFY(qAbs(row["coverage"].toDouble()-(10800.0+300.0)/32400.0)<1e-10);
    }
    void clockBoundariesAndIncompleteToday(){
        QJsonArray events;
        for(int h:{0,6,9,12,15,18,21})events<<amount(day,10,"carbs",h)<<amount(day,1,"rapid",h);
        events<<amount(day.addDays(1),100,"carbs",0)<<amount(day.addDays(1),100,"rapid",0);
        const auto rows=dailyPeriodAnalysis({},events,zone,day)["groups"].toArray();
        QCOMPARE(rows.size(),7);
        for(auto value:rows){const auto r=value.toObject();QCOMPARE(r["ratioDays"].toInt(),1);QCOMPARE(r["ratioPer10g"].toDouble(),1.0);QCOMPARE(r["carbs"].toDouble(),10.0);}
        QCOMPARE(rows[0].toObject()["label"].toString(),QString::fromUtf8("06:00–09:00"));
        QCOMPARE(rows[5].toObject()["label"].toString(),QString::fromUtf8("21:00–00:00"));
        QCOMPARE(rows[6].toObject()["label"].toString(),QString::fromUtf8("00:00–06:00"));
    }
    void reportUsesHistoryWithoutStreamMixing(){
        const auto start=at(day);
        const auto row=dailyPeriodAnalysis(
            {reading(start,10,"history"),reading(start+300000,10,"history")},{},zone,day)["groups"].toArray()[0].toObject();
        QCOMPARE(row["glucoseMean"].toDouble(),10.0);
        QCOMPARE(row["coverage"].toDouble(),600.0/10800);
        QVERIFY(!row.contains("historySeconds"));
        QVERIFY(!row.contains("historyFraction"));
    }
    void dstAndNoData(){
        const QTimeZone helsinki("Europe/Helsinki");
        for(auto date:{QDate(2026,3,29),QDate(2026,10,25)}){
            QList<Reading> rows;
            const auto a=QDateTime(date,QTime(0,0),helsinki).toMSecsSinceEpoch(),b=QDateTime(date,QTime(6,0),helsinki).toMSecsSinceEpoch();
            for(auto t=a;t<b;t+=60000)rows<<reading(t,7);
            const auto r=dailyPeriodAnalysis(rows,{},helsinki,date)["groups"].toArray()[6].toObject();
            QCOMPARE(r["coverage"].toDouble(),1.0);QCOMPARE(r["glucoseMean"].toDouble(),7.0);
        }
        const auto empty=dailyPeriodAnalysis({}, {},zone,day);
        QCOMPARE(empty["groups"].toArray().size(),7);QCOMPARE(empty["calendarDays"].toInt(),0);
        for(auto v:empty["groups"].toArray()){QVERIFY(!v.toObject().contains("glucoseMean"));QVERIFY(!v.toObject().contains("ratioPer10g"));}
    }
    void constructorPurgesRetiredDataOnly(){
        QTemporaryDir dir;const auto file=dir.path()+"/studio.sqlite3";
        QJsonArray before;
        {FixtureStore db(file);QVERIFY(db.setMeta("supplementDaysV1","synthetic-retired-record"));
            QVERIFY(db.run("INSERT INTO audit(time,kind,data) VALUES(1,'supplement-day','synthetic-retired-record')"));
            QVERIFY(db.run("INSERT INTO audit(time,kind,data) VALUES(2,'keep','synthetic-kept-record')"));
            QVERIFY(db.setMeta("keep","unchanged"));before=db.query("SELECT * FROM audit WHERE kind='keep'");}
        {FixtureStore db(file);QVERIFY(db.good());QVERIFY(db.meta("supplementDaysV1").isEmpty());
            QCOMPARE(db.meta("keep"),QString("unchanged"));QCOMPARE(db.query("SELECT * FROM audit WHERE kind='keep'"),before);
            QVERIFY(db.query("SELECT * FROM audit WHERE kind GLOB 'supplement-*'").isEmpty());}
    }
};
QTEST_GUILESS_MAIN(DailyAnalysisTest)
#include "test_dailyanalysis.moc"
