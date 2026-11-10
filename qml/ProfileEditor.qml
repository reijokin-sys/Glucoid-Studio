// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import GlucoidStudio
import "."
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScrollView {
    id: root
    required property Backend studio
    property bool historicalMode: true
    property var draft: ({})
    property int loadedVersion: -1
    property string result: ""
    function load() {
        draft = JSON.parse(JSON.stringify(root.studio.profile));
        loadedVersion = root.studio.profile.version || 0;
    }
    Component.onCompleted: load()
    Connections {
        target: root.studio
        function onChanged() {
            if ((root.studio.profile.version || 0) !== root.loadedVersion)
                root.load();
        }
    }
    function get(key) {
        let o = draft;
        for (let k of key.split("."))
            o = o ? o[k] : null;
        return o === null || o === undefined ? "" : String(o);
    }
    function set(key, value, numeric) {
        let p = JSON.parse(JSON.stringify(draft)), o = p, keys = key.split(".");
        for (let i = 0; i < keys.length - 1; i++) {
            if (!o[keys[i]])
                o[keys[i]] = {};
            o = o[keys[i]];
        }
        let v = value;
        if (numeric) {
            let s = String(value).trim().replace(",", ".");
            v = s.length ? Number(s) : null;
            if (!Number.isFinite(v))
                v = null;
        }
        o[keys[keys.length - 1]] = v;
        if (key === "rapidMedicine")
            p.actionModel.medicine = value;
        draft = p;
    }
    function save(approve) {
        let r = historicalMode ? root.studio.saveAnalysisProfile(draft, approve) : root.studio.saveProfile(draft, approve);
        result = r.status === "saved" ? "Profiiliversio " + r.version + " tallennettu." : (r.reasons || []).join("\n");
    }
    function activate() {
        let r = root.studio.activateObservationProfile(draft);
        result = r.status === "active" ? qsTranslate("Studio", "Profiili ") + r.version + qsTranslate("Studio", " on käytössä seurannassa.") : (r.reasons || []).join("\n");
        if (r.status === "active")
            load();
    }
    function approveResponse() {
        let r = root.studio.saveResponseProfile(draft, true);
        result = r.status === "saved" ? "Profiiliversio " + r.version + qsTranslate("Studio", " hyväksytty vastearvioon. Annos- ja muutosehdotusten hyväksyntä on erillinen.") : (r.reasons || []).join("\n");
    }
    contentWidth: availableWidth
    ColumnLayout {
        width: root.availableWidth
        spacing: 16
        Kirigami.Heading {
            text: qsTranslate("Studio", "Hoitoprofiili")
            level: 2
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.studio.observationProfile.version ? qsTranslate("Studio", "Seurannassa profiili ") + root.studio.observationProfile.version + (root.studio.observationProfile.version === root.draft.version ? qsTranslate("Studio", " · tämä versio") : qsTranslate("Studio", " · muokkaat eri versiota")) : qsTranslate("Studio", "Täydennä tiedot ja ota hoito seurantaan.")
        }
        TextField {
            visible: root.historicalMode
            Layout.fillWidth: true
            placeholderText: qsTranslate("Studio", "Voimassa alkaen: vvvv-kk-pp tt:mm")
            text: root.get("effectiveFromText")
            onEditingFinished: root.set("effectiveFromText", text, false)
        }
        ReviewCheckBox {
            visible: root.historicalMode
            text: qsTranslate("Studio", "Asetukset olivat voimassa tästä ajasta alkaen")
            checked: !!root.draft.historicalConfirmed
            onToggled: root.set("historicalConfirmed", checked, false)
        }
        Disclosure {
            Layout.fillWidth: true
            title: qsTranslate("Studio", "Profiilin käyttö ja käsitteet")
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTranslate("Studio", "Seuranta käyttää ilmoitettua hoitoa. Vastearvio ja lääkityslaskenta hyväksytään erikseen.")
            }
            Label {
                objectName: "responseProfileApprovalStatus"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: (root.draft.responseApproved ? qsTranslate("Studio", "Vastearvion hyväksyntä on voimassa. ") : "") + qsTranslate("Studio", "Vastearviossa nykyinen korjauskerroin ja annoslaskurin yksittäinen tavoite voivat puuttua. Muut rajat ja ammattilaisen tarkistus tarvitaan.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTranslate("Studio", "Korjauskerroin: yhden insuliiniyksikön aiheuttama glukoosin lasku (mmol/l/U). HH-suhde: yhden yksikön kattama hiilihydraattimäärä (g/U).")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTranslate("Studio", "Korjausten tavoitealue on erillinen tieto annoslaskurin yksittäisestä tavoitteesta. Tyhjää arvoa ei korvata oletuksella.")
            }
        }
        Repeater {
            model: [
                {
                    title: qsTranslate("Studio", "Insuliinit"),
                    open: true,
                    fields: [["rapidMedicine", qsTranslate("Studio", "Pikainsuliini"), false], ["basalMedicine", qsTranslate("Studio", "Perusinsuliini"), false], ["basalDose", qsTranslate("Studio", "Perusinsuliinin annos (U)"), true], ["basalTime", qsTranslate("Studio", "Perusinsuliinin aika (HH:mm)"), false]]
                },
                {
                    title: qsTranslate("Studio", "Tavoitteet"),
                    fields: [["targetLow", qsTranslate("Studio", "Tavoitealueen alaraja (mmol/l)"), true], ["targetHigh", qsTranslate("Studio", "Tavoitealueen yläraja (mmol/l)"), true], ["correctionTargetRange.low", qsTranslate("Studio", "Korjausten tavoite: alaraja (mmol/l)"), true], ["correctionTargetRange.high", qsTranslate("Studio", "Korjausten tavoite: yläraja (mmol/l)"), true], ["target", qsTranslate("Studio", "Annoslaskurin tavoite (mmol/l)"), true], ["hypo", qsTranslate("Studio", "Laskennan estävä matalan raja (mmol/l)"), true]]
                },
                {
                    title: qsTranslate("Studio", "Vaikutusmalli ja tarkistaja"),
                    fields: [["actionModel.name", qsTranslate("Studio", "Mallin nimi ja versio"), false], ["actionModel.reference", qsTranslate("Studio", "Mallin lähde ja soveltuvuus"), false], ["actionModel.durationMinutes", qsTranslate("Studio", "Pikainsuliinin vaikutusaika (min)"), true], ["basalReportedAction.durationMinutes", qsTranslate("Studio", "Perusinsuliinin vaikutusaika (min, arvio)"), true], ["basalStableDays", qsTranslate("Studio", "Perusannoksen tasaantumisaika (vrk)"), true], ["timezone", qsTranslate("Studio", "Aikavyöhyke"), false], ["reviewer", qsTranslate("Studio", "Profiilin tarkistanut ammattilainen"), false], ["maxGlucoseAgeMinutes", qsTranslate("Studio", "Mittauksen enimmäisikä (min)"), true], ["maxBolus", qsTranslate("Studio", "Enimmäisannos (U)"), true], ["doseIncrement", "Annosporras (U)", true]]
                },
                {
                    title: qsTranslate("Studio", "Kelpuutettavan aineiston rajat"),
                    fields: [["rules.minimumEpisodes", qsTranslate("Studio", "Jaksojen vähimmäismäärä"), true], ["rules.minimumDays", qsTranslate("Studio", "Erillisten päivien vähimmäismäärä"), true], ["rules.minCoverage", qsTranslate("Studio", "Datan vähimmäiskattavuus (0–1)"), true], ["rules.maxGapMinutes", qsTranslate("Studio", "Mittausvälin enimmäispituus (min)"), true], ["rules.consensus", qsTranslate("Studio", "Yhdensuuntaisten havaintojen osuus (>0,5–1)"), true], ["rules.minReviewDays", qsTranslate("Studio", "Muutosten vähimmäisarviointiväli (vrk)"), true], ["rules.mealWindowMinutes", qsTranslate("Studio", "Aterian seurantajakso (min)"), true], ["rules.correctionWindowMinutes", qsTranslate("Studio", "Korjauksen seurantajakso (min)"), true], ["rules.basalWindowMinutes", qsTranslate("Studio", "Paaston/yön seurantajakso (min)"), true], ["rules.mealDeviation", qsTranslate("Studio", "Aterian poikkeamaraja (mmol/l)"), true], ["rules.correctionDeviation", qsTranslate("Studio", "Korjauksen poikkeamaraja (mmol/l)"), true], ["rules.basalDeviation", qsTranslate("Studio", "Perusinsuliinijakson poikkeamaraja (mmol/l)"), true]]
                },
                {
                    title: qsTranslate("Studio", "Muutosehdotusten rajat"),
                    fields: [["rules.stepPercent", qsTranslate("Studio", "Yhden muutosehdotuksen askel (%)"), true], ["rules.maxChangePercent", qsTranslate("Studio", "Suurin sallittu muutos (%)"), true], ["rules.minimumIcr", qsTranslate("Studio", "HH-suhteen alaraja (g/U)"), true], ["rules.maximumIcr", qsTranslate("Studio", "HH-suhteen yläraja (g/U)"), true], ["rules.minimumIsf", qsTranslate("Studio", "Korjauskertoimen alaraja (mmol/l/U)"), true], ["rules.maximumIsf", qsTranslate("Studio", "Korjauskertoimen yläraja (mmol/l/U)"), true], ["rules.minimumBasal", qsTranslate("Studio", "Perusinsuliiniannoksen alaraja (U)"), true], ["rules.maximumBasal", qsTranslate("Studio", "Perusinsuliiniannoksen yläraja (U)"), true]]
                }
            ]
            delegate: Disclosure {
                id: fieldGroup
                required property var modelData
                Layout.fillWidth: true
                title: fieldGroup.modelData.title
                expanded: !!fieldGroup.modelData.open
                Repeater {
                    model: fieldGroup.modelData.fields
                    delegate: RowLayout {
                        id: fieldRow
                        required property var modelData
                        visible: !root.historicalMode || !["maxGlucoseAgeMinutes", "maxBolus", "doseIncrement"].includes(fieldRow.modelData[0])
                        Layout.fillWidth: true
                        Label {
                            text: fieldRow.modelData[1]
                            Layout.preferredWidth: Math.min(340, root.availableWidth * 0.48)
                            wrapMode: Text.WordWrap
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: root.get(fieldRow.modelData[0])
                            placeholderText: qsTranslate("Studio", "Määritettävä")
                            onEditingFinished: root.set(fieldRow.modelData[0], text, fieldRow.modelData[2])
                        }
                    }
                }
            }
        }
        Label {
            visible: !root.historicalMode
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTranslate("Studio", "Vaikutuskäyrä: yksi piste riville muodossa minuutti; jäljellä oleva osuus. Alku on 0;1 ja loppu määritetty vaikutusaika;0. Käytä valmisteelle hyväksyttyä lähteistettyä mallia.")
        }
        TextArea {
            visible: !root.historicalMode
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            placeholderText: qsTranslate("Studio", "minuutti;osuus")
            text: ((root.draft.actionModel || {}).curve || []).map(p => p.minute + ";" + p.fraction).join("\n")
            onActiveFocusChanged: if (!activeFocus) {
                let curve = [];
                for (let line of text.split("\n")) {
                    if (!line.trim())
                        continue;
                    let parts = line.split(";");
                    curve.push({
                        minute: parts.length === 2 ? Number(parts[0].trim().replace(",", ".")) : null,
                        fraction: parts.length === 2 ? Number(parts[1].trim().replace(",", ".")) : null
                    });
                }
                let p = JSON.parse(JSON.stringify(root.draft));
                p.actionModel.curve = curve;
                root.draft = p;
            }
        }
        RowLayout {
            visible: !root.historicalMode
            Label {
                text: qsTranslate("Studio", "Annoslaskelman pyöristys")
            }
            ComboBox {
                model: [qsTranslate("Studio", "Alaspäin annosportaaseen"), qsTranslate("Studio", "Lähimpään annosportaaseen")]
                currentIndex: root.draft.rounding === "nearest" ? 1 : 0
                onActivated: root.set("rounding", currentIndex === 1 ? "nearest" : "down", false)
            }
        }
        Kirigami.Heading {
            text: qsTranslate("Studio", "Vuorokauden hoitojaksot")
            level: 3
        }
        Label {
            text: qsTranslate("Studio", "Jaksot alkavat aikajärjestyksessä, ensimmäinen klo 00:00. HH-suhde on grammoja per yksikkö.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Repeater {
            model: root.draft.slots || []
            delegate: RowLayout {
                id: slotRow
                required property int index
                required property var modelData
                function update(key, value) {
                    let p = JSON.parse(JSON.stringify(root.draft));
                    p.slots[index][key] = value;
                    root.draft = p;
                }
                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTranslate("Studio", "Jakson nimi")
                    text: slotRow.modelData.name
                    onEditingFinished: slotRow.update("name", text)
                }
                TextField {
                    Layout.preferredWidth: 100
                    placeholderText: "HH:mm"
                    text: String(Math.floor(slotRow.modelData.startMinute / 60)).padStart(2, "0") + ":" + String(slotRow.modelData.startMinute % 60).padStart(2, "0")
                    onEditingFinished: {
                        let p = text.split(":");
                        slotRow.update("startMinute", p.length === 2 ? Number(p[0]) * 60 + Number(p[1]) : null);
                    }
                }
                TextField {
                    Layout.preferredWidth: 130
                    placeholderText: qsTranslate("Studio", "HH-suhde g/U")
                    text: slotRow.modelData.icr === null ? "" : slotRow.modelData.icr
                    onEditingFinished: slotRow.update("icr", text.trim().length ? Number(text.replace(",", ".")) : null)
                }
                TextField {
                    Layout.preferredWidth: 155
                    placeholderText: qsTranslate("Studio", "Korjaus mmol/l/U")
                    text: slotRow.modelData.isf === null ? "" : slotRow.modelData.isf
                    onEditingFinished: slotRow.update("isf", text.trim().length ? Number(text.replace(",", ".")) : null)
                }
                Button {
                    text: qsTranslate("Studio", "Poista jakso")
                    enabled: slotRow.index > 0
                    onClicked: {
                        let p = JSON.parse(JSON.stringify(root.draft));
                        p.slots.splice(slotRow.index, 1);
                        root.draft = p;
                    }
                }
            }
        }
        Button {
            text: qsTranslate("Studio", "Lisää hoitojakso")
            onClicked: {
                let p = JSON.parse(JSON.stringify(root.draft));
                p.slots.push({
                    name: "",
                    startMinute: null,
                    icr: null,
                    isf: null
                });
                root.draft = p;
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: qsTranslate("Studio", "Tallenna luonnos")
                onClicked: root.save(false)
            }
            Button {
                text: qsTranslate("Studio", "Ota seurantaan")
                onClicked: root.activate()
            }
        }
        Disclosure {
            objectName: "profileApprovals"
            Layout.fillWidth: true
            title: qsTranslate("Studio", "Hyväksy laskentaan")
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTranslate("Studio", "Vastearvio käyttää tarkistettuja havaintoja. Annos- ja muutosehdotukset tarvitsevat täydelliset lääkeasetukset.")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    objectName: "approveResponseProfile"
                    visible: root.historicalMode
                    text: qsTranslate("Studio", "Hyväksy vastearvioon")
                    onClicked: root.approveResponse()
                }
                Button {
                    text: qsTranslate("Studio", "Hyväksy lääkityslaskentaan")
                    onClicked: root.save(true)
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.result
        }
    }
}
