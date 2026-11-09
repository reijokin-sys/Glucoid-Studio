// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import GlucoidStudio
import "."
import "theme.js" as Theme
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: window
    required property Backend studio
    required property string appVersion
    required property bool demoMode
    required property bool uiTestMode
    required property int uiTestWidth
    Kirigami.Theme.backgroundColor: Theme.bg
    Kirigami.Theme.alternateBackgroundColor: Theme.surface
    Kirigami.Theme.textColor: Theme.text
    Kirigami.Theme.disabledTextColor: Theme.textDisabled
    Kirigami.Theme.activeTextColor: Theme.text
    Kirigami.Theme.highlightColor: Theme.accentTeal
    Kirigami.Theme.linkColor: Theme.accentBlue
    Kirigami.Theme.positiveTextColor: Theme.accentTeal
    Kirigami.Theme.neutralTextColor: Theme.textSecondary
    Kirigami.Theme.negativeTextColor: Theme.alertRed
    color: Theme.bg
    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.bg
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.elevated
    palette.buttonText: Theme.text
    palette.highlight: Theme.accentTeal
    palette.highlightedText: Theme.bg
    palette.placeholderText: Theme.textDisabled
    palette.toolTipBase: Theme.elevated
    palette.toolTipText: Theme.text
    palette.light: Theme.surface
    palette.midlight: Theme.border
    palette.mid: Theme.border
    palette.dark: Theme.bg
    palette.shadow: Theme.bg
    flags: Qt.Window | (window.uiTestMode ? Qt.WindowStaysOnTopHint : 0)
    width: window.uiTestWidth > 0 ? window.uiTestWidth : 1280
    height: 900
    minimumWidth: 900
    minimumHeight: 650
    readonly property string applicationTitle: "Glucoid Studio" + (typeof window.appVersion === "string" && window.appVersion.length ? " " + window.appVersion : "")
    visible: true
    title: applicationTitle
    function n(v, d = 1) {
        return v === null || v === undefined || !Number.isFinite(Number(v)) ? "—" : Number(v).toLocaleString(Qt.locale("fi_FI"), 'f', d);
    }
    function stamp(v) {
        return v > 0 ? new Date(v).toLocaleString(Qt.locale("fi_FI")) : "Ei tietoa";
    }
    function isoDate(v) {
        const d = new Date(v);
        const p = x => String(x).padStart(2, "0");
        return !isNaN(d.getTime()) ? d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate()) : "";
    }
    function longDate(v) {
        const d = new Date(v);
        return isNaN(d.getTime()) ? "" : d.toLocaleDateString(Qt.locale("fi_FI"), "ddd d.M.yyyy");
    }
    function shiftDay(v, days) {
        const d = new Date(v);
        d.setDate(d.getDate() + days);
        return isoDate(d.getTime());
    }
    function shiftDate(date, days) {
        const p = date.split("-").map(Number);
        return new Date(Date.UTC(p[0], p[1] - 1, p[2] + days)).toISOString().slice(0, 10);
    }
    function decimal(v) {
        return String(v).trim().length ? Number(String(v).replace(",", ".")) : NaN;
    }
    function referenceColor(available, inRange) {
        return !available ? Theme.text : (inRange ? Theme.accentTeal : Theme.accentAmber);
    }
    function showView(index) {
        if (index === 0)
            tabs.currentIndex = 0;
        else if (index === 3)
            tabs.currentIndex = 1;
        else if (index === 4)
            settingsDialog.open();
        else {
            tabs.currentIndex = 2;
            curveTabs.currentIndex = index === 1 ? 0 : 1;
        }
    }
    Component.onCompleted: {
        window.studio.setWindow(window.studio.currentDayChart.date || window.isoDate(new Date().getTime()), 1);
    }
    pageStack.initialPage: Kirigami.Page {
        objectName: "mainApplicationPage"
        title: window.applicationTitle
        background: Rectangle {
            color: Theme.bg
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            RowLayout {
                Kirigami.Heading {
                    text: "Oma seuranta"
                    Layout.fillWidth: true
                }
                BusyIndicator {
                    running: window.studio.busy || window.studio.automaticBusy
                    visible: running
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }
                Button {
                    text: window.studio.busy ? "Päivitetään…" : "Päivitä tiedot"
                    enabled: !window.studio.busy && !window.demoMode
                    onClicked: window.studio.sync()
                }
                ToolButton {
                    text: "Asetukset"
                    onClicked: settingsDialog.open()
                }
                ToolButton {
                    text: "Lisää"
                    onClicked: moreMenu.open()
                    Menu {
                        id: moreMenu
                        MenuItem {
                            text: "Tallenna raportti"
                            onTriggered: window.studio.saveReport()
                        }
                        MenuItem {
                            text: "Varmuuskopioi"
                            onTriggered: window.studio.backup()
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !window.studio.status.startsWith("Valmis.")
                text: window.studio.status
                elide: Text.ElideRight
                HoverHandler {
                    id: statusHover
                }
                ToolTip.visible: statusHover.hovered
                ToolTip.text: text
            }
            Label {
                Layout.fillWidth: true
                opacity: 0.7
                elide: Text.ElideRight
                text: "Lähde: " + (window.studio.connection.source === "nightscout" ? "Nightscout" : "Juggluco") + " · Glukoosi: " + window.stamp(window.studio.analysis.lastReading) + " · Kirjaukset: " + window.stamp(window.studio.analysis.lastAmount)
            }
            TabBar {
                id: tabs
                objectName: "mainTabs"
                Layout.fillWidth: true
                TabButton {
                    text: "Yhteenveto"
                }
                TabButton {
                    text: "Vuorokausianalyysi"
                }
                TabButton {
                    text: "Käyrät"
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabs.currentIndex
                ScrollView {
                    id: summaryScroll
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: summaryScroll.availableWidth
                        spacing: 10
                        RowLayout {
                            Repeater {
                                model: [
                                    {
                                        days: 7,
                                        label: "1 vk"
                                    },
                                    {
                                        days: 28,
                                        label: "4 vk"
                                    },
                                    {
                                        days: 90,
                                        label: "3 kk"
                                    }
                                ]
                                delegate: Button {
                                    id: periodButton
                                    required property var modelData
                                    objectName: "analysisPeriod" + periodButton.modelData.days
                                    readonly property bool active: window.studio.analysis.days === periodButton.modelData.days
                                    text: periodButton.modelData.label
                                    implicitWidth: 56
                                    implicitHeight: 30
                                    padding: 0
                                    onClicked: window.studio.setAnalysisWindow(periodButton.modelData.days, "")
                                    background: Rectangle {
                                        radius: 4
                                        color: periodButton.active ? Theme.accentTeal : (periodButton.down ? Theme.border : Theme.elevated)
                                        border.width: 1
                                        border.color: periodButton.active ? Theme.accentTeal : Theme.border
                                    }
                                    contentItem: Label {
                                        text: periodButton.text
                                        color: periodButton.active ? Theme.bg : Theme.text
                                        font.bold: periodButton.active
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                            Label {
                                text: (window.studio.analysis.firstDate || "") + " – " + (window.studio.analysis.lastDate || "")
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignRight
                            }
                            ToolButton {
                                text: "Päivä"
                                onClicked: periodPopup.open()
                            }
                        }
                        Frame {
                            id: currentCard
                            objectName: "currentGlucoseCard"
                            Layout.fillWidth: true
                            background: Rectangle {
                                color: Theme.surface
                                radius: 4
                            }
                            property var reading: window.studio.currentReading || ({})
                            property bool fresh: reading.hasData === true && reading.stale !== true
                            property color stateColor: reading.status === "ok" ? Theme.accentTeal : reading.status === "warning" ? Theme.accentAmber : reading.status === "critical" ? Theme.alertRed : reading.status === "stale" ? Theme.textDisabled : Theme.textSecondary
                            property string stateText: reading.status === "ok" ? "Tavoitealueella" : reading.status === "warning" ? "Tavoitealueen ulkopuolella" : reading.status === "critical" ? "Matala lukema" : reading.status === "stale" ? "Mittaus ei ole tuore" : "Ei tavoitealuetta"
                            contentItem: RowLayout {
                                spacing: 16
                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.fillHeight: true
                                    implicitHeight: 64
                                    radius: 3
                                    color: currentCard.stateColor
                                }
                                ColumnLayout {
                                    Label {
                                        text: "Viimeisin glukoosi"
                                        opacity: 0.75
                                    }
                                    RowLayout {
                                        Label {
                                            objectName: "currentGlucoseValue"
                                            font.pixelSize: 40
                                            font.bold: true
                                            color: currentCard.stateColor
                                            text: currentCard.fresh ? window.n(currentCard.reading.value, 1) : "—"
                                        }
                                        Label {
                                            text: "mmol/l"
                                            opacity: 0.75
                                            Layout.alignment: Qt.AlignBottom
                                        }
                                        Label {
                                            objectName: "currentGlucoseTrend"
                                            font.pixelSize: 32
                                            color: currentCard.stateColor
                                            text: currentCard.reading.hasData ? currentCard.reading.trend : ""
                                            Layout.leftMargin: 8
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Label {
                                        objectName: "currentGlucoseDelta"
                                        text: currentCard.reading.rateKnown ? (currentCard.reading.rate > 0 ? "+" : "") + window.n(currentCard.reading.rate, 2) + " mmol/l/min" : "Muutos ei tiedossa"
                                    }
                                    Label {
                                        objectName: "currentGlucoseTime"
                                        text: currentCard.reading.hasData ? "klo " + new Date(currentCard.reading.time).toLocaleTimeString(Qt.locale("fi_FI"), "HH:mm") : ""
                                    }
                                    Label {
                                        objectName: "currentGlucoseAge"
                                        text: currentCard.reading.hasData ? (currentCard.reading.ageMinutes === 0 ? "nyt" : window.n(currentCard.reading.ageMinutes, 0) + " min sitten") : "Ei mittausta"
                                    }
                                    Label {
                                        objectName: "currentGlucoseSource"
                                        text: currentCard.reading.seriesKind === "history" ? "Sensorihistoria (täydennys)" : currentCard.reading.hasData ? "Stream (minuuttisarja)" : ""
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Label {
                                    objectName: "currentGlucoseState"
                                    text: currentCard.stateText
                                    color: currentCard.stateColor
                                    font.bold: true
                                }
                            }
                            Timer {
                                interval: 30000
                                running: true
                                repeat: true
                                onTriggered: window.studio.refreshCurrentReading()
                            }
                        }
                        GridLayout {
                            objectName: "summaryMetrics"
                            Layout.fillWidth: true
                            columns: window.width >= 1200 ? 5 : window.width >= 760 ? 3 : 2
                            columnSpacing: 10
                            rowSpacing: 10
                            MetricCard {
                                objectName: "meanMetric"
                                Layout.fillWidth: true
                                title: "Keskiarvo"
                                value: window.n(window.studio.analysis.mean)
                                unit: "mmol/l"
                                note: "Aikapainotettu keskiarvo koko jaksolta"
                                valueColor: window.referenceColor(window.studio.analysis.mean !== undefined, (window.studio.analysis.reference || {}).meanInRange)
                            }
                            MetricCard {
                                objectName: "gmiMetric"
                                Layout.fillWidth: true
                                title: "GMI (arvioitu HbA1c)"
                                value: window.studio.analysis.gmiAvailable ? window.n(window.studio.analysis.gmi, 1) : "—"
                                unit: window.studio.analysis.gmiAvailable ? "%" : ""
                                note: window.studio.analysis.gmiAvailable ? window.n(window.studio.analysis.gmiMmol, 0) + " mmol/mol · " + (window.studio.analysis.gmiAdequate ? "Arvio keskiarvosta, ei HbA1c-mittaus" : "Lyhyt tai vajaa jakso – suuntaa-antava") : "Ei riittävästi mittauksia"
                                accent: Theme.accentPurple
                                valueColor: window.referenceColor(window.studio.analysis.gmiAvailable, (window.studio.analysis.reference || {}).gmiInRange)
                            }
                            MetricCard {
                                objectName: "targetMetric"
                                Layout.fillWidth: true
                                title: "Tavoitealueella"
                                value: window.studio.analysis.targetsSet ? window.n(window.studio.analysis.inRange * 100) : "—"
                                unit: "%"
                                note: window.studio.analysis.targetsSet ? "Osuus mittausajasta alueella " + window.n(window.studio.analysis.low) + "–" + window.n(window.studio.analysis.high) + " mmol/l" : "Tavoitealuetta ei ole asetettu"
                                fraction: window.studio.analysis.targetsSet ? window.studio.analysis.inRange : NaN
                                accent: Theme.accentTeal
                                valueColor: window.referenceColor(window.studio.analysis.targetsSet && window.studio.analysis.inRange !== undefined, (window.studio.analysis.reference || {}).tirInRange)
                            }
                            MetricCard {
                                objectName: "cvMetric"
                                Layout.fillWidth: true
                                title: "Vaihtelu (CV)"
                                value: window.n(window.studio.analysis.cv)
                                unit: "%"
                                note: "Pienempi luku tarkoittaa tasaisempaa glukoosia"
                                accent: Theme.accentAmber
                                valueColor: window.referenceColor(window.studio.analysis.cv !== undefined, (window.studio.analysis.reference || {}).cvInRange)
                            }
                            MetricCard {
                                objectName: "coverageMetric"
                                Layout.fillWidth: true
                                title: "Datan kattavuus"
                                value: window.n(window.studio.analysis.coverage * 100)
                                unit: "%"
                                note: "Osuus jaksosta, jolta on mittauksia"
                                fraction: window.studio.analysis.coverage
                                accent: Theme.accentBlue
                                valueColor: window.referenceColor(window.studio.analysis.coveredSeconds > 0, (window.studio.analysis.reference || {}).coverageInRange)
                            }
                        }
                        Disclosure {
                            objectName: "referenceValues"
                            Layout.fillWidth: true
                            title: "Viitearvot ja värien merkitys"
                            summary: "Vihreä = viitearvossa · keltainen = viitearvon ulkopuolella · harmaa = ei arvioitavissa"
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                text: "Värit koskevat vain lukuja eivätkä ole hoito-ohje. Viitearvot ovat yleisiä, eivät henkilökohtaisia hoitotavoitteita. Henkilökohtaiset tavoitteet tulevat hoitotiimiltä."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                text: "Keskiarvo ja öiden keskiarvo: 3,9–8,6 mmol/l. Alaraja on hypoglykemiaraja 3,9 mmol/l; yläraja vastaa HbA1c:n yleistä tavoitetta 7 % (eAG ≈ 8,6 mmol/l)."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                text: "GMI: < 7,0 % (< 53 mmol/mol). Viitearvoväri näytetään myös lyhyelle tai vajaalle jaksolle; tällöin arvio on suuntaa-antava."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                text: "Tavoitealueella: ≥ 70 %. Kansainvälinen CGM-konsensus käyttää aluetta 3,9–10,0 mmol/l; sovelluksen oma tavoitealue voi poiketa, joten prosentti ei ole suoraan sama asia."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                text: "Vaihtelu (CV): ≤ 36 %. Datan kattavuus: ≥ 70 % (14 päivän suositus)."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                text: "Lähteet: Battelino ym., Diabetes Care 2019 (doi:10.2337/dci19-0028); ADA Standards of Care 2025, HbA1c < 7 %; Nathan ym., Diabetes Care 2008 (eAG)."
                            }
                        }
                        MetricCard {
                            objectName: "nightGlucoseMetric"
                            Layout.fillWidth: true
                            title: "Öiden keskiarvo"
                            value: window.n(window.studio.analysis.nightMean)
                            unit: "mmol/l"
                            note: "Aikapainotettu · valittu jakso · klo 00–06\nYöajan kattavuus " + window.n((window.studio.analysis.nightCoverage || 0) * 100) + " %"
                            accent: Theme.accentPurple
                            valueColor: window.referenceColor(window.studio.analysis.nightMean !== undefined, (window.studio.analysis.reference || {}).nightInRange)
                        }
                        Label {
                            objectName: "summarySources"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: window.studio.analysis.coveredSeconds > 0 ? "Stream ensisijainen; sensorihistoria täydentää vain aukot. History-täydennys " + window.n(window.studio.analysis.historyFraction * 100) + " % katetusta ajasta (" + window.n(window.studio.analysis.historySeconds / 3600) + " h)." : "Stream ensisijainen; sensorihistoria täydentää vain aukot. Ei mittauksia valitulla jaksolla."
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: window.studio.analysis.targetsSet ? "Alle tavoitteen " + window.n(window.studio.analysis.below * 100) + " % · Yli tavoitteen " + window.n(window.studio.analysis.above * 100) + " %" : "Seurannan tavoitealuetta ei ole asetettu."
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            visible: window.studio.observation.status === "observing"
                            text: "Nykyisen hoidon aikana tavoitteessa " + window.n((window.studio.observation.glucose || {}).inRange * 100) + " % · kattavuus " + window.n((window.studio.observation.glucose || {}).coverage * 100) + " %"
                        }
                        Label {
                            objectName: "summaryCurrentDayTitle"
                            text: "Kuluvan päivän käyrä" + (window.studio.currentDayChart.dateLabel ? " · " + window.studio.currentDayChart.dateLabel : "")
                            font.bold: true
                            font.pixelSize: 16
                        }
                        CurrentDayChart {
                            objectName: "summaryCurrentDayChart"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 260
                            chartData: window.studio.currentDayChart
                        }
                        Legend {
                            objectName: "summaryCurrentDayLegend"
                            Layout.fillWidth: true
                            items: [
                                {
                                    color: Theme.accentTeal,
                                    text: "Glukoosi",
                                    shape: "line"
                                }
                            ].concat(window.studio.analysis.targetsSet ? [
                                {
                                    color: Theme.alertRed,
                                    text: "Tavoitealueen ulkopuolella",
                                    shape: "line"
                                },
                                {
                                    color: Theme.chartTargetEdge,
                                    text: "Tavoitealue",
                                    shape: "square"
                                }
                            ] : [])
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                            text: "Tämän päivän mittaukset klo 00–24. Stream ensisijainen; sensorihistoria täydentää aukot."
                        }
                        Label {
                            text: "Vuorokausiprofiili"
                            font.bold: true
                            font.pixelSize: 16
                        }
                        ProfileChart {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 280
                            profileData: window.studio.analysis
                        }
                        Legend {
                            objectName: "profileLegendSummary"
                            Layout.fillWidth: true
                            items: [
                                {
                                    color: Theme.chartMean,
                                    text: "Keskiarvo",
                                    shape: "line"
                                },
                                {
                                    color: Theme.chartMedian,
                                    text: "Mediaani",
                                    shape: "line"
                                },
                                {
                                    color: Theme.chart2575Edge,
                                    text: "25–75 % päivistä",
                                    shape: "square"
                                },
                                {
                                    color: Theme.chart595Edge,
                                    text: "5–95 % päivistä",
                                    shape: "square"
                                },
                                {
                                    color: Theme.chartTargetEdge,
                                    text: "Tavoitealue",
                                    shape: "square"
                                }
                            ]
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                            text: "Vaaka-akseli: kellonaika · pystyakseli: mmol/l. Katkos tarkoittaa, ettei siltä ajalta ole mittauksia."
                        }
                        Disclosure {
                            objectName: "summaryStatistics"
                            Layout.fillWidth: true
                            title: "Tilastot ja vertailu"
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Mediaani " + window.n(window.studio.analysis.median) + " mmol/l · Keskihajonta " + window.n(window.studio.analysis.sd) + " mmol/l"
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: window.studio.analysis.gmiAvailable
                                text: "GMI on CGM-keskiarvosta laskettu arvio HbA1c:stä, ei laboratorio-HbA1c. GMI-% = 3,31 + 0,02392 × keskiarvo (mg/dl); mmol/mol = 12,71 + 4,70587 × keskiarvo (mmol/l). Kun HbA1c:hen vaikuttavat muut tekijät kuin verensokeri – esimerkiksi hemoglobiinitaso, punasolujen elinikä, anemia, hemoglobiinivariantit tai munuaissairaus – GMI voi olla tarkempi kuva keskimääräisestä verensokerista. GMI ei silti ole mitattu HbA1c eikä automaattisesti tarkempi: myös CGM-kattavuus ja sensorin tarkkuus vaikuttavat. Luotettava perusta on vähintään 14 päivää ja 70 %:n kattavuus; lyhyempi tai vajaa jakso on suuntaa-antava."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Edellinen jakso: keskiarvo " + window.n((window.studio.analysis.previous || {}).mean) + " mmol/l"
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Tavoitealueella = osuus mittausajasta asetettujen rajojen sisällä. CV = keskihajonta jaettuna keskiarvolla. Kattavuus = osuus jaksosta, jolta on mittauksia."
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Keskeneräinen päivä ei kuulu yhteenvetoon. Puuttuva mittausaika ei ole tavoitealueella vietettyä aikaa. Vuorokausikäyrän alueet kuvaavat 5.–95. ja 25.–75. persentiilejä."
                            }
                        }
                    }
                }
                DailyAnalysis {
                    studio: window.studio
                }
                ColumnLayout {
                    TabBar {
                        id: curveTabs
                        Layout.fillWidth: true
                        TabButton {
                            objectName: "profileTab"
                            text: "Vuorokausiprofiili"
                        }
                        TabButton {
                            text: "Päivävertailu"
                        }
                    }
                    StackLayout {
                        currentIndex: curveTabs.currentIndex
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ColumnLayout {
                            ProfileChart {
                                objectName: "profileChart"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                profileData: window.studio.analysis
                            }
                            Legend {
                                objectName: "profileLegend"
                                Layout.fillWidth: true
                                items: [
                                    {
                                        color: Theme.chartMean,
                                        text: "Keskiarvo",
                                        shape: "line"
                                    },
                                    {
                                        color: Theme.chartMedian,
                                        text: "Mediaani",
                                        shape: "line"
                                    },
                                    {
                                        color: Theme.chart2575Edge,
                                        text: "25–75 % päivistä",
                                        shape: "square"
                                    },
                                    {
                                        color: Theme.chart595Edge,
                                        text: "5–95 % päivistä",
                                        shape: "square"
                                    },
                                    {
                                        color: Theme.chartTargetEdge,
                                        text: "Tavoitealue",
                                        shape: "square"
                                    }
                                ]
                            }
                        }
                        ColumnLayout {
                            id: dayComparisonView
                            property string displayedDate: ""
                            RowLayout {
                                Label {
                                    text: "Päivä"
                                }
                                ToolButton {
                                    objectName: "previousDay"
                                    text: "‹"
                                    enabled: dayComparisonView.displayedDate.length > 0
                                    ToolTip.text: "Edellinen päivä"
                                    ToolTip.visible: hovered
                                    onClicked: window.studio.setWindow(window.shiftDate(dayComparisonView.displayedDate, -1), 1)
                                }
                                TextField {
                                    id: dayDate
                                    objectName: "dayDate"
                                    placeholderText: "vvvv-kk-pp"
                                    onAccepted: window.studio.setWindow(text, 1)
                                }
                                ToolButton {
                                    objectName: "nextDay"
                                    text: "›"
                                    enabled: dayComparisonView.displayedDate.length > 0
                                    ToolTip.text: "Seuraava päivä"
                                    ToolTip.visible: hovered
                                    onClicked: window.studio.setWindow(window.shiftDate(dayComparisonView.displayedDate, 1), 1)
                                }
                                Button {
                                    text: "Näytä päivä"
                                    onClicked: window.studio.setWindow(dayDate.text, 1)
                                }
                                Button {
                                    objectName: "comparisonToday"
                                    text: "Tänään"
                                    onClicked: window.studio.setWindow(window.studio.currentDayChart.date, 1)
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                            }
                            Connections {
                                target: window.studio
                                function onChanged() {
                                    const date = (window.studio.dayComparisonCharts[0] || {}).date || "";
                                    if (!dayDate.activeFocus)
                                        dayDate.text = date;
                                    if (dayComparisonView.displayedDate !== date) {
                                        dayComparisonView.displayedDate = date;
                                        if (dayComparisonScroll.contentItem)
                                            dayComparisonScroll.contentItem.contentY = 0;
                                    }
                                }
                            }
                            Legend {
                                objectName: "dayLegend"
                                Layout.fillWidth: true
                                items: [
                                    {
                                        color: Theme.accentTeal,
                                        text: "Glukoosi",
                                        shape: "line"
                                    },
                                    {
                                        color: Theme.alertRed,
                                        text: "Tavoitealueen ulkopuolella",
                                        shape: "line"
                                    },
                                    {
                                        color: Theme.accentAmber,
                                        text: "HH",
                                        shape: "square"
                                    },
                                    {
                                        color: Theme.accentPurple,
                                        text: "Perusinsuliini",
                                        shape: "line"
                                    },
                                    {
                                        color: Theme.accentBlue,
                                        text: "Pika-/korjausinsuliini",
                                        shape: "line"
                                    }
                                ]
                            }
                            ScrollView {
                                id: dayComparisonScroll
                                objectName: "dayComparisonScroll"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                contentWidth: availableWidth
                                clip: true
                                ColumnLayout {
                                    width: dayComparisonScroll.availableWidth
                                    spacing: 16
                                    Repeater {
                                        model: 4
                                        delegate: ColumnLayout {
                                            id: comparisonDay
                                            required property int index
                                            readonly property var modelData: window.studio.dayComparisonCharts[index] || ({})
                                            objectName: "comparisonDay" + comparisonDay.index
                                            Layout.fillWidth: true
                                            Label {
                                                objectName: comparisonDay.index === 0 ? "selectedDayTitle" : comparisonDay.index === 1 ? "previousDayTitle" : "comparisonDayTitle" + comparisonDay.index
                                                Layout.fillWidth: true
                                                wrapMode: Text.WordWrap
                                                font.bold: true
                                                text: (comparisonDay.index === 0 ? "Valittu päivä" : comparisonDay.index === 1 ? "Edellinen päivä" : comparisonDay.index + " päivää aiemmin") + " · " + comparisonDay.modelData.dateLabel
                                            }
                                            GlucoseChart {
                                                objectName: comparisonDay.index === 0 ? "glucoseChart" : "comparisonChart" + comparisonDay.index
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 320
                                                Layout.minimumHeight: 320
                                                chartData: comparisonDay.modelData
                                                highlightOutsideTarget: true
                                                sourceName: window.studio.connection.source === "nightscout" ? "Nightscout" : "Juggluco"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Popup {
        id: periodPopup
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 20
        ColumnLayout {
            Label {
                text: "Jakson viimeinen päivä"
            }
            TextField {
                id: lastDate
                text: window.studio.analysis.lastDate || ""
                placeholderText: "vvvv-kk-pp"
            }
            Button {
                text: "Näytä jakso"
                onClicked: {
                    window.studio.setAnalysisWindow(window.studio.analysis.days, lastDate.text);
                    periodPopup.close();
                }
            }
        }
    }
    Dialog {
        id: settingsDialog
        objectName: "simpleSettings"
        title: "Asetukset"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width - 40, 780)
        height: Math.min(window.height - 40, 700)
        standardButtons: Dialog.Close
        ScrollView {
            id: settingsScroll
            anchors.fill: parent
            contentWidth: availableWidth
            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 14
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Tee päivittäiset merkinnät valitsemasi lähteen sovelluksessa. Studio lukee tiedot avattaessa ja viiden minuutin välein sovelluksen ollessa auki."
                }
                Disclosure {
                    objectName: "connectionDetails"
                    Layout.fillWidth: true
                    title: "Tietolähde ja yhteys"
                    summary: window.studio.connection.source === "nightscout" ? "Nightscout · HTTPS" : (window.studio.connection.mode === "usb" ? "Juggluco · USB" : "Juggluco · WireGuard")
                    ConnectionEditor {
                        Layout.fillWidth: true
                        backend: window.studio
                    }
                }
                Disclosure {
                    objectName: "labelMappingSettings"
                    Layout.fillWidth: true
                    title: "Jugglucon merkintäluokat"
                    visible: window.studio.connection.source !== "nightscout"
                    summary: "HH- ja insuliinimerkintöjen tunnistus"
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Valitse, mitä kukin Jugglucon merkintänimi tarkoittaa. Insuliinimerkinnälle tarvitaan myös valmisteen nimi."
                    }
                    Repeater {
                        model: window.studio.labels
                        delegate: RowLayout {
                            id: labelRow
                            required property var modelData
                            Layout.fillWidth: true
                            Label {
                                text: labelRow.modelData.label
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            ComboBox {
                                id: kind
                                property var kinds: ["unknown", "carbs", "rapid", "basal", "correction", "other"]
                                model: ["Tuntematon", "Hiilihydraatit", "Pikainsuliini", "Perusinsuliini", "Korjausinsuliini", "Muu"]
                                currentIndex: Math.max(0, kinds.indexOf(labelRow.modelData.kind))
                            }
                            TextField {
                                id: medicine
                                text: labelRow.modelData.medicine || ""
                                placeholderText: "Valmiste"
                                Layout.preferredWidth: 150
                            }
                            Button {
                                text: "Tallenna"
                                onClicked: window.studio.mapLabel(labelRow.modelData.label, kind.kinds[kind.currentIndex], medicine.text)
                            }
                        }
                    }
                }
            }
        }
    }
}
