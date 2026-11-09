// SPDX-License-Identifier: GPL-3.0-or-later
#include "observation.h"
#include "observation_fixture.h"
#include <QtTest>
using namespace Studio;
class ObservationTest:public QObject {
    Q_OBJECT
    static QTimeZone zone(){return QTimeZone("Europe/Helsinki");}
    static qint64 at(QDate date,QTime time=QTime(0,0)){return QDateTime(date,time,zone()).toMSecsSinceEpoch();}
    static QJsonObject amount(QDate date,double dose=16){return {{"time",double(at(date,QTime(22,0)))},{"kind","basal"},{"value",dose},{"medicine","Synthetic basal"}};}
private slots:
    void unknownCorrectionAndIntervalTargetAllowObservationOnly(){auto p=observationFixture();QVERIFY(validateObservationProfile(p).isEmpty());QVERIFY(!validateHistoricalProfile(p).isEmpty());QVERIFY(p["target"].isNull());QVERIFY(p["slots"].toArray()[0].toObject()["isf"].isNull());QCOMPARE(calculateBolus(p,{}, {},at(QDate(2026,10,15)))["status"].toString(),"blocked");}
    void invalidRegimenRejected(){auto p=observationFixture();p["basalDose"]=QJsonValue();QVERIFY(!validateObservationProfile(p).isEmpty());p=observationFixture();p["targetLow"]=20;QVERIFY(!validateObservationProfile(p).isEmpty());p=observationFixture();p["effectiveFrom"]=double(QDateTime::currentMSecsSinceEpoch()+86400000);QVERIFY(!validateObservationProfile(p).isEmpty());}
    void priorRegimenGlucoseExcluded(){auto p=observationFixture();QDate d(2026,10,1);p["effectiveFrom"]=double(at(d.addDays(1)));auto r=observeProfile(p,{{at(d),at(d.addDays(1)),18,"stream"},{at(d.addDays(1)),at(d.addDays(2)),6,"stream"}},{},d,2,zone(),{});auto stats=r["glucose"].toObject();QCOMPARE(stats["mean"].toDouble(),6.0);QCOMPARE(stats["coverage"].toDouble(),1.0);QCOMPARE(stats["inRange"].toDouble(),1.0);QCOMPARE(r["daily"].toArray().size(),1);}
    void missingBasalIsUnknownEvenOnConfirmedDay(){QDate d(2026,10,1);auto r=observeProfile(observationFixture(),{}, {},d,1,zone(),{d.toString(Qt::ISODate)});auto row=r["daily"].toArray()[0].toObject();QVERIFY(row["basalRecorded"].isNull());QVERIFY(!row["comparisonReady"].toBool());QVERIFY(!row.contains("basalDifference"));QCOMPARE(r["basalComparedDays"].toInt(),0);QCOMPARE(r["status"].toString(),"waiting_for_glucose");}
    void incompleteDayNotCompared(){QDate d(2026,10,1);auto r=observeProfile(observationFixture(),{}, {amount(d)},d,1,zone(),{});auto row=r["daily"].toArray()[0].toObject();QCOMPARE(row["basalRecorded"].toDouble(),16.0);QVERIFY(!row["comparisonReady"].toBool());QVERIFY(!row.contains("basalDifference"));}
    void confirmedDoseDifferenceIsDescriptive(){QDate d(2026,10,1);auto r=observeProfile(observationFixture(),{}, {amount(d,15)},d,1,zone(),{d.toString(Qt::ISODate)});auto row=r["daily"].toArray()[0].toObject();QVERIFY(row["comparisonReady"].toBool());QCOMPARE(row["basalDifference"].toDouble(),-1.0);QCOMPARE(r["basalComparedDays"].toInt(),1);QCOMPARE(r["basalMatchingDays"].toInt(),0);QVERIFY(!r.contains("candidate"));}
    void mixedMedicineOrMultipleInjectionsNotCompared(){QDate d(2026,10,1);auto a=amount(d);a["medicine"]="Other synthetic basal";auto r=observeProfile(observationFixture(),{}, {a},d,1,zone(),{d.toString(Qt::ISODate)});QVERIFY(!r["daily"].toArray()[0].toObject()["comparisonReady"].toBool());r=observeProfile(observationFixture(),{}, {amount(d,8),amount(d,8)},d,1,zone(),{d.toString(Qt::ISODate)});QVERIFY(!r["daily"].toArray()[0].toObject()["comparisonReady"].toBool());}
    void excludedOrUnknownEventInvalidatesComparison(){QDate d(2026,10,1);auto a=amount(d);a["analysisExcluded"]=true;auto r=observeProfile(observationFixture(),{}, {a},d,1,zone(),{d.toString(Qt::ISODate)});QVERIFY(!r["daily"].toArray()[0].toObject()["comparisonReady"].toBool());a=amount(d);a["kind"]="unknown";r=observeProfile(observationFixture(),{}, {amount(d),a},d,1,zone(),{d.toString(Qt::ISODate)});QVERIFY(!r["daily"].toArray()[0].toObject()["comparisonReady"].toBool());}
    void partialProfileDayHasNoFullDayDoseComparison(){QDate d(2026,10,1);auto p=observationFixture();p["effectiveFrom"]=double(at(d,QTime(12,0)));auto r=observeProfile(p,{{at(d),at(d.addDays(1)),6,"stream"}}, {amount(d)},d,1,zone(),{d.toString(Qt::ISODate)});QVERIFY(!r["daily"].toArray()[0].toObject()["comparisonReady"].toBool());QCOMPARE(r["glucose"].toObject()["coverage"].toDouble(),1.0);QCOMPARE(r["glucose"].toObject()["coveredSeconds"].toDouble(),43200.0);}
};
QTEST_GUILESS_MAIN(ObservationTest)
#include "test_observation.moc"
