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
    signal selectCandidate(var entry)
    signal openProfile
    property var draft: JSON.parse(JSON.stringify(root.studio.responsePolicy))
    property var proposal: ({})
    property string message: ""
    property var missingReasons: (root.studio.response.reasons || []).reduce((all, line) => all.concat(String(line).split("\n")), []).filter(line => line.length > 0)
    function status(s) {
        return ({
                estimated: "Arvioitavissa",
                insufficient: "Aineisto ei riitä",
                conditional: "Ehdollinen arvio",
                evaluated: "Arvioitu",
                regimen_changed: "Hoito muuttunut"
            })[s] || s || "";
    }
    function n(v) {
        return v === undefined || v === null ? "—" : Number(v).toLocaleString(Qt.locale("fi_FI"), "f", 2);
    }
    function set(key, value) {
        let p = JSON.parse(JSON.stringify(draft));
        p[key] = value;
        draft = p;
    }
    function groupSet(index, key, value) {
        let p = JSON.parse(JSON.stringify(draft));
        p.groups[index][key] = value;
        draft = p;
    }
    Connections {
        target: root.studio
        function onChanged() {
            root.proposal = ({});
        }
    }
    contentWidth: availableWidth
    ColumnLayout {
        width: root.availableWidth
        spacing: 16
        Kirigami.Heading {
            text: "Insuliinin vaikutus"
            level: 2
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Korjausvaste ja HH-suhde neljänä vuorokaudenaikana. Valitse 1 tai 2 viikon jakso."
        }
        RowLayout {
            Button {
                text: "Laske vastearvio"
                enabled: !root.studio.busy
                onClicked: {
                    root.proposal = ({});
                    root.studio.analyseResponse([28, 56, 84][reference.currentIndex]);
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: "Vertailu"
            }
            ComboBox {
                id: reference
                model: ["4 viikkoa", "8 viikkoa", "12 viikkoa"]
            }
        }
        Frame {
            visible: root.studio.response.status !== "evaluated"
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.bold: true
                    text: root.studio.response.status === "blocked" ? "Täydennä tiedot ennen arviota" : "Aloita tarkistamalla profiili ja tapahtumat"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.studio.response.status === "blocked" ? root.missingReasons[0] || "Tarkista laskennan edellytykset." : "Arvio tarvitsee hyväksytyn profiilin, laskennan rajat ja tarkistetut jaksot."
                }
                Button {
                    text: "Avaa hoitoprofiili"
                    onClicked: root.openProfile()
                }
                Disclosure {
                    objectName: "responseMissingDetails"
                    Layout.fillWidth: true
                    title: "Kaikki puuttuvat tiedot (" + root.missingReasons.length + ")"
                    visible: root.missingReasons.length > 1
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.missingReasons.join("\n")
                    }
                }
            }
        }
        Label {
            objectName: "responseEstimationScope"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: root.studio.response.status === "evaluated" && root.studio.response.medicationApproved !== true
            text: "Vain vastearvio · annos- ja muutosehdotukset edellyttävät erillistä hyväksyntää."
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: !!root.studio.response.unsafe
            font.bold: true
            text: "Matala glukoosi havaittu. Insuliinia lisäävä muutosehdotus on estetty."
        }
        GridLayout {
            objectName: "responseCards"
            Layout.fillWidth: true
            columns: root.availableWidth >= 1000 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: root.studio.response.groups || []
                delegate: Frame {
                    id: card
                    objectName: "responseGroupCard" + modelData.index
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    contentItem: ColumnLayout {
                        spacing: 12
                        Kirigami.Heading {
                            text: card.modelData.name
                            level: 3
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 20
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: "Korjausvaste"
                                    opacity: 0.75
                                }
                                Label {
                                    text: root.n(card.modelData.isf) + " mmol/l/U"
                                    font.pixelSize: 23
                                    font.bold: true
                                }
                                Label {
                                    text: root.status(card.modelData.isfStatus)
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: "HH-suhteen arvio"
                                    opacity: 0.75
                                }
                                Label {
                                    text: root.n(card.modelData.ratio) + " g/U"
                                    font.pixelSize: 23
                                    font.bold: true
                                }
                                Label {
                                    text: root.status(card.modelData.status)
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.75
                            text: card.modelData.corrections + " korjausta / " + card.modelData.correctionDays + " päivää  ·  " + card.modelData.meals + " ateriaa / " + card.modelData.mealDays + " päivää"
                        }
                        Disclosure {
                            Layout.fillWidth: true
                            title: "Perustelut ja vertailu"
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Korjausvasteen arvioalue: " + root.n(card.modelData.isfLow) + "–" + root.n(card.modelData.isfHigh) + " mmol/l/U"
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: (card.modelData.isfReasons || []).length > 0
                                text: (card.modelData.isfReasons || []).join("\n")
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "HH-suhteen arvioalue: " + root.n(card.modelData.ratioLow) + "–" + root.n(card.modelData.ratioHigh) + " g/U · nykyinen asetus " + root.n(card.modelData.currentRatio) + " g/U"
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: (card.modelData.reasons || []).length > 0
                                text: (card.modelData.reasons || []).join("\n")
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Yhden päivän suurin vaikutus: " + root.n(card.modelData.dayInfluencePercent) + " %"
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: {
                                    let r = ((root.studio.response.comparison || {}).groups || [])[card.modelData.index];
                                    return r && r.status === "compared" ? "Pitkä vertailu: annos/HH " + root.n(r.dosePerCarbChangePercent) + " % (" + root.n(r.dosePerCarbChangeLow) + "–" + root.n(r.dosePerCarbChangeHigh) + ") · korjausvaste " + root.n(r.isfChangePercent) + " % (" + root.n(r.isfChangeLow) + "–" + root.n(r.isfChangeHigh) + ")" : "Pitkän ajan vertailuun ei ole riittävää aineistoa.";
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: {
                                    let r = ((root.studio.response.short || {}).groups || [])[card.modelData.index];
                                    return r ? "Viimeiset 7 päivää: HH-suhde " + root.n(r.ratio) + " g/U · " + r.mealDays + " ateriapäivää · " + root.status(r.status) : "";
                                }
                            }
                            Disclosure {
                                Layout.fillWidth: true
                                title: "Käytetyt tapahtumat"
                                Repeater {
                                    model: card.modelData.observations || []
                                    delegate: Label {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.day + " · " + (modelData.kind === "isf" ? "Korjaus" : "Ateria") + " · glukoosin muutos " + root.n(modelData.delta) + " mmol/l · " + modelData.id
                                    }
                                }
                            }
                        }
                        Button {
                            text: "Tarkista HH-suhteen muutos"
                            visible: root.studio.response.medicationApproved === true
                            enabled: card.modelData.status === "estimated" && root.studio.response.medicationApproved === true
                            onClicked: root.proposal = root.studio.proposeResponse(card.modelData.index)
                        }
                    }
                }
            }
        }
        Frame {
            visible: !!root.proposal.status
            Layout.fillWidth: true
            contentItem: ColumnLayout {
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.proposal.status === "proposal" ? "HH-suhde " + root.n(root.proposal.current) + " → " + root.n(root.proposal.candidate) + " g/U\nSaman HH-määrän annosmuutos " + root.n(root.proposal.doseChangePercent) + " % · suhteen muutos " + root.n(root.proposal.ratioChangePercent) + " %" : (root.proposal.reasons || []).join("\n")
                }
                Button {
                    text: "Hyväksy muutos uuteen profiiliin"
                    visible: root.proposal.status === "proposal"
                    enabled: root.proposal.status === "proposal"
                    onClicked: {
                        let r = root.studio.acceptResponse();
                        root.message = r.status === "saved" ? "Profiiliversio " + r.version + " tallennettu." : (r.reasons || []).join("\n");
                        root.proposal = ({});
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: root.message.length > 0
            text: root.message
        }
        Disclosure {
            Layout.fillWidth: true
            title: "Pitkän ajan vertailujaksot"
            visible: root.studio.response.status === "evaluated"
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: {
                    let p = root.studio.response.reference || {};
                    return p.from ? "Vertailu: " + new Date(p.from).toLocaleDateString() + " – " + new Date(p.to - 1).toLocaleDateString() + " · " + (root.studio.response.referenceSelection === "entire_window" ? "koko vertailujakso" : "lähin vertailukelpoinen hoitojakso") : "Vertailukelpoista aiempaa jaksoa ei ole.";
                }
            }
            Repeater {
                model: root.studio.response.trend || []
                delegate: Label {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: new Date(modelData.from).toLocaleDateString() + " – " + new Date(modelData.to - 1).toLocaleDateString() + " · " + root.status(modelData.status) + (modelData.comparable === false ? " · eri hoito, ei suoraa vertailua" : "") + "\n" + (modelData.groups || []).map(g => g.name + ": korjausvaste " + root.n(g.isf) + " mmol/l/U, HH-suhde " + root.n(g.ratio) + " g/U (" + root.status(g.status) + ")").join("\n")
                }
            }
        }
        Disclosure {
            Layout.fillWidth: true
            title: "Arviosta pois jätetyt tapahtumat (" + (root.studio.response.excluded || []).length + ")"
            visible: (root.studio.response.excluded || []).length > 0
            Repeater {
                model: root.studio.response.excluded || []
                delegate: Label {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: (modelData.reasons || []).join(" · ")
                }
            }
        }
        Disclosure {
            objectName: "responseCandidatesSection"
            Layout.fillWidth: true
            title: "Tarkistettavat tapahtumat (" + root.studio.responseCandidates.length + ")"
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Avaa tapahtuma ja vahvista annokset sekä kirjausten kattavuus."
            }
            Repeater {
                model: root.studio.responseCandidates
                delegate: Button {
                    required property var modelData
                    text: modelData.startText + " · " + (modelData.kind === "icr" ? "Ateria" : "Korjaus") + " · " + modelData.linkedIds.length + " kirjausta"
                    onClicked: root.selectCandidate(modelData)
                }
            }
        }
        Disclosure {
            Layout.fillWidth: true
            title: "Mittausvirheen tarkistus"
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Kirjaa vain erikseen varmistettu mittausvirhe. Aidon matalan glukoosin havaintoa ei saa poistaa tästä. Alkuperäiset tiedot säilyvät, ja lähdemuutos mitätöi tarkistuksen."
            }
            RowLayout {
                TextField {
                    id: artifactFrom
                    placeholderText: "Alku vvvv-kk-pp tt:mm"
                    Layout.fillWidth: true
                }
                TextField {
                    id: artifactTo
                    placeholderText: "Loppu vvvv-kk-pp tt:mm"
                    Layout.fillWidth: true
                }
            }
            TextField {
                id: artifactReason
                placeholderText: "Syy mittauksen epäluotettavuuteen"
                Layout.fillWidth: true
            }
            TextField {
                id: artifactEvidence
                placeholderText: "Mihin varmennus perustuu (esim. vertailumittaus)"
                Layout.fillWidth: true
            }
            ReviewCheckBox {
                id: artifactConfirmed
                text: "Olen varmistanut ja dokumentoinut mittausartefaktin; kyse ei ole aidon hypoglykemian poissulusta"
            }
            Button {
                text: "Tallenna artefaktin tarkistus"
                enabled: artifactConfirmed.checked
                onClicked: {
                    root.studio.reviewGlucoseArtifact(artifactFrom.text, artifactTo.text, artifactReason.text, artifactEvidence.text, artifactConfirmed.checked);
                    artifactConfirmed.checked = false;
                }
            }
            Repeater {
                model: root.studio.artifactReviews
                delegate: RowLayout {
                    id: artifactRow
                    required property var modelData
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: new Date(artifactRow.modelData.from_time).toLocaleString() + " – " + new Date(artifactRow.modelData.to_time).toLocaleString() + " · " + artifactRow.modelData.reason + " · " + (artifactRow.modelData.current ? "voimassa" : "ei voimassa / lähde muuttunut")
                    }
                    Button {
                        text: "Peru tarkistus"
                        enabled: !!artifactRow.modelData.active
                        onClicked: root.studio.removeGlucoseArtifact(artifactRow.modelData.id)
                    }
                }
            }
        }
        Disclosure {
            objectName: "responsePolicySection"
            Layout.fillWidth: true
            title: "Laskennan asetukset"
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Määritä neljän ryhmän alkamisajat kasvavassa järjestyksessä. Viimeinen ryhmä jatkuu keskiyön yli ensimmäiseen. Tyhjät kelpuutus- tai muutosrajat estävät laskennan. Tekniset bootstrap-asetukset eivät määritä lääkeannosta."
            }
            Repeater {
                model: root.draft.groups || []
                delegate: RowLayout {
                    id: groupRow
                    required property int index
                    required property var modelData
                    Label {
                        text: groupRow.modelData.name
                        Layout.preferredWidth: 130
                    }
                    TextField {
                        placeholderText: "HH:mm"
                        text: groupRow.modelData.startMinute === null ? "" : String(Math.floor(groupRow.modelData.startMinute / 60)).padStart(2, "0") + ":" + String(groupRow.modelData.startMinute % 60).padStart(2, "0")
                        onEditingFinished: {
                            let p = text.split(":");
                            root.groupSet(groupRow.index, "startMinute", /^([01][0-9]|2[0-3]):[0-5][0-9]$/.test(text) ? Number(p[0]) * 60 + Number(p[1]) : null);
                        }
                    }
                    TextField {
                        placeholderText: "Ulkoinen ISF, valinnainen"
                        text: groupRow.modelData.externalIsf === null ? "" : groupRow.modelData.externalIsf
                        onEditingFinished: root.groupSet(groupRow.index, "externalIsf", text.trim().length ? Number(text.replace(",", ".")) : null)
                    }
                }
            }
            Repeater {
                model: [
                    {
                        title: "Aineiston riittävyys",
                        fields: [["minimumEpisodes", "Tapahtumia vähintään / ryhmä"], ["minimumDays", "Erillisiä päiviä vähintään / ryhmä"], ["minimumBasalDays", "Vakaita perusvaikutuksen päiviä vähintään"], ["minCoverage", "Datan vähimmäiskattavuus (0–1)"], ["doseMatchToleranceUnits", "Sallittu annospoikkeama profiilista (U)"], ["minimumDose", "Pienin kelpuutettava annos (U)"], ["minimumCarbs", "Pienin kelpuutettava HH-määrä (g)"]]
                    },
                    {
                        title: "Arvion luotettavuus",
                        fields: [["maxRelativeWidth", "Suurin suhteellinen arvioalueen leveys"], ["maxDayInfluencePercent", "Yhden päivän suurin vaikutus (%)"], ["maxPeriodDriftPercent", "Osajaksojen suurin muutos (%)"], ["maxInvalidFraction", "Suurin puuttuvien estimaattien osuus"]]
                    },
                    {
                        title: "Muutosehdotusten rajat",
                        fields: [["doseStepPercent", "Saman HH-määrän annosmuutosaskel (%)"], ["maximumChangePercent", "Suurin sallittu annosmuutos (%)"]]
                    },
                    {
                        title: "Laskentamenetelmä",
                        fields: [["bootstrapSamples", "Bootstrap-toistot"], ["blockDays", "Peräkkäisiä päiviä lohkossa"], ["endpointMinutes", "Alku-/loppumediaanin ikkuna (min)"]]
                    }
                ]
                delegate: Disclosure {
                    id: policyGroup
                    required property var modelData
                    Layout.fillWidth: true
                    title: policyGroup.modelData.title
                    Repeater {
                        model: policyGroup.modelData.fields
                        delegate: RowLayout {
                            id: policyField
                            required property var modelData
                            Layout.fillWidth: true
                            Label {
                                text: policyField.modelData[1]
                                Layout.preferredWidth: Math.min(360, root.availableWidth * 0.45)
                                wrapMode: Text.WordWrap
                            }
                            TextField {
                                objectName: "responsePolicyField" + policyField.modelData[0]
                                Layout.fillWidth: true
                                text: root.draft[policyField.modelData[0]] === null ? "" : root.draft[policyField.modelData[0]]
                                placeholderText: "Määritettävä"
                                onEditingFinished: root.set(policyField.modelData[0], text.trim().length ? Number(text.replace(",", ".")) : null)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Button {
                    text: "Tallenna luonnos"
                    onClicked: {
                        let r = root.studio.saveResponsePolicy(root.draft, false);
                        root.message = r.status === "saved" ? "Luonnos tallennettu." : (r.reasons || []).join("\n");
                    }
                }
                Button {
                    text: "Hyväksy vastepolitiikka"
                    onClicked: {
                        let r = root.studio.saveResponsePolicy(root.draft, true);
                        root.message = r.status === "saved" ? "Vastepolitiikka hyväksytty." : (r.reasons || []).join("\n");
                    }
                }
            }
        }
        Disclosure {
            Layout.fillWidth: true
            title: "Mitä arvio tarkoittaa?"
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Korjausvaste kertoo glukoosin laskun insuliiniyksikköä kohti. HH-suhde kertoo hiilihydraattigrammat yksikköä kohti. Arvio ja käytössä oleva hoitoasetus ovat erillisiä."
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Tutkimusmalli. Bootstrap-arvioalue kuvaa aineiston vaihtelua, mutta ei kaikkia kirjaus- tai imeytymisvirheitä. Lääkemuutokset tarvitsevat erillisen hyväksynnän."
            }
        }
    }
}
