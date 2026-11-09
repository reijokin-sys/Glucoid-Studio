// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "theme.js" as Theme
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: window
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
    width: 1240; height: 860
    minimumWidth: 960; minimumHeight: 680
    visible: true
    readonly property string applicationTitle: "Glucoid Studio"+(typeof appVersion==="string"&&appVersion.length?" "+appVersion:"")
    title: applicationTitle+(demoMode ? " — esimerkkiaineisto" : "")
    property var selectedIds: ({})
    property var calculation: ({})
    property var assessment: ({})
    property string episodeResult: ""
    Connections {
        target:studio
        function onChanged() {
            if(window.calculation.status === "research" && (window.calculation.sourceRevision !== studio.dataRevision || window.calculation.profileVersion !== studio.profile.version)) window.calculation=({status:"blocked",reasons:["Aineisto tai hoitoprofiili muuttui. Laske uudelleen."]})
            if(window.assessment.status === "research" && (window.assessment.sourceRevision !== studio.dataRevision || window.assessment.profileVersion !== studio.profile.version)) window.assessment=({status:"blocked",reasons:["Aineisto tai hoitoprofiili muuttui. Arvioi uudelleen."]})
        }
    }
    Timer { interval:1000;repeat:true;running:true;onTriggered:if(window.calculation.status === "research" && Date.now()>window.calculation.validUntil) window.calculation=({status:"blocked",reasons:["Mittauksen kelpoisuusaika on päättynyt. Laske uudelleen tuoreella mittauksella."]}) }
    function showView(index) {
        if(deleteDialog.visible)deleteDialog.reject()
        tabs.currentIndex=index === 6 ? 5 : index;settingsTabs.currentIndex=index === 6 ? 1 : 0
        if(index === 1 && demoMode && studio.diary.length){deleteDialog.entry=studio.diary[0];deleteDialog.open()}
    }
    function decimal(text) { let s=String(text).trim().replace(",",".");return s.length ? Number(s) : NaN }
    function n(value,digits) { return typeof value === "number" && Number.isFinite(value) ? value.toLocaleString(Qt.locale("fi_FI"),"f",digits === undefined ? 1 : digits) : "—" }
    function stamp() { return new Date().toLocaleString(Qt.locale("sv_SE"),"yyyy-MM-dd HH:mm") }
    function select(id,checked) { let m=Object.assign({},selectedIds);if(checked)m[id]=true;else delete m[id];selectedIds=m }
    Dialog {
        id:deleteDialog
        parent:Overlay.overlay
        anchors.centerIn:parent
        width:Math.min(window.width-80,520)
        title:"Poista merkintä päiväkirjasta?"
        modal:true
        standardButtons:Dialog.Ok | Dialog.Cancel
        property var entry: ({})
        contentItem:Label {
            text:(deleteDialog.entry.timeText || "")+" · "+(deleteDialog.entry.label || "")+"\n\nMerkinnän voi palauttaa kohdasta Poistetut merkinnät. Jugglucon alkuperäinen tieto säilyy. Poisto mitätöi merkintään perustuvat arviot. Poistettu annos- tai HH-tieto voi estää laskennan vaikutusikkunan aikana."
            wrapMode:Text.WordWrap
        }
        onAccepted:if(studio.removeAmount(entry.id))window.select(entry.id,false)
    }
    function resultText(result) {
        if (!result.status) return ""
        if (result.status === "blocked") return "Laskenta estetty\n"+(result.reasons || []).join("\n")
        if (result.status === "saved") return "Tallennettu."
        if (result.kind) return "Nykyinen: "+n(result.current,2)+"\nEhdotus: "+n(result.candidate,2)+" ("+n(result.changePercent,1)+" %)\n"+result.rule
        return "Tutkimuslaskelma: "+n(result.units,2)+" U\nHH-osuus: "+n(result.meal,2)+" U\nGlukoosikorjaus: "+n(result.correction,2)+" U\nAktiivinen pikainsuliini: "+n(result.iob,2)+" U\nEnnen pyöristystä: "+n(result.unrounded,2)+" U\n"+result.message
    }
    pageStack.initialPage: Kirigami.Page {
        title: window.applicationTitle
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Kirigami.Heading { text: studio.latestText; level: 2; Layout.fillWidth: true }
                BusyIndicator { running: studio.busy; visible: running; Layout.preferredWidth: 30;Layout.preferredHeight:30 }
                Button { text: "Hae tiedot"; enabled: !studio.busy && !demoMode; onClicked: studio.sync() }
                Button { text: "Varmuuskopio"; onClicked: studio.backup() }
            }
            Label { text:"Tutkimustila · lääkityslaskentaa ei ole kliinisesti validoitu";color:Kirigami.Theme.neutralTextColor;Layout.fillWidth:true;wrapMode:Text.WordWrap }
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: "Vuorokausi" }
                TabButton { text: "Päiväkirja" }
                TabButton { text: "Yhteenveto" }
                TabButton { text: "Hoidon arviointi" }
                TabButton { text: "Aterialaskuri" }
                TabButton { text: "Asetukset" }
            }
            StackLayout {
                Layout.fillWidth:true;Layout.fillHeight:true
                currentIndex: tabs.currentIndex
                ColumnLayout {
                    RowLayout {
                        Button { text:"Viimeiset 24 h";onClicked:studio.setWindow("",1) }
                        Label { text:"Päivä" }
                        TextField { id:day;placeholderText:"vvvv-kk-pp";text:new Date().toLocaleString(Qt.locale("sv_SE"),"yyyy-MM-dd");Layout.preferredWidth:150 }
                        Button { text:"Näytä päivä";onClicked:studio.setWindow(day.text,1) }
                        Button { text:"7 vrk";onClicked:studio.setWindow("",7) }
                        Button { text:"14 vrk";onClicked:studio.setWindow("",14) }
                        Button { text:"30 vrk";onClicked:studio.setWindow("",30) }
                    }
                    GlucoseChart { objectName:"glucoseChart";Layout.fillWidth:true;Layout.fillHeight:true;chartData:studio.chart }
                    Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Glukoosi mmol/l · kultaiset rajat: määritetty tavoitealue · sininen: pikainsuliini · violetti: perusinsuliini · kulta: HH. Vie osoitin merkinnän päälle nähdäksesi tiedot. Aukot ovat puuttuvaa dataa." }
                }
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        Kirigami.Heading { text:"Toteutuneet merkinnät";level:2 }
                        Label { text:"Lista käyttää Vuorokausi-näkymän aikaväliä. Valitse arviointiin kuuluvat ateria- ja insuliinimerkinnät tästä listasta.";wrapMode:Text.WordWrap;Layout.fillWidth:true }
                        RowLayout {
                            TextField { id:localDate;placeholderText:"vvvv-kk-pp tt:mm";text:window.stamp();Layout.preferredWidth:190 }
                            ComboBox { id:localKind;model:["HH (g)","Pikainsuliini (U)","Perusinsuliini (U)","Muu"] }
                            TextField { id:localValue;placeholderText:"Määrä";Layout.preferredWidth:90 }
                            TextField { id:localMedicine;placeholderText:"Insuliinivalmiste";Layout.fillWidth:true }
                            ComboBox { id:localPurpose;model:["ateria","korjaus","perus","muu"] }
                            Button { text:"Kirjaa toteutunut";onClicked:studio.addLocal(localDate.text,window.decimal(localValue.text),["carbs","rapid","basal","other"][localKind.currentIndex],localMedicine.text,localPurpose.currentText) }
                        }
                        Switch { id:showDeleted;text:"Näytä poistetut merkinnät ("+studio.deletedDiary.length+")" }
                        ColumnLayout {
                            visible:showDeleted.checked
                            Layout.fillWidth:true
                            Repeater {
                                model:studio.deletedDiary
                                delegate:RowLayout {
                                    required property var modelData
                                    Layout.fillWidth:true
                                    Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:modelData.timeText+" · "+modelData.label+" · "+window.n(modelData.value,2)+" · poistettu" }
                                    Button { text:"Palauta";onClicked:studio.restoreAmount(modelData.id) }
                                }
                            }
                            Label { visible:studio.deletedDiary.length === 0;text:"Tällä aikavälillä ei ole poistettuja merkintöjä." }
                        }
                        Repeater {
                            model:studio.diary
                            delegate: Frame {
                                required property var modelData
                                Layout.fillWidth:true
                                ColumnLayout {
                                    anchors.fill:parent
                                    RowLayout {
                                        CheckBox { checked:!!window.selectedIds[modelData.id];onClicked:window.select(modelData.id,checked);Accessible.name:"Valitse arviointijaksoon" }
                                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:modelData.timeText+" · "+modelData.label+" · "+window.n(modelData.value,2)+" "+(modelData.kind === "carbs" ? "g" : modelData.kind === "rapid" || modelData.kind === "basal" ? "U" : "")+" · "+(modelData.medicine || modelData.kind)+" · "+(modelData.local ? "paikallinen" : "Juggluco") }
                                        Label { text:modelData.purpose || "" }
                                        Button { text:"Poista";onClicked:{deleteDialog.entry=modelData;deleteDialog.open()} }
                                    }
                                    RowLayout {
                                        ComboBox { id:purpose;model:["ateria","korjaus","perus","muu"];currentIndex:Math.max(0,model.indexOf(modelData.purpose)) }
                                        TextField { id:note;Layout.fillWidth:true;placeholderText:"Paikallinen täydennys tai huomio";text:modelData.annotation.note || "" }
                                        Button { text:"Tallenna täydennys";onClicked:studio.annotate(modelData.id,purpose.currentText,note.text) }
                                    }
                                }
                            }
                        }
                        Label { visible:studio.diary.length === 0;text:"Valitulla aikavälillä ei ole merkintöjä." }
                    }
                }
                ScrollView {
                    contentWidth:availableWidth
                    ColumnLayout {
                        width:parent.width
                        Kirigami.Heading { text:"Glukoosiseurannan yhteenveto";level:2 }
                        Repeater {
                            model:studio.summaries
                            delegate: Frame {
                                required property var modelData
                                Layout.fillWidth:true
                                ColumnLayout {
                                    anchors.fill:parent
                                    Kirigami.Heading { text:modelData.days+" vuorokautta";level:3 }
                                    Label { text:"Kattavuus "+window.n(modelData.coverage*100)+" % · keskiarvo "+window.n(modelData.mean)+" mmol/l · vaihtelukerroin "+window.n(modelData.cv)+" %" }
                                    Label { text:modelData.targetsSet ? "Tavoitealueella "+window.n(modelData.inRange*100)+" % · alapuolella "+window.n(modelData.below*100)+" % · yläpuolella "+window.n(modelData.above*100)+" %" : "Määritä henkilökohtainen tavoitealue hoitoprofiilissa." }
                                }
                            }
                        }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Osuudet lasketaan katetusta ajasta. Puuttuva aika raportoidaan erikseen kattavuudessa. Yhteenveto ei yksin osoita, mikä aiheutti glukoosimuutokset." }
                        RowLayout {
                            Repeater { model:[7,14,30];delegate:Button { required property int modelData;text:modelData+" päivän käyrä";onClicked:{studio.setWindow("",modelData);tabs.currentIndex=0} } }
                        }
                    }
                }
                ScrollView {
                    contentWidth:availableWidth
                    ColumnLayout {
                        width:parent.width
                        spacing:12
                        Kirigami.Heading { text:"Tarkista vertailukelpoinen jakso";level:2 }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Valitse ensin Päiväkirjasta jakson toteutuneet merkinnät. Sovellus laskee mittauksista lähtö- ja loppuarvon, kattavuuden sekä poikkeaman. Hyväksyntä koskee vain nykyisen hoitoprofiilin aikana kerättyä aineistoa." }
                        RowLayout {
                            ComboBox { id:episodeKind;model:["HH-suhde / ateria","Korjauskerroin","Perusinsuliini / paasto"] }
                            TextField { id:episodeDate;Layout.preferredWidth:200;placeholderText:"Alku: vvvv-kk-pp tt:mm" }
                            Label { text:Object.keys(window.selectedIds).length+" merkintää valittu" }
                            Button { text:"Tyhjennä valinta";onClicked:window.selectedIds=({}) }
                        }
                        CheckBox { id:reviewed;text:"Olen tarkistanut aterian/korjauksen/paastojakson ja siihen liitetyt annokset" }
                        CheckBox { id:complete;text:"HH- ja annostiedot ovat kattavat koko arviointi- ja vaikutusjaksolta" }
                        CheckBox { id:contextClear;text:"Ei tunnettua sekoittavaa liikuntaa, sairautta, alkoholia tai hypoglykemian korjaushiilihydraatteja" }
                        CheckBox { id:stable;visible:episodeKind.currentIndex === 2;text:"Perusinsuliinin annostus on ollut vakaa profiilin tasaantumisjakson ajan" }
                        CheckBox { id:basalComplete;visible:episodeKind.currentIndex === 2;text:"Kaikki tasaantumisjakson perusinsuliiniannokset on kirjattu ja toteutuminen tarkistettu" }
                        Button {
                            text:"Tallenna tarkistettu jakso"
                            onClicked:{let r=studio.saveEpisode({kind:["icr","isf","basal"][episodeKind.currentIndex],startText:episodeDate.text,linkedIds:Object.keys(window.selectedIds),reviewed:reviewed.checked,complete:complete.checked,contextClear:contextClear.checked,basalStable:stable.checked,basalHistoryComplete:basalComplete.checked});window.episodeResult=window.resultText(r)}
                        }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:window.episodeResult }
                        Kirigami.Heading { text:"Laske muutosehdotus";level:2 }
                        RowLayout {
                            ComboBox { id:assessKind;model:["HH-suhde","Korjauskerroin","Perusinsuliini"] }
                            ComboBox { id:assessSlot;model:studio.slots;textRole:"name";enabled:assessKind.currentIndex !== 2 }
                            Button { text:"Arvioi historiasta";onClicked:window.assessment=studio.assess(["icr","isf","basal"][assessKind.currentIndex],assessSlot.currentText) }
                        }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:window.resultText(window.assessment);font.pixelSize:16 }
                        Label { visible:!!window.assessment.status;text:"Mukana "+(window.assessment.included || []).length+" jaksoa · pois jätetty "+(window.assessment.excluded || []).length }
                        Repeater {
                            model:window.assessment.included || []
                            delegate: Label { required property var modelData;Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Mukana: "+modelData.startText+" · lähtö "+window.n(modelData.firstGlucose)+" → loppu "+window.n(modelData.lastGlucose)+" mmol/l · poikkeama "+window.n(modelData.deviation)+" · kattavuus "+window.n(modelData.coverage*100)+" %" }
                        }
                        Repeater {
                            model:window.assessment.excluded || []
                            delegate: Label { required property var modelData;Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Pois jätetty: "+(modelData.reasons || []).join(" · ") }
                        }
                        Button { text:"Hyväksy tämä yksi muutos uuteen tutkimusprofiiliin";enabled:window.assessment.status === "research";onClicked:{window.episodeResult=window.resultText(studio.acceptRecommendation());window.assessment=({})} }
                        Kirigami.Heading { text:"Tallennetut jaksot";level:3 }
                        Repeater {
                            model:studio.episodes
                            delegate: Label { required property var modelData;Layout.fillWidth:true;wrapMode:Text.WordWrap;text:modelData.startText+" · "+modelData.kind+" · "+modelData.slot+" · profiili "+modelData.profileVersion+(modelData.stale ? " · lähdetieto muuttunut, tarkistettava uudelleen" : "") }
                        }
                    }
                }
                ScrollView {
                    contentWidth:availableWidth
                    ColumnLayout {
                        width:parent.width
                        spacing:12
                        Kirigami.Heading { text:"Ateria-annoslaskuri";level:2 }
                        Label { text:"Käyttää viimeisintä mittausta ja hyväksytyn profiilin tämän hetken HH-suhdetta, korjauskerrointa ja vaikutusmallia.";Layout.fillWidth:true;wrapMode:Text.WordWrap }
                        RowLayout { Label { text:"Aterian hiilihydraatit (g)" } TextField { id:bolusCarbs;placeholderText:"g";Layout.preferredWidth:180;onTextChanged:window.calculation=({}) } }
                        CheckBox { id:measurement;text:"Olen varmistanut nykyisen glukoosimittauksen luotettavuuden";onToggled:window.calculation=({}) }
                        CheckBox { id:history;text:"Kaikki vaikutusikkunan toteutuneet insuliiniannokset on kirjattu";onToggled:window.calculation=({}) }
                        CheckBox { id:carbsComplete;text:"Aterian HH-määrä ja ateriatiedot ovat kattavat";onToggled:window.calculation=({}) }
                        CheckBox { id:context;text:"Olen arvioinut tilanteen soveltuvuuden (liikunta, sairaus ja muut poikkeustilanteet)";onToggled:window.calculation=({}) }
                        Button { text:"Laske tutkimuslaskelma";onClicked:window.calculation=studio.bolus({carbs:window.decimal(bolusCarbs.text),measurementConfirmed:measurement.checked,historyComplete:history.checked,carbsComplete:carbsComplete.checked,contextReviewed:context.checked}) }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:window.resultText(window.calculation);font.pixelSize:18 }
                        Label { visible:!!window.calculation.time;text:"Laskettu "+new Date(window.calculation.time).toLocaleString(Qt.locale("fi_FI"))+" · hoitoprofiili "+window.calculation.profileVersion }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Laskelma ei ole annoskirjaus. Toteutunut pistos kirjataan Päiväkirjassa tai Jugglucossa. Mittauksen, merkintöjen tai profiilin muuttuessa tarvitaan uusi laskelma." }
                    }
                }
                ColumnLayout {
                    TabBar { id:settingsTabs;Layout.fillWidth:true;TabButton { text:"Yhteys ja merkintäluokat" } TabButton { text:"Hoitoprofiili" } }
                    StackLayout {
                        Layout.fillWidth:true;Layout.fillHeight:true;currentIndex:settingsTabs.currentIndex
                        ScrollView {
                            contentWidth:availableWidth
                            ColumnLayout {
                                width:parent.width;spacing:12
                                Kirigami.Heading { text:"Yhteys Jugglucoon";level:2 }
                                Button { text:"Tuo Glucoidin yhteys ja tunnus avainrenkaaseen";enabled:!demoMode;onClicked:studio.importGlucoid() }
                                RowLayout {
                                    Label { text:"Yhteystapa" }
                                    ComboBox { id:connectionMode;model:["USB / ADB","Verkko / paikallinen palvelin"];currentIndex:studio.connection.mode === "network" ? 1 : 0 }
                                }
                                TextField { id:address;Layout.fillWidth:true;placeholderText:"Palvelimen juuriosoite (verkkomuodossa)";text:studio.connection.address }
                                TextField { id:serial;Layout.fillWidth:true;placeholderText:"ADB-laitetunnus (vain jos useita puhelimia)";text:studio.connection.serial }
                                ComboBox { id:authMode;model:["API secret","Juggluco-token","Ei tunnistautumista"];currentIndex:studio.connection.auth === "none" ? 2 : studio.connection.auth === "token" ? 1 : 0 }
                                TextField { id:secretField;Layout.fillWidth:true;placeholderText:"Uusi tunnus; tyhjä säilyttää avainrenkaan tunnuksen";echoMode:TextInput.Password }
                                Button { text:"Tallenna yhteys";enabled:!demoMode;onClicked:{studio.saveConnection(connectionMode.currentIndex === 0 ? "usb" : "network",address.text,["apisecret","token","none"][authMode.currentIndex],secretField.text,serial.text);secretField.clear()} }
                                Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"USB käyttää puhelimen omaa localhost-palvelinta. Verkkomuoto voi käyttää nykyistä WireGuard-yhteyttä tai paikallista Juggluco-palvelinta. Osoitetta ja tunnusta ei välitetä sovelluksen avauslinkissä." }
                                Kirigami.Heading { text:"Merkintäluokkien merkitys";level:2 }
                                Repeater {
                                    model:studio.labels
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth:true
                                        Label { text:modelData.label;Layout.preferredWidth:220 }
                                        ComboBox { id:mappingKind;model:["Luokittelematon","Pikainsuliini","Korjausinsuliini","Perusinsuliini","Hiilihydraatit","Muu"];currentIndex:Math.max(0,["unknown","rapid","correction","basal","carbs","other"].indexOf(modelData.kind)) }
                                        TextField { id:mappingMedicine;placeholderText:"Insuliinivalmiste";text:modelData.medicine;Layout.fillWidth:true }
                                        Button { text:"Tallenna";onClicked:studio.mapLabel(modelData.label,["unknown","rapid","correction","basal","carbs","other"][mappingKind.currentIndex],mappingMedicine.text) }
                                    }
                                }
                                Label { text:"Tietohakemisto: "+studio.storagePath;wrapMode:Text.WrapAnywhere;Layout.fillWidth:true }
                                Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Terveystiedot ja varmuuskopiot ovat paikallisia ja Gitin ulkopuolella. Automaattinen varmuuskopio tehdään ensimmäisen onnistuneen haun jälkeen päivittäin." }
                            }
                        }
                        ProfileEditor { historicalMode:false; }
                    }
                }
            }
            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:studio.status;color:Kirigami.Theme.disabledTextColor }
        }
    }
}
