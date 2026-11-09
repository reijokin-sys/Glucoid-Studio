// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ScrollView {
    id: root
    property bool historicalMode: true
    property var draft: ({})
    property int loadedVersion: -1
    property string result: ""
    function load() { draft = JSON.parse(JSON.stringify(studio.profile)); loadedVersion = studio.profile.version || 0 }
    Component.onCompleted: load()
    Connections { target: studio; function onChanged() { if ((studio.profile.version || 0) !== root.loadedVersion) root.load() } }
    function get(key) { let o=draft; for (let k of key.split(".")) o=o ? o[k] : null; return o === null || o === undefined ? "" : String(o) }
    function set(key,value,numeric) {
        let p=JSON.parse(JSON.stringify(draft)), o=p, keys=key.split(".")
        for (let i=0;i<keys.length-1;i++) { if (!o[keys[i]]) o[keys[i]]={}; o=o[keys[i]] }
        let v=value
        if (numeric) { let s=String(value).trim().replace(",","."); v=s.length ? Number(s) : null; if (!Number.isFinite(v)) v=null }
        o[keys[keys.length-1]]=v
        if (key === "rapidMedicine") p.actionModel.medicine=value
        draft=p
    }
    function save(approve) {
        let r=historicalMode ? studio.saveAnalysisProfile(draft,approve) : studio.saveProfile(draft,approve)
        result=r.status === "saved" ? "Profiiliversio "+r.version+" tallennettu." : (r.reasons || []).join("\n")
    }
    function activate() {
        let r=studio.activateObservationProfile(draft)
        result=r.status === "active" ? "Profiili "+r.version+" on käytössä seurannassa." : (r.reasons || []).join("\n")
        if(r.status === "active")load()
    }
    function approveResponse() {
        let r=studio.saveResponseProfile(draft,true)
        result=r.status === "saved" ? "Profiiliversio "+r.version+" hyväksytty vastearvioon. Annos- ja muutosehdotusten hyväksyntä on erillinen." : (r.reasons || []).join("\n")
    }
    contentWidth: availableWidth
    ColumnLayout {
        width: root.availableWidth
        spacing: 16
        Kirigami.Heading { text: "Hoitoprofiili"; level: 2 }
        Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:studio.observationProfile.version ? "Seurannassa profiili "+studio.observationProfile.version+(studio.observationProfile.version===root.draft.version?" · tämä versio":" · muokkaat eri versiota") : "Täydennä tiedot ja ota hoito seurantaan."}
        TextField { visible:root.historicalMode;Layout.fillWidth:true;placeholderText:"Voimassa alkaen: vvvv-kk-pp tt:mm";text:root.get("effectiveFromText");onEditingFinished:root.set("effectiveFromText",text,false) }
        ReviewCheckBox { visible:root.historicalMode;text:"Asetukset olivat voimassa tästä ajasta alkaen";checked:!!root.draft.historicalConfirmed;onToggled:root.set("historicalConfirmed",checked,false) }
        Disclosure {
            Layout.fillWidth:true;title:"Profiilin käyttö ja käsitteet"
            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Seuranta käyttää ilmoitettua hoitoa. Vastearvio ja lääkityslaskenta hyväksytään erikseen."}
            Label {objectName:"responseProfileApprovalStatus";Layout.fillWidth:true;wrapMode:Text.WordWrap;text:(root.draft.responseApproved?"Vastearvion hyväksyntä on voimassa. ":"")+"Vastearviossa nykyinen korjauskerroin ja annoslaskurin yksittäinen tavoite voivat puuttua. Muut rajat ja ammattilaisen tarkistus tarvitaan."}
            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Korjauskerroin: yhden insuliiniyksikön aiheuttama glukoosin lasku (mmol/l/U). HH-suhde: yhden yksikön kattama hiilihydraattimäärä (g/U)."}
            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Korjausten tavoitealue on erillinen tieto annoslaskurin yksittäisestä tavoitteesta. Tyhjää arvoa ei korvata oletuksella."}
        }
        Repeater {
            model: [
                {title:"Insuliinit",open:true,fields:[
                    ["rapidMedicine","Pikainsuliini",false],["basalMedicine","Perusinsuliini",false],
                    ["basalDose","Perusinsuliinin annos (U)",true],["basalTime","Perusinsuliinin aika (HH:mm)",false]]},
                {title:"Tavoitteet",fields:[
                    ["targetLow","Tavoitealueen alaraja (mmol/l)",true],["targetHigh","Tavoitealueen yläraja (mmol/l)",true],
                    ["correctionTargetRange.low","Korjausten tavoite: alaraja (mmol/l)",true],["correctionTargetRange.high","Korjausten tavoite: yläraja (mmol/l)",true],
                    ["target","Annoslaskurin tavoite (mmol/l)",true],["hypo","Laskennan estävä matalan raja (mmol/l)",true]]},
                {title:"Vaikutusmalli ja tarkistaja",fields:[
                    ["actionModel.name","Mallin nimi ja versio",false],["actionModel.reference","Mallin lähde ja soveltuvuus",false],["actionModel.durationMinutes","Pikainsuliinin vaikutusaika (min)",true],
                    ["basalReportedAction.durationMinutes","Perusinsuliinin vaikutusaika (min, arvio)",true],["basalStableDays","Perusannoksen tasaantumisaika (vrk)",true],
                    ["timezone","Aikavyöhyke",false],["reviewer","Profiilin tarkistanut ammattilainen",false],
                    ["maxGlucoseAgeMinutes","Mittauksen enimmäisikä (min)",true],["maxBolus","Enimmäisannos (U)",true],["doseIncrement","Annosporras (U)",true]]},
                {title:"Kelpuutettavan aineiston rajat",fields:[
                    ["rules.minimumEpisodes","Jaksojen vähimmäismäärä",true],["rules.minimumDays","Erillisten päivien vähimmäismäärä",true],["rules.minCoverage","Datan vähimmäiskattavuus (0–1)",true],
                    ["rules.maxGapMinutes","Mittausvälin enimmäispituus (min)",true],["rules.consensus","Yhdensuuntaisten havaintojen osuus (>0,5–1)",true],["rules.minReviewDays","Muutosten vähimmäisarviointiväli (vrk)",true],
                    ["rules.mealWindowMinutes","Aterian seurantajakso (min)",true],["rules.correctionWindowMinutes","Korjauksen seurantajakso (min)",true],["rules.basalWindowMinutes","Paaston/yön seurantajakso (min)",true],
                    ["rules.mealDeviation","Aterian poikkeamaraja (mmol/l)",true],["rules.correctionDeviation","Korjauksen poikkeamaraja (mmol/l)",true],["rules.basalDeviation","Perusinsuliinijakson poikkeamaraja (mmol/l)",true]]},
                {title:"Muutosehdotusten rajat",fields:[
                    ["rules.stepPercent","Yhden muutosehdotuksen askel (%)",true],["rules.maxChangePercent","Suurin sallittu muutos (%)",true],
                    ["rules.minimumIcr","HH-suhteen alaraja (g/U)",true],["rules.maximumIcr","HH-suhteen yläraja (g/U)",true],
                    ["rules.minimumIsf","Korjauskertoimen alaraja (mmol/l/U)",true],["rules.maximumIsf","Korjauskertoimen yläraja (mmol/l/U)",true],
                    ["rules.minimumBasal","Perusinsuliiniannoksen alaraja (U)",true],["rules.maximumBasal","Perusinsuliiniannoksen yläraja (U)",true]]}
            ]
            delegate: Disclosure {
                required property var modelData
                Layout.fillWidth: true
                title:modelData.title
                expanded:!!modelData.open
                Repeater {
                    model: modelData.fields
                    delegate: RowLayout {
                        required property var modelData
                        visible:!root.historicalMode || !["maxGlucoseAgeMinutes","maxBolus","doseIncrement"].includes(modelData[0])
                        Layout.fillWidth: true
                        Label { text: modelData[1]; Layout.preferredWidth: Math.min(340,root.availableWidth*0.48); wrapMode: Text.WordWrap }
                        TextField {
                            Layout.fillWidth: true
                            text: root.get(modelData[0])
                            placeholderText: "Määritettävä"
                            onEditingFinished: root.set(modelData[0],text,modelData[2])
                        }
                    }
                }
            }
        }
        Label { visible:!root.historicalMode;Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Vaikutuskäyrä: yksi piste riville muodossa minuutti; jäljellä oleva osuus. Alku on 0;1 ja loppu määritetty vaikutusaika;0. Käytä valmisteelle hyväksyttyä lähteistettyä mallia." }
        TextArea {
            visible:!root.historicalMode
            Layout.fillWidth: true; Layout.preferredHeight: 130
            placeholderText: "minuutti;osuus"
            text: ((root.draft.actionModel || {}).curve || []).map(p => p.minute+";"+p.fraction).join("\n")
            onActiveFocusChanged: if (!activeFocus) {
                let curve=[]
                for (let line of text.split("\n")) {
                    if (!line.trim()) continue
                    let parts=line.split(";")
                    curve.push({minute:parts.length === 2 ? Number(parts[0].trim().replace(",",".")) : null, fraction:parts.length === 2 ? Number(parts[1].trim().replace(",",".")) : null})
                }
                let p=JSON.parse(JSON.stringify(root.draft)); p.actionModel.curve=curve;root.draft=p
            }
        }
        RowLayout {
            visible:!root.historicalMode
            Label { text: "Annoslaskelman pyöristys" }
            ComboBox { model:["Alaspäin annosportaaseen","Lähimpään annosportaaseen"]; currentIndex:root.draft.rounding === "nearest" ? 1 : 0; onActivated:root.set("rounding",currentIndex === 1 ? "nearest" : "down",false) }
        }
        Kirigami.Heading { text: "Vuorokauden hoitojaksot"; level: 3 }
        Label { text:"Jaksot alkavat aikajärjestyksessä, ensimmäinen klo 00:00. HH-suhde on grammoja per yksikkö."; wrapMode:Text.WordWrap;Layout.fillWidth:true }
        Repeater {
            model: root.draft.slots || []
            delegate: RowLayout {
                id: slotRow
                required property int index
                required property var modelData
                function update(key,value) { let p=JSON.parse(JSON.stringify(root.draft));p.slots[index][key]=value;root.draft=p }
                TextField { Layout.fillWidth:true; placeholderText:"Jakson nimi";text:modelData.name;onEditingFinished:slotRow.update("name",text) }
                TextField { Layout.preferredWidth:100;placeholderText:"HH:mm";text:String(Math.floor(modelData.startMinute/60)).padStart(2,"0")+":"+String(modelData.startMinute%60).padStart(2,"0");onEditingFinished:{let p=text.split(":");slotRow.update("startMinute",p.length === 2 ? Number(p[0])*60+Number(p[1]) : null)} }
                TextField { Layout.preferredWidth:130;placeholderText:"HH-suhde g/U";text:modelData.icr === null ? "" : modelData.icr;onEditingFinished:slotRow.update("icr",text.trim().length ? Number(text.replace(",",".")) : null) }
                TextField { Layout.preferredWidth:155;placeholderText:"Korjaus mmol/l/U";text:modelData.isf === null ? "" : modelData.isf;onEditingFinished:slotRow.update("isf",text.trim().length ? Number(text.replace(",",".")) : null) }
                Button { text:"Poista jakso";enabled:index>0;onClicked:{let p=JSON.parse(JSON.stringify(root.draft));p.slots.splice(index,1);root.draft=p} }
            }
        }
        Button { text:"Lisää hoitojakso";onClicked:{let p=JSON.parse(JSON.stringify(root.draft));p.slots.push({name:"",startMinute:null,icr:null,isf:null});root.draft=p} }
        Flow {
            Layout.fillWidth:true;spacing:8
            Button { text:"Tallenna luonnos";onClicked:root.save(false) }
            Button { text:"Ota seurantaan";onClicked:root.activate() }
        }
        Disclosure {
            objectName:"profileApprovals"
            Layout.fillWidth:true;title:"Hyväksy laskentaan"
            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Vastearvio käyttää tarkistettuja havaintoja. Annos- ja muutosehdotukset tarvitsevat täydelliset lääkeasetukset."}
            Flow {
                Layout.fillWidth:true;spacing:8
                Button {objectName:"approveResponseProfile";visible:root.historicalMode;text:"Hyväksy vastearvioon";onClicked:root.approveResponse()}
                Button {text:"Hyväksy lääkityslaskentaan";onClicked:root.save(true)}
            }
        }
        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:root.result }
    }
}
