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
    width: 1240
    height: 860
    minimumWidth: 960
    minimumHeight: 680
    visible: true
    readonly property string applicationTitle: "Glucoid Studio" + (typeof window.appVersion === "string" && window.appVersion.length ? " " + window.appVersion : "")
    title: applicationTitle + (window.demoMode ? " — esimerkkiaineisto" : "")
    property var selectedIds: ({})
    property var calculation: ({})
    property var assessment: ({})
    property string episodeResult: ""
    Connections {
        target: window.studio
        function onChanged() {
            if (window.calculation.status === "research" && (window.calculation.sourceRevision !== window.studio.dataRevision || window.calculation.profileVersion !== window.studio.profile.version))
                window.calculation = ({
                        status: "blocked",
                        reasons: [qsTranslate("Studio", "Aineisto tai hoitoprofiili muuttui. Laske uudelleen.")]
                    });
            if (window.assessment.status === "research" && (window.assessment.sourceRevision !== window.studio.dataRevision || window.assessment.profileVersion !== window.studio.profile.version))
                window.assessment = ({
                        status: "blocked",
                        reasons: [qsTranslate("Studio", "Aineisto tai hoitoprofiili muuttui. Arvioi uudelleen.")]
                    });
        }
    }
    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: if (window.calculation.status === "research" && Date.now() > window.calculation.validUntil)
            window.calculation = ({
                    status: "blocked",
                    reasons: [qsTranslate("Studio", "Mittauksen kelpoisuusaika on päättynyt. Laske uudelleen tuoreella mittauksella.")]
                })
    }
    function showView(index) {
        if (deleteDialog.visible)
            deleteDialog.reject();
        tabs.currentIndex = index === 6 ? 5 : index;
        settingsTabs.currentIndex = index === 6 ? 1 : 0;
        if (index === 1 && window.demoMode && window.studio.diary.length) {
            deleteDialog.entry = window.studio.diary[0];
            deleteDialog.open();
        }
    }
    function decimal(text) {
        let s = String(text).trim().replace(",", ".");
        return s.length ? Number(s) : NaN;
    }
    function n(value, digits) {
        return typeof value === "number" && Number.isFinite(value) ? value.toLocaleString(Qt.locale(window.studio.language === "fi" ? "fi_FI" : "en_GB"), "f", digits === undefined ? 1 : digits) : "—";
    }
    function stamp() {
        return new Date().toLocaleString(Qt.locale("sv_SE"), "yyyy-MM-dd HH:mm");
    }
    function select(id, checked) {
        let m = Object.assign({}, selectedIds);
        if (checked)
            m[id] = true;
        else
            delete m[id];
        selectedIds = m;
    }
    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 80, 520)
        title: qsTranslate("Studio", "Poista merkintä päiväkirjasta?")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var entry: ({})
        contentItem: Label {
            text: (deleteDialog.entry.timeText || "") + " · " + (deleteDialog.entry.label || "") + qsTranslate("Studio", "\n\nMerkinnän voi palauttaa kohdasta Poistetut merkinnät. Jugglucon alkuperäinen tieto säilyy. Poisto mitätöi merkintään perustuvat arviot. Poistettu annos- tai HH-tieto voi estää laskennan vaikutusikkunan aikana.")
            wrapMode: Text.WordWrap
        }
        onAccepted: if (window.studio.removeAmount(entry.id))
            window.select(entry.id, false)
    }
    function resultText(result) {
        if (!result.status)
            return "";
        if (result.status === "blocked")
            return qsTranslate("Studio", "Laskenta estetty\n") + (result.reasons || []).join("\n");
        if (result.status === "saved")
            return "Tallennettu.";
        if (result.kind)
            return "Nykyinen: " + n(result.current, 2) + "\nEhdotus: " + n(result.candidate, 2) + " (" + n(result.changePercent, 1) + " %)\n" + result.rule;
        return "Tutkimuslaskelma: " + n(result.units, 2) + " U\nHH-osuus: " + n(result.meal, 2) + " U\nGlukoosikorjaus: " + n(result.correction, 2) + qsTranslate("Studio", " U\nAktiivinen pikainsuliini: ") + n(result.iob, 2) + qsTranslate("Studio", " U\nEnnen pyöristystä: ") + n(result.unrounded, 2) + " U\n" + result.message;
    }
    pageStack.initialPage: Kirigami.Page {
        title: window.applicationTitle
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Kirigami.Heading {
                    text: window.studio.latestText
                    level: 2
                    Layout.fillWidth: true
                }
                BusyIndicator {
                    running: window.studio.busy
                    visible: running
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                }
                Button {
                    text: qsTranslate("Studio", "Hae tiedot")
                    enabled: !window.studio.busy && !window.demoMode
                    onClicked: window.studio.sync()
                }
                Button {
                    text: qsTranslate("Studio", "Varmuuskopio")
                    onClicked: window.studio.backup()
                }
            }
            Label {
                text: qsTranslate("Studio", "Tutkimustila · lääkityslaskentaa ei ole kliinisesti validoitu")
                color: Kirigami.Theme.neutralTextColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton {
                    text: qsTranslate("Studio", "Vuorokausi")
                }
                TabButton {
                    text: qsTranslate("Studio", "Päiväkirja")
                }
                TabButton {
                    text: qsTranslate("Studio", "Yhteenveto")
                }
                TabButton {
                    text: qsTranslate("Studio", "Hoidon arviointi")
                }
                TabButton {
                    text: qsTranslate("Studio", "Aterialaskuri")
                }
                TabButton {
                    text: qsTranslate("Studio", "Asetukset")
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabs.currentIndex
                ColumnLayout {
                    RowLayout {
                        Button {
                            text: qsTranslate("Studio", "Viimeiset 24 h")
                            onClicked: window.studio.setWindow("", 1)
                        }
                        Label {
                            text: qsTranslate("Studio", "Päivä")
                        }
                        TextField {
                            id: day
                            placeholderText: qsTranslate("Studio", "vvvv-kk-pp")
                            text: new Date().toLocaleString(Qt.locale("sv_SE"), "yyyy-MM-dd")
                            Layout.preferredWidth: 150
                        }
                        Button {
                            text: qsTranslate("Studio", "Näytä päivä")
                            onClicked: window.studio.setWindow(day.text, 1)
                        }
                        Button {
                            text: qsTranslate("Studio", "7 vrk")
                            onClicked: window.studio.setWindow("", 7)
                        }
                        Button {
                            text: qsTranslate("Studio", "14 vrk")
                            onClicked: window.studio.setWindow("", 14)
                        }
                        Button {
                            text: qsTranslate("Studio", "30 vrk")
                            onClicked: window.studio.setWindow("", 30)
                        }
                    }
                    GlucoseChart {
                                language: window.studio.language
                        objectName: "glucoseChart"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        chartData: window.studio.chart
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTranslate("Studio", "Glukoosi mmol/l · kultaiset rajat: määritetty tavoitealue · sininen: pikainsuliini · violetti: perusinsuliini · kulta: HH. Vie osoitin merkinnän päälle nähdäksesi tiedot. Aukot ovat puuttuvaa dataa.")
                    }
                }
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        Kirigami.Heading {
                            text: qsTranslate("Studio", "Toteutuneet merkinnät")
                            level: 2
                        }
                        Label {
                            text: qsTranslate("Studio", "Lista käyttää Vuorokausi-näkymän aikaväliä. Valitse arviointiin kuuluvat ateria- ja insuliinimerkinnät tästä listasta.")
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            TextField {
                                id: localDate
                                placeholderText: qsTranslate("Studio", "vvvv-kk-pp tt:mm")
                                text: window.stamp()
                                Layout.preferredWidth: 190
                            }
                            ComboBox {
                                id: localKind
                                model: ["HH (g)", "Pikainsuliini (U)", "Perusinsuliini (U)", qsTranslate("Studio", "Muu")]
                            }
                            TextField {
                                id: localValue
                                placeholderText: qsTranslate("Studio", "Määrä")
                                Layout.preferredWidth: 90
                            }
                            TextField {
                                id: localMedicine
                                placeholderText: qsTranslate("Studio", "Insuliinivalmiste")
                                Layout.fillWidth: true
                            }
                            ComboBox {
                                id: localPurpose
                                model: ["ateria", "korjaus", "perus", "muu"]
                            }
                            Button {
                                text: qsTranslate("Studio", "Kirjaa toteutunut")
                                onClicked: window.studio.addLocal(localDate.text, window.decimal(localValue.text), ["carbs", "rapid", "basal", "other"][localKind.currentIndex], localMedicine.text, localPurpose.currentText)
                            }
                        }
                        Switch {
                            id: showDeleted
                            text: qsTranslate("Studio", "Näytä poistetut merkinnät (") + window.studio.deletedDiary.length + ")"
                        }
                        ColumnLayout {
                            visible: showDeleted.checked
                            Layout.fillWidth: true
                            Repeater {
                                model: window.studio.deletedDiary
                                delegate: RowLayout {
                                    id: deletedRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: deletedRow.modelData.timeText + " · " + deletedRow.modelData.label + " · " + window.n(deletedRow.modelData.value, 2) + " · poistettu"
                                    }
                                    Button {
                                        text: qsTranslate("Studio", "Palauta")
                                        onClicked: window.studio.restoreAmount(deletedRow.modelData.id)
                                    }
                                }
                            }
                            Label {
                                visible: window.studio.deletedDiary.length === 0
                                text: qsTranslate("Studio", "Tällä aikavälillä ei ole poistettuja merkintöjä.")
                            }
                        }
                        Repeater {
                            model: window.studio.diary
                            delegate: Frame {
                                id: diaryEntry
                                required property var modelData
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    RowLayout {
                                        CheckBox {
                                            checked: !!window.selectedIds[diaryEntry.modelData.id]
                                            onClicked: window.select(diaryEntry.modelData.id, checked)
                                            Accessible.name: qsTranslate("Studio", "Valitse arviointijaksoon")
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: diaryEntry.modelData.timeText + " · " + diaryEntry.modelData.label + " · " + window.n(diaryEntry.modelData.value, 2) + " " + (diaryEntry.modelData.kind === "carbs" ? "g" : diaryEntry.modelData.kind === "rapid" || diaryEntry.modelData.kind === "basal" ? "U" : "") + " · " + (diaryEntry.modelData.medicine || diaryEntry.modelData.kind) + " · " + (diaryEntry.modelData.local ? "paikallinen" : "Juggluco")
                                        }
                                        Label {
                                            text: diaryEntry.modelData.purpose || ""
                                        }
                                        Button {
                                            text: qsTranslate("Studio", "Poista")
                                            onClicked: {
                                                deleteDialog.entry = diaryEntry.modelData;
                                                deleteDialog.open();
                                            }
                                        }
                                    }
                                    RowLayout {
                                        ComboBox {
                                            id: purpose
                                            model: ["ateria", "korjaus", "perus", "muu"]
                                            currentIndex: Math.max(0, model.indexOf(diaryEntry.modelData.purpose))
                                        }
                                        TextField {
                                            id: note
                                            Layout.fillWidth: true
                                            placeholderText: qsTranslate("Studio", "Paikallinen täydennys tai huomio")
                                            text: diaryEntry.modelData.annotation.note || ""
                                        }
                                        Button {
                                            text: qsTranslate("Studio", "Tallenna täydennys")
                                            onClicked: window.studio.annotate(diaryEntry.modelData.id, purpose.currentText, note.text)
                                        }
                                    }
                                }
                            }
                        }
                        Label {
                            visible: window.studio.diary.length === 0
                            text: qsTranslate("Studio", "Valitulla aikavälillä ei ole merkintöjä.")
                        }
                    }
                }
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        Kirigami.Heading {
                            text: qsTranslate("Studio", "Glukoosiseurannan yhteenveto")
                            level: 2
                        }
                        Repeater {
                            model: window.studio.summaries
                            delegate: Frame {
                                id: summaryCard
                                required property var modelData
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Kirigami.Heading {
                                        text: summaryCard.modelData.days + " vuorokautta"
                                        level: 3
                                    }
                                    Label {
                                        text: qsTranslate("Studio", "Kattavuus ") + window.n(summaryCard.modelData.coverage * 100) + " % · keskiarvo " + window.n(summaryCard.modelData.mean) + qsTranslate("Studio", " mmol/l · vaihtelukerroin ") + window.n(summaryCard.modelData.cv) + " %"
                                    }
                                    Label {
                                        text: summaryCard.modelData.targetsSet ? "Tavoitealueella " + window.n(summaryCard.modelData.inRange * 100) + " % · alapuolella " + window.n(summaryCard.modelData.below * 100) + qsTranslate("Studio", " % · yläpuolella ") + window.n(summaryCard.modelData.above * 100) + " %" : qsTranslate("Studio", "Määritä henkilökohtainen tavoitealue hoitoprofiilissa.")
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTranslate("Studio", "Osuudet lasketaan katetusta ajasta. Puuttuva aika raportoidaan erikseen kattavuudessa. Yhteenveto ei yksin osoita, mikä aiheutti glukoosimuutokset.")
                        }
                        RowLayout {
                            Repeater {
                                model: [7, 14, 30]
                                delegate: Button {
                                    required property int modelData
                                    text: modelData + qsTranslate("Studio", " päivän käyrä")
                                    onClicked: {
                                        window.studio.setWindow("", modelData);
                                        tabs.currentIndex = 0;
                                    }
                                }
                            }
                        }
                    }
                }
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 12
                        Kirigami.Heading {
                            text: qsTranslate("Studio", "Tarkista vertailukelpoinen jakso")
                            level: 2
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTranslate("Studio", "Valitse ensin Päiväkirjasta jakson toteutuneet merkinnät. Sovellus laskee mittauksista lähtö- ja loppuarvon, kattavuuden sekä poikkeaman. Hyväksyntä koskee vain nykyisen hoitoprofiilin aikana kerättyä aineistoa.")
                        }
                        RowLayout {
                            ComboBox {
                                id: episodeKind
                                model: [qsTranslate("Studio", "HH-suhde / ateria"), qsTranslate("Studio", "Korjauskerroin"), qsTranslate("Studio", "Perusinsuliini / paasto")]
                            }
                            TextField {
                                id: episodeDate
                                Layout.preferredWidth: 200
                                placeholderText: qsTranslate("Studio", "Alku: vvvv-kk-pp tt:mm")
                            }
                            Label {
                                text: Object.keys(window.selectedIds).length + qsTranslate("Studio", " merkintää valittu")
                            }
                            Button {
                                text: qsTranslate("Studio", "Tyhjennä valinta")
                                onClicked: window.selectedIds = ({})
                            }
                        }
                        CheckBox {
                            id: reviewed
                            text: qsTranslate("Studio", "Olen tarkistanut aterian/korjauksen/paastojakson ja siihen liitetyt annokset")
                        }
                        CheckBox {
                            id: complete
                            text: qsTranslate("Studio", "HH- ja annostiedot ovat kattavat koko arviointi- ja vaikutusjaksolta")
                        }
                        CheckBox {
                            id: contextClear
                            text: qsTranslate("Studio", "Ei tunnettua sekoittavaa liikuntaa, sairautta, alkoholia tai hypoglykemian korjaushiilihydraatteja")
                        }
                        CheckBox {
                            id: stable
                            visible: episodeKind.currentIndex === 2
                            text: qsTranslate("Studio", "Perusinsuliinin annostus on ollut vakaa profiilin tasaantumisjakson ajan")
                        }
                        CheckBox {
                            id: basalComplete
                            visible: episodeKind.currentIndex === 2
                            text: qsTranslate("Studio", "Kaikki tasaantumisjakson perusinsuliiniannokset on kirjattu ja toteutuminen tarkistettu")
                        }
                        Button {
                            text: qsTranslate("Studio", "Tallenna tarkistettu jakso")
                            onClicked: {
                                let r = window.studio.saveEpisode({
                                    kind: ["icr", "isf", "basal"][episodeKind.currentIndex],
                                    startText: episodeDate.text,
                                    linkedIds: Object.keys(window.selectedIds),
                                    reviewed: reviewed.checked,
                                    complete: complete.checked,
                                    contextClear: contextClear.checked,
                                    basalStable: stable.checked,
                                    basalHistoryComplete: basalComplete.checked
                                });
                                window.episodeResult = window.resultText(r);
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: window.episodeResult
                        }
                        Kirigami.Heading {
                            text: qsTranslate("Studio", "Laske muutosehdotus")
                            level: 2
                        }
                        RowLayout {
                            ComboBox {
                                id: assessKind
                                model: ["HH-suhde", qsTranslate("Studio", "Korjauskerroin"), qsTranslate("Studio", "Perusinsuliini")]
                            }
                            ComboBox {
                                id: assessSlot
                                model: window.studio.slots
                                textRole: "name"
                                enabled: assessKind.currentIndex !== 2
                            }
                            Button {
                                text: qsTranslate("Studio", "Arvioi historiasta")
                                onClicked: window.assessment = window.studio.assess(["icr", "isf", "basal"][assessKind.currentIndex], assessSlot.currentText)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: window.resultText(window.assessment)
                            font.pixelSize: 16
                        }
                        Label {
                            visible: !!window.assessment.status
                            text: qsTranslate("Studio", "Mukana ") + (window.assessment.included || []).length + qsTranslate("Studio", " jaksoa · pois jätetty ") + (window.assessment.excluded || []).length
                        }
                        Repeater {
                            model: window.assessment.included || []
                            delegate: Label {
                                required property var modelData
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTranslate("Studio", "Mukana: ") + modelData.startText + qsTranslate("Studio", " · lähtö ") + window.n(modelData.firstGlucose) + " → loppu " + window.n(modelData.lastGlucose) + qsTranslate("Studio", " mmol/l · poikkeama ") + window.n(modelData.deviation) + " · kattavuus " + window.n(modelData.coverage * 100) + " %"
                            }
                        }
                        Repeater {
                            model: window.assessment.excluded || []
                            delegate: Label {
                                required property var modelData
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTranslate("Studio", "Pois jätetty: ") + (modelData.reasons || []).join(" · ")
                            }
                        }
                        Button {
                            text: qsTranslate("Studio", "Hyväksy tämä yksi muutos uuteen tutkimusprofiiliin")
                            enabled: window.assessment.status === "research"
                            onClicked: {
                                window.episodeResult = window.resultText(window.studio.acceptRecommendation());
                                window.assessment = ({});
                            }
                        }
                        Kirigami.Heading {
                            text: qsTranslate("Studio", "Tallennetut jaksot")
                            level: 3
                        }
                        Repeater {
                            model: window.studio.episodes
                            delegate: Label {
                                required property var modelData
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: modelData.startText + " · " + modelData.kind + " · " + modelData.slot + " · profiili " + modelData.profileVersion + (modelData.stale ? qsTranslate("Studio", " · lähdetieto muuttunut, tarkistettava uudelleen") : "")
                            }
                        }
                    }
                }
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 12
                        Kirigami.Heading {
                            text: qsTranslate("Studio", "Ateria-annoslaskuri")
                            level: 2
                        }
                        Label {
                            text: qsTranslate("Studio", "Käyttää viimeisintä mittausta ja hyväksytyn profiilin tämän hetken HH-suhdetta, korjauskerrointa ja vaikutusmallia.")
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Label {
                                text: qsTranslate("Studio", "Aterian hiilihydraatit (g)")
                            }
                            TextField {
                                id: bolusCarbs
                                placeholderText: "g"
                                Layout.preferredWidth: 180
                                onTextChanged: window.calculation = ({})
                            }
                        }
                        CheckBox {
                            id: measurement
                            text: qsTranslate("Studio", "Olen varmistanut nykyisen glukoosimittauksen luotettavuuden")
                            onToggled: window.calculation = ({})
                        }
                        CheckBox {
                            id: history
                            text: qsTranslate("Studio", "Kaikki vaikutusikkunan toteutuneet insuliiniannokset on kirjattu")
                            onToggled: window.calculation = ({})
                        }
                        CheckBox {
                            id: carbsComplete
                            text: qsTranslate("Studio", "Aterian HH-määrä ja ateriatiedot ovat kattavat")
                            onToggled: window.calculation = ({})
                        }
                        CheckBox {
                            id: context
                            text: qsTranslate("Studio", "Olen arvioinut tilanteen soveltuvuuden (liikunta, sairaus ja muut poikkeustilanteet)")
                            onToggled: window.calculation = ({})
                        }
                        Button {
                            text: qsTranslate("Studio", "Laske tutkimuslaskelma")
                            onClicked: window.calculation = window.studio.bolus({
                                carbs: window.decimal(bolusCarbs.text),
                                measurementConfirmed: measurement.checked,
                                historyComplete: history.checked,
                                carbsComplete: carbsComplete.checked,
                                contextReviewed: context.checked
                            })
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: window.resultText(window.calculation)
                            font.pixelSize: 18
                        }
                        Label {
                            visible: !!window.calculation.time
                            text: qsTranslate("Studio", "Laskettu ") + new Date(window.calculation.time).toLocaleString(Qt.locale(window.studio.language === "fi" ? "fi_FI" : "en_GB")) + " · hoitoprofiili " + window.calculation.profileVersion
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTranslate("Studio", "Laskelma ei ole annoskirjaus. Toteutunut pistos kirjataan Päiväkirjassa tai Jugglucossa. Mittauksen, merkintöjen tai profiilin muuttuessa tarvitaan uusi laskelma.")
                        }
                    }
                }
                ColumnLayout {
                    TabBar {
                        id: settingsTabs
                        Layout.fillWidth: true
                        TabButton {
                            text: qsTranslate("Studio", "Yhteys ja merkintäluokat")
                        }
                        TabButton {
                            text: qsTranslate("Studio", "Hoitoprofiili")
                        }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: settingsTabs.currentIndex
                        ScrollView {
                            contentWidth: availableWidth
                            ColumnLayout {
                                LanguageSelector { studio: window.studio }
                                width: parent.width
                                spacing: 12
                                Kirigami.Heading {
                                    text: qsTranslate("Studio", "Yhteys Jugglucoon")
                                    level: 2
                                }
                                Button {
                                    text: qsTranslate("Studio", "Tuo Glucoidin yhteys ja tunnus avainrenkaaseen")
                                    enabled: !window.demoMode
                                    onClicked: window.studio.importGlucoid()
                                }
                                RowLayout {
                                    Label {
                                        text: qsTranslate("Studio", "Yhteystapa")
                                    }
                                    ComboBox {
                                        id: connectionMode
                                        model: ["USB / ADB", qsTranslate("Studio", "Verkko / paikallinen palvelin")]
                                        currentIndex: window.studio.connection.mode === "network" ? 1 : 0
                                    }
                                }
                                TextField {
                                    id: address
                                    Layout.fillWidth: true
                                    placeholderText: qsTranslate("Studio", "Palvelimen juuriosoite (verkkomuodossa)")
                                    text: window.studio.connection.address
                                }
                                TextField {
                                    id: serial
                                    Layout.fillWidth: true
                                    placeholderText: qsTranslate("Studio", "ADB-laitetunnus (vain jos useita puhelimia)")
                                    text: window.studio.connection.serial
                                }
                                ComboBox {
                                    id: authMode
                                    model: [qsTranslate("Studio", "API-salaisuus"), "Juggluco-token", "Ei tunnistautumista"]
                                    currentIndex: window.studio.connection.auth === "none" ? 2 : window.studio.connection.auth === "token" ? 1 : 0
                                }
                                TextField {
                                    id: secretField
                                    Layout.fillWidth: true
                                    placeholderText: qsTranslate("Studio", "Uusi tunnus; tyhjä säilyttää avainrenkaan tunnuksen")
                                    echoMode: TextInput.Password
                                }
                                Button {
                                    text: qsTranslate("Studio", "Tallenna yhteys")
                                    enabled: !window.demoMode
                                    onClicked: {
                                        window.studio.saveConnection(connectionMode.currentIndex === 0 ? "usb" : "network", address.text, ["apisecret", "token", "none"][authMode.currentIndex], secretField.text, serial.text);
                                        secretField.clear();
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: qsTranslate("Studio", "USB käyttää puhelimen omaa localhost-palvelinta. Verkkomuoto voi käyttää nykyistä WireGuard-yhteyttä tai paikallista Juggluco-palvelinta. Osoitetta ja tunnusta ei välitetä sovelluksen avauslinkissä.")
                                }
                                Kirigami.Heading {
                                    text: qsTranslate("Studio", "Merkintäluokkien merkitys")
                                    level: 2
                                }
                                Repeater {
                                    model: window.studio.labels
                                    delegate: RowLayout {
                                        id: labelRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Label {
                                            text: labelRow.modelData.label
                                            Layout.preferredWidth: 220
                                        }
                                        ComboBox {
                                            id: mappingKind
                                            model: [qsTranslate("Studio", "Luokittelematon"), qsTranslate("Studio", "Pikainsuliini"), qsTranslate("Studio", "Korjausinsuliini"), qsTranslate("Studio", "Perusinsuliini"), qsTranslate("Studio", "Hiilihydraatit"), qsTranslate("Studio", "Muu")]
                                            currentIndex: Math.max(0, ["unknown", "rapid", "correction", "basal", "carbs", "other"].indexOf(labelRow.modelData.kind))
                                        }
                                        TextField {
                                            id: mappingMedicine
                                            placeholderText: qsTranslate("Studio", "Insuliinivalmiste")
                                            text: labelRow.modelData.medicine
                                            Layout.fillWidth: true
                                        }
                                        Button {
                                            text: qsTranslate("Studio", "Tallenna")
                                            onClicked: window.studio.mapLabel(labelRow.modelData.label, ["unknown", "rapid", "correction", "basal", "carbs", "other"][mappingKind.currentIndex], mappingMedicine.text)
                                        }
                                    }
                                }
                                Label {
                                    text: qsTranslate("Studio", "Tietohakemisto: ") + window.studio.storagePath
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: qsTranslate("Studio", "Terveystiedot ja varmuuskopiot ovat paikallisia ja Gitin ulkopuolella. Automaattinen varmuuskopio tehdään ensimmäisen onnistuneen haun jälkeen päivittäin.")
                                }
                            }
                        }
                        ProfileEditor {
                            historicalMode: false
                            studio: window.studio
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: window.studio.status
                color: Kirigami.Theme.disabledTextColor
            }
        }
    }
}
