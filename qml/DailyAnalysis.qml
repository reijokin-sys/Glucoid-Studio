// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import GlucoidStudio
import "."
import QtQuick.Controls
import QtQuick.Layouts
import "theme.js" as Theme

ScrollView {
    id: root
    required property Backend studio
    objectName: "dailyAnalysisView"
    Component.onCompleted: root.studio.refreshDailyAnalysis()
    contentWidth: availableWidth
    function n(value, decimals = 1) {
        return value === undefined || value === null || !Number.isFinite(Number(value)) ? "—" : Number(value).toLocaleString(Qt.locale(root.studio.language === "fi" ? "fi_FI" : "en_GB"), 'f', decimals);
    }
    ColumnLayout {
        width: root.availableWidth
        spacing: 8
        Label {
            text: qsTranslate("Studio", "Vuorokausianalyysi")
            font.pixelSize: 18
            font.bold: true
            Layout.fillWidth: true
        }
        Label {
            objectName: "dailyAnalysisPeriod"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            text: root.studio.dailyAnalysis.firstDate ? qsTranslate("Studio", "Koko historia · ") + root.studio.dailyAnalysis.firstDate + " – " + root.studio.dailyAnalysis.lastDate + " · " + root.studio.dailyAnalysis.timezone : qsTranslate("Studio", "Koko historia eiliseen asti")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            text: qsTranslate("Studio", "Jokainen päivä painaa yhtä paljon. Glukoosikeskiarvo käyttää kaikkia mitattuja päiviä. Insuliini/HH-suhde käyttää päiviä, joilta on sekä HH- että pikainsuliinikirjaus.")
        }
        Label {
            visible: root.studio.dailyAnalysis.status === "computing"
            font.pixelSize: 11
            text: qsTranslate("Studio", "Lasketaan vuorokausianalyysiä…")
        }
        Label {
            visible: root.studio.dailyAnalysis.status === "error"
            font.pixelSize: 11
            text: qsTranslate("Studio", "Aineiston lukeminen ei onnistunut. Päivitä tiedot ja yritä uudelleen.")
        }
        GridLayout {
            objectName: "dailyPeriodGrid"
            Layout.fillWidth: true
            columns: root.availableWidth >= 1150 ? 3 : root.availableWidth >= 700 ? 2 : 1
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: root.studio.dailyAnalysis.groups || []
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
                            Label {
                                text: card.modelData.label
                                font.bold: true
                                font.pixelSize: 14
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Label {
                                text: card.modelData.glucoseDays + " pv"
                                font.pixelSize: 10
                                opacity: 0.7
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 2
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    text: qsTranslate("Studio", "Glukoosi")
                                    font.pixelSize: 10
                                    opacity: 0.75
                                }
                                Label {
                                    objectName: "dailyGlucose" + card.index
                                    text: root.n(card.modelData.glucoseMean) + " mmol/l"
                                    font.pixelSize: 17
                                    font.bold: true
                                    color: Theme.accentTeal
                                }
                                Label {
                                    text: qsTranslate("Studio", "kattavuus ") + root.n(card.modelData.coverage * 100) + " %"
                                    font.pixelSize: 10
                                    opacity: 0.75
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    text: qsTranslate("Studio", "Insuliini/HH")
                                    font.pixelSize: 10
                                    opacity: 0.75
                                }
                                Label {
                                    objectName: "dailyRatio" + card.index
                                    text: root.n(card.modelData.ratioPer10g, 2) + " U / 10 g"
                                    font.pixelSize: 17
                                    font.bold: true
                                    color: Theme.accentBlue
                                }
                                Label {
                                    text: card.modelData.ratioDays + qsTranslate("Studio", " kirjauspäivää")
                                    font.pixelSize: 10
                                    opacity: 0.75
                                }
                            }
                        }
                        Disclosure {
                            objectName: "dailyCalculationDetails" + card.index
                            Layout.fillWidth: true
                            padding: 6
                            title: qsTranslate("Studio", "Laskennan tiedot")
                            Label {
                                objectName: "dailyAmountTotals" + card.index
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                                text: qsTranslate("Studio", "Kaikkien päivien kirjaukset: HH ") + root.n(card.modelData.carbs) + " g · pikainsuliini " + root.n(card.modelData.units) + qsTranslate("Studio", " U. Perusinsuliini ei sisälly määrään.")
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                                text: qsTranslate("Studio", "Suhteesta puuttuu: ") + card.modelData.onlyCarbDays + qsTranslate("Studio", " päivää ilman insuliinikirjausta · ") + card.modelData.onlyInsulinDays + qsTranslate("Studio", " päivää ilman positiivista HH-määrää. Näidenkin päivien kirjaukset sisältyvät yllä oleviin määriin.")
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                                text: qsTranslate("Studio", "Päivän suhde = 10 × pikainsuliini / HH. Näytetty suhde on päivittäisten suhteiden keskiarvo, ei kokonaismäärien suhde. Kirjaukset kuuluvat omaan kelloväliinsä ilman aterioiden yhdistämistä.")
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                                text: qsTranslate("Studio", "Glukoosi: päivän aikapainotettujen keskiarvojen keskiarvo käyttää sensorin 5 minuutin historiaa. Streamia ei sekoiteta raporttiin. Puuttuva mittaus ei ole nolla. Myös matalat ja korkeat arvot ovat mukana; ateria- ja insuliinikirjaukset eivät rajaa glukoosikeskiarvoa.")
                            }
                        }
                    }
                }
            }
        }
        Label {
            visible: root.studio.dailyAnalysis.unknownEntries > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            text: qsTranslate("Studio", "Aineistossa on luokittelemattomia kirjauksia: Juggluco ei välittänyt niille tunnistettavaa luokkaa. Ne näkyvät käyrillä, mutta eivät ole mukana hiilihydraatti- eikä insuliinisummissa. Tarkista merkinnän vientiasetus Jugglucossa vain, jos kirjaus on tehty väärin.")
        }
    }
}
