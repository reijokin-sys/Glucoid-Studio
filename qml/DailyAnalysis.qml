// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "theme.js" as Theme

ScrollView {
    id: root
    objectName: "dailyAnalysisView"
    Component.onCompleted: studio.refreshDailyAnalysis()
    contentWidth: availableWidth
    function n(value,decimals=1) { return value === undefined || value === null || !Number.isFinite(Number(value)) ? "—" : Number(value).toLocaleString(Qt.locale("fi_FI"),'f',decimals) }
    ColumnLayout {
        width: root.availableWidth; spacing: 8
        Label { text: "Vuorokausianalyysi"; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
        Label { objectName: "dailyAnalysisPeriod"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11
            text: studio.dailyAnalysis.firstDate ? "Koko historia · " + studio.dailyAnalysis.firstDate + " – " + studio.dailyAnalysis.lastDate + " · " + studio.dailyAnalysis.timezone : "Koko historia eiliseen asti" }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11
            text: "Jokainen päivä painaa yhtä paljon. Glukoosikeskiarvo käyttää kaikkia mitattuja päiviä. Insuliini/HH-suhde käyttää päiviä, joilta on sekä HH- että pikainsuliinikirjaus." }
        Label { visible: studio.dailyAnalysis.status === "computing"; font.pixelSize: 11; text: "Lasketaan vuorokausianalyysiä…" }
        Label { visible: studio.dailyAnalysis.status === "error"; font.pixelSize: 11; text: "Aineiston lukeminen ei onnistunut. Päivitä tiedot ja yritä uudelleen." }
        GridLayout {
            objectName: "dailyPeriodGrid"
            Layout.fillWidth: true
            columns: root.availableWidth >= 1150 ? 3 : root.availableWidth >= 700 ? 2 : 1
            columnSpacing: 8; rowSpacing: 8
            Repeater {
                model: studio.dailyAnalysis.groups || []
                delegate: Frame {
                    id: card
                    required property var modelData
                    required property int index
                    objectName: "dailyPeriodCard" + index
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    padding: 9
                    contentItem: ColumnLayout {
                        spacing: 3
                        RowLayout {
                            spacing: 6
                            Label { text: card.modelData.label; font.bold: true; font.pixelSize: 14 }
                            Item { Layout.fillWidth: true }
                            Label { text: card.modelData.glucoseDays + " pv"; font.pixelSize: 10; opacity: 0.7 }
                        }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2; columnSpacing: 12; rowSpacing: 2
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 0
                                Label { text: "Glukoosi"; font.pixelSize: 10; opacity: 0.75 }
                                Label { objectName: "dailyGlucose" + card.index; text: root.n(card.modelData.glucoseMean) + " mmol/l"; font.pixelSize: 17; font.bold: true; color: Theme.accentTeal }
                                Label { text: "kattavuus " + root.n(card.modelData.coverage * 100) + " %"; font.pixelSize: 10; opacity: 0.75 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 0
                                Label { text: "Insuliini/HH"; font.pixelSize: 10; opacity: 0.75 }
                                Label { objectName: "dailyRatio" + card.index; text: root.n(card.modelData.ratioPer10g,2) + " U / 10 g"; font.pixelSize: 17; font.bold: true; color: Theme.accentBlue }
                                Label { text: card.modelData.ratioDays + " kirjauspäivää"; font.pixelSize: 10; opacity: 0.75 }
                            }
                        }
                        Disclosure {
                            objectName: "dailyCalculationDetails" + card.index
                            Layout.fillWidth: true; padding: 6; title: "Laskennan tiedot"
                            Label { objectName: "dailyAmountTotals" + card.index; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; text: "Kaikkien päivien kirjaukset: HH " + root.n(card.modelData.carbs) + " g · pikainsuliini " + root.n(card.modelData.units) + " U. Mukana ovat ateria- ja korjausannokset, eivät perusinsuliinit." }
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; text: "Suhteesta puuttuu: " + card.modelData.onlyCarbDays + " päivää ilman insuliinikirjausta · " + card.modelData.onlyInsulinDays + " päivää ilman positiivista HH-määrää. Näidenkin päivien kirjaukset sisältyvät yllä oleviin määriin." }
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; text: "Päivän suhde = 10 × pikainsuliini / HH. Näytetty suhde on päivittäisten suhteiden keskiarvo, ei kokonaismäärien suhde. Kirjaukset kuuluvat omaan kelloväliinsä ilman aterioiden yhdistämistä." }
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; text: "Glukoosi: päivän aikapainotettujen keskiarvojen keskiarvo. Stream on ensisijainen, history täydentää aukot (" + root.n(card.modelData.historyFraction * 100) + " % katetusta ajasta). Puuttuva mittaus ei ole nolla. Myös matalat ja korkeat arvot ovat mukana; ateria- ja insuliinikirjaukset eivät rajaa glukoosikeskiarvoa." }
                        }
                    }
                }
            }
        }
        Label { visible: studio.dailyAnalysis.unknownEntries > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; text: "Aineistossa on luokittelemattomia kirjauksia. Tarkista tietolähteen kirjausluokat asetuksissa." }
    }
}
