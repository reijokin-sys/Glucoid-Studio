// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "theme.js" as Theme
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
Kirigami.ApplicationWindow {
    id:window
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
    // Keep bounded native render tests exposed under the Wayland compositor.
    flags:Qt.Window | (uiTestMode ? Qt.WindowStaysOnTopHint : 0)
    width:uiTestWidth>0?uiTestWidth:1280;height:900;minimumWidth:900;minimumHeight:650
    Component.onCompleted:{episodeRevision=studio.dataRevision;episodeProfileVersion=studio.profile.version || 0;studio.setWindow(studio.analysis.lastDate,1);studio.startAutomaticAnalysis(false)}
    visible:true;title:"Glucoid Studio — analyysi"
    property var selectedIds: ({})
    property var assessment: ({})
    property string episodeResult:""
    property bool episodeNeedsProfile:false
    property string episodeRevision:""
    property int episodeProfileVersion:0
    onSelectedIdsChanged:resetEpisodeReview()
    function resetEpisodeReview() {
        for (let control of [reviewed,complete,contextClear,stable,basalComplete,endStable])
            if(control) control.checked=false
        episodeResult=""
        episodeNeedsProfile=false
    }
    function saveReviewedEpisode() {
        let input={kind:["icr","isf","basal"][episodeKind.currentIndex],startText:episodeDate.text,linkedIds:Object.keys(selectedIds),reviewed:reviewed.checked,complete:complete.checked,contextClear:contextClear.checked,basalStable:stable.checked,basalHistoryComplete:basalComplete.checked,endStable:endStable.checked,sourceRevision:episodeRevision,profileVersion:episodeProfileVersion}
        let r=responseV2.checked?studio.saveResponseEpisode(input):studio.saveEpisode(input)
        if(r.status==="saved") {
            selectedIds=({})
            resetEpisodeReview()
            savedEpisodes.expanded=true
            episodeResult="Jakso tallennettu."
        } else {
            episodeNeedsProfile=!!r.requiresProfile
            episodeResult="Jaksoa ei tallennettu.\n"+(r.reasons || []).join("\n")
        }
    }
    function n(v,d=1){return v===null || v===undefined || !Number.isFinite(Number(v)) ? "—" : Number(v).toLocaleString(Qt.locale("fi_FI"),'f',d)}
    function decimal(v){let x=String(v).trim();return x.length ? Number(x.replace(",",".")) : null}
    function select(id,checked){let next=Object.assign({},selectedIds);if(checked)next[id]=true;else delete next[id];selectedIds=next}
    function showView(index){if(index===7){tabs.currentIndex=5;return}if(index<5)tabs.currentIndex=index;else{tabs.currentIndex=4;settingsTabs.currentIndex=index===6?1:0}}
    function resultText(r){if(!r.status)return "";if(r.status==="blocked")return "Ei luotettavaa muutosehdotusta\n"+(r.reasons || []).join("\n");if(r.status==="saved")return "Tallennettu.";return "Nykyinen: "+n(r.current,2)+" → ehdotus: "+n(r.candidate,2)+" ("+n(r.changePercent,1)+" %)\n"+r.rule}
    function kindText(v){return ({carbs:"HH",rapid:"Pikainsuliini",correction:"Korjausinsuliini",basal:"Perusinsuliini",other:"Muu",unknown:"Luokitus puuttuu",icr:"Ateria",isf:"Korjaus"})[v] || v}
    function stamp(v){return v>0 ? new Date(v).toLocaleString(Qt.locale("fi_FI")) : "Ei tietoa"}
    Connections { target:studio;function onChanged(){
        window.assessment=({})
        let revision=studio.dataRevision,version=studio.profile.version || 0
        if(window.episodeRevision!==revision || window.episodeProfileVersion!==version) {
            window.resetEpisodeReview()
            window.episodeRevision=revision
            window.episodeProfileVersion=version
        }
    } }
    pageStack.initialPage:Kirigami.Page {
        title:"Glucoid Studio"
        ColumnLayout {
            anchors.fill:parent;spacing:10
            RowLayout {
                Kirigami.Heading { text:"Oma seuranta";Layout.fillWidth:true }
                BusyIndicator { running:studio.busy;visible:running;Layout.preferredWidth:28;Layout.preferredHeight:28 }
                Button { text:studio.busy ? "Tuodaan…" : "Tuo puhelimesta";enabled:!studio.busy&&!demoMode;onClicked:studio.sync() }
                ToolButton {
                    text:"Lisää";onClicked:moreMenu.open()
                    Menu { id:moreMenu
                        MenuItem { text:"Tallenna raportti";onTriggered:studio.saveReport() }
                        MenuItem { text:"Varmuuskopioi";onTriggered:studio.backup() }
                        MenuItem { text:"Asetukset";onTriggered:tabs.currentIndex=4 }
                    }
                }
            }
            Label { Layout.fillWidth:true;text:studio.status;elide:Text.ElideRight;HoverHandler{id:statusHover}ToolTip.visible:statusHover.hovered;ToolTip.text:text }
            RowLayout {
                Repeater { model:[7,14,28];delegate:Button {required property int modelData;text:(modelData/7)+" vk";checkable:true;checked:studio.analysis.days===modelData;onClicked:studio.setAnalysisWindow(modelData,lastDate.text)} }
                Label { text:(studio.analysis.firstDate || "")+" – "+(studio.analysis.lastDate || "");Layout.fillWidth:true;horizontalAlignment:Text.AlignRight }
                ToolButton { text:"Vaihda päivää";onClicked:periodPopup.open() }
            }
            Popup {
                id:periodPopup;anchors.centerIn:Overlay.overlay;modal:true;padding:20
                ColumnLayout {
                    Label { text:"Jakson viimeinen päivä";font.bold:true }
                    TextField { id:lastDate;Layout.preferredWidth:220;placeholderText:"vvvv-kk-pp";text:studio.analysis.lastDate || "";onAccepted:{studio.setAnalysisWindow(studio.analysis.days,text);periodPopup.close()} }
                    Button { text:"Näytä jakso";onClicked:{studio.setAnalysisWindow(studio.analysis.days,lastDate.text);periodPopup.close()} }
                }
            }
            Label { Layout.fillWidth:true;elide:Text.ElideRight;opacity:0.75;text:"Glukoosi: "+window.stamp(studio.analysis.lastReading)+"  ·  Kirjaukset: "+window.stamp(studio.analysis.lastAmount);HoverHandler{id:freshnessHover}ToolTip.visible:freshnessHover.hovered;ToolTip.text:"Viimeisin onnistunut tuonti: "+window.stamp(studio.analysis.lastImport) }
            TabBar { objectName:"mainTabs";id:tabs;Layout.fillWidth:true;TabButton{text:"Yhteenveto"}TabButton{text:"Vuorokausi"}TabButton{text:"Päivät"}TabButton{text:"Jaksojen tarkistus"}TabButton{text:"Asetukset"}TabButton{text:"Insuliinivaste"} }
            StackLayout {
                Layout.fillWidth:true;Layout.fillHeight:true;currentIndex:tabs.currentIndex
                ScrollView {
                    id:summaryScroll
                    contentWidth:availableWidth
                    ColumnLayout {
                        width:summaryScroll.availableWidth;spacing:16
                        GridLayout {
                            objectName:"summaryMetrics"
                            Layout.fillWidth:true;columns:4;columnSpacing:12
                            MetricCard { Layout.fillWidth:true;title:"Keskiarvo";value:window.n(studio.analysis.mean);unit:"mmol/l" }
                            MetricCard { Layout.fillWidth:true;title:"Tavoitealueella";value:studio.analysis.targetsSet?window.n(studio.analysis.inRange*100):"—";unit:"%" }
                            MetricCard { Layout.fillWidth:true;title:"Vaihtelu (CV)";value:window.n(studio.analysis.cv);unit:"%" }
                            MetricCard { Layout.fillWidth:true;title:"Datan kattavuus";value:window.n(studio.analysis.coverage*100);unit:"%" }
                        }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:studio.analysis.targetsSet?"Alle tavoitealueen "+window.n(studio.analysis.below*100)+" %   ·   Yli tavoitealueen "+window.n(studio.analysis.above*100)+" %":"Aseta henkilökohtainen tavoitealue asetuksissa." }
                        ProfileChart { Layout.fillWidth:true;Layout.preferredHeight:300;profileData:studio.analysis }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;opacity:0.75;text:"Vihreä: keskiarvo  ·  Kulta: mediaani  ·  Alueet: vaihtelu" }
                        Disclosure {
                            objectName:"summaryStatistics"
                            Layout.fillWidth:true;title:"Tilastot ja vertailu"
                            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Mediaani "+window.n(studio.analysis.median)+" mmol/l · Keskihajonta "+window.n(studio.analysis.sd)+" mmol/l" }
                            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:{let p=studio.analysis.previous || {};return "Edellinen "+studio.analysis.days+" vrk: keskiarvo "+window.n(p.mean)+" mmol/l · kattavuus "+window.n(p.coverage*100)+" %"+(p.mean!==undefined&&studio.analysis.mean!==undefined?" · muutos "+window.n(studio.analysis.mean-p.mean)+" mmol/l":"")} }
                            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Tänään: "+window.n((studio.analysis.today || {}).coveredSeconds/3600)+" h mittauksia. Keskeneräinen päivä ei kuulu raporttijaksoon." }
                            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Kuvaajan tumma alue: 5.–95. persentiili. Vaaleampi alue: 25.–75. persentiili. Päivät painavat yhtä paljon. Puuttuvaa mittausaikaa ei lasketa tavoitealueella vietettyyn aikaan." }
                        }
                        Disclosure {
                            id:night;objectName:"overnightSummary";Layout.fillWidth:true
                            title:"Perusinsuliini ja yöglukoosi"
                            property var result:studio.automaticMeals.overnight || ({})
                            property var report:(studio.automaticSettings.reportedBasalHistory || []).slice(-1)[0] || ({})
                            visible:result.status==="observed"
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Ilmoitettu perusinsuliini "+window.n(night.report.units,0)+" U päivittäin noin klo "+(night.report.time || "")+" · tieto käyttäjältä"}
                            Label {objectName:"overnightChange";Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Viimeisten 14 yön muutosmediaani "+window.n((night.result.last14 || {}).medianChangeQuiet)+" mmol/l · "+((night.result.last14 || {}).loggedQuietNights || 0)+" yötä ilman kirjattuja ateria-/pikainsuliinisekoittajia"}
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Matalia viime jaksolla "+((night.result.last14 || {}).lowNights || 0)+" / "+((night.result.last14 || {}).nights || 0)+" yössä; koko historiassa "+(night.result.lowNights || 0)+" / "+(night.result.observedNights || 0)+" · klo 00–06"}
                            Label {text:"Yöarvion perusteet";font.bold:true}
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:night.result.from>0?"Saatavilla olevat yöt: "+new Date(night.result.from).toLocaleDateString(Qt.locale("fi_FI"))+" – "+new Date(night.result.to).toLocaleDateString(Qt.locale("fi_FI")):""}
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Koko historian muutosmediaani "+window.n(night.result.medianChangeQuiet)+" mmol/l. Edellisten 14 yön muutosmediaani "+window.n((night.result.previous14 || {}).medianChangeQuiet)+" mmol/l."}
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Ilman kirjattuja sekoittajia: alkumediaani "+window.n(night.result.medianStartQuiet)+", loppumediaani "+window.n(night.result.medianEndQuiet)+" mmol/l. Yömuutosten keskimmäinen puolikas "+window.n(night.result.changeP25)+"…"+window.n(night.result.changeP75)+" mmol/l."}
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Riittävä mittauskattavuus "+(night.result.coveredNights || 0)+" yössä. Kaikkien kattavien öiden muutosmediaani "+window.n(night.result.medianChangeAll)+" mmol/l. Ilman saman tai edeltävän ajan matalia vertailuun jää "+(night.result.assessmentNights || 0)+" yötä."}
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Ajanjakso on ilmoitetun perushoidon koko saatavilla oleva historia. Hoidon jatkuminen viimeisimmän ilmoituksen jälkeen on oletus; päiväkohtaisia pistoksia ei ole varmennettu. Klo 00–06 on tekninen tarkasteluikkuna; nukkumis- ja paastoaikoja ei ole vahvistettu. Kirjaamaton ruoka, liikunta, alkoholi ja sensorivirheet voivat vaikuttaa. Yöhavainnot eivät määrää uutta annosta."}
                        }
                        Frame {
                            id:tracking;objectName:"observationSummary";Layout.fillWidth:true
                            property var regimen:studio.observationProfile
                            property var observed:studio.observation
                            property bool dataActive:!!observed.active
                            contentItem:ColumnLayout {
                                spacing:10
                                Kirigami.Heading {text:"Käytössä oleva hoito";level:3}
                                Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;visible:!tracking.dataActive;text:"Ota hoitoprofiili seurantaan asetuksissa."}
                                Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;visible:tracking.dataActive;text:tracking.dataActive?tracking.regimen.basalMedicine+" "+window.n(tracking.regimen.basalDose)+" U klo "+tracking.regimen.basalTime+" · "+tracking.regimen.rapidMedicine:""}
                                Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;visible:tracking.dataActive;text:{let g=tracking.observed.glucose || {};if(tracking.observed.status==="waiting_for_glucose")return "Tämän hoidon ajalta puuttuu glukoositietoja.";if(tracking.observed.status==="before_profile_period")return "Raporttijakso on ennen tämän hoidon alkua.";if(tracking.observed.status==="timezone_mismatch")return "Tarkista raportin ja hoitoprofiilin aikavyöhykkeet.";return "Hoidon aikana tavoitealueella "+window.n(g.inRange*100)+" % · kattavuus "+window.n(g.coverage*100)+" %."}}
                                Disclosure {
                                    Layout.fillWidth:true;title:"Hoidon tiedot";visible:tracking.dataActive
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Profiili "+tracking.regimen.version+" · alkaen "+new Date(tracking.regimen.effectiveFrom).toLocaleDateString()}
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:(tracking.regimen.slots || []).map(s=>s.name+": HH-suhde "+window.n(s.icr)+" g/U · korjauskerroin "+(s.isf>0?window.n(s.isf)+" mmol/l/U":"ei tiedossa")).join("\n")}
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:{let p=tracking.regimen,r=p.correctionTargetRange || {};return "Tavoitealue "+window.n(p.targetLow)+"–"+window.n(p.targetHigh)+" mmol/l"+(r.low>0?" · korjausten tavoite "+window.n(r.low)+"–"+window.n(r.high)+" mmol/l":"")}}
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:{let g=tracking.observed.glucose || {};return "Alle tavoitealueen "+window.n(g.below*100)+" % · yli "+window.n(g.above*100)+" %."}}
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:tracking.observed.basalComparedDays>0?"Vertailukelpoisia perusannospäiviä "+tracking.observed.basalComparedDays+", ilmoitettua annosta vastaavia "+tracking.observed.basalMatchingDays+".":"Perusannoksen vertailu tarvitsee kattavaksi vahvistettuja päiviä ja saman valmisteen kirjaukset."}
                                    Disclosure {
                                        Layout.fillWidth:true;title:"Perusannoksen päivävertailu"
                                        Repeater {model:tracking.observed.daily || [];delegate:Label {required property var modelData;Layout.fillWidth:true;wrapMode:Text.WordWrap;text:{let r=modelData;let why=({partial_profile_day:"hoito alkoi kesken päivän",unconfirmed_day:"kattavuutta ei vahvistettu",unclassified_or_excluded:"tarkista luokitus tai poissulku",different_medicine:"eri valmiste",single_basal_entry_required:r.basalEntries===0?"kirjaus puuttuu":"useita kirjauksia"})[r.reason] || r.reason;return r.date+" · "+window.n(r.basalRecorded)+" U · "+(r.comparisonReady?"ero ilmoitettuun "+window.n(r.basalDifference)+" U":why)}}}
                                    }
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Seuranta kuvaa kirjattua hoitoa. Annos- ja muutosehdotukset vaativat erillisen hyväksynnän."}
                                }
                            }
                        }
                        Disclosure {
                            objectName:"dailyEntries"
                            Layout.fillWidth:true;title:"Annoskirjaukset ja hiilihydraatit"
                            summary:{let p=studio.analysis.insulin || {};return p.completeDays>0?p.completeDays+" vahvistettua päivää · kirjattu keskimäärin "+window.n(p.meanTotal)+" U/vrk":"Päivien kattavuutta ei ole vielä vahvistettu"}
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Vahvista päivä vain, kun kaikki annokset ja hiilihydraatit ovat mukana. Puuttuva kirjaus on tuntematon määrä."}
                            Repeater {
                                model:(studio.analysis.insulin || {}).daily || []
                                delegate:RowLayout {
                                    required property var modelData
                                    Layout.fillWidth:true
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:modelData.date+" · pika "+window.n(modelData.rapid)+" U · perus "+window.n(modelData.basal)+" U · HH "+window.n(modelData.carbs)+" g"+(modelData.unknown?" · tarkista luokitus/poissulku":"")}
                                    CheckBox {text:"Kattava päivä";checked:modelData.complete;onClicked:studio.confirmDay(modelData.date,checked)}
                                }
                            }
                            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Lähdemuutokset mitätöivät vahvistuksen. Kirjattu vuorokausiannos ei ole ennuste insuliinintarpeesta."}
                        }
                    }
                }
                ColumnLayout {
                    ProfileChart { objectName:"profileChart";Layout.fillWidth:true;Layout.fillHeight:true;profileData:studio.analysis }
                    Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Vie osoitin käyrälle: kellonaika, havaintopäivät ja kattavuus. Aikaväli 15 min." }
                    Label { text:studio.analysis.fillHistory ? "Sensorihistoria täydentää minuuttisarjan aukkoja; täydentävää aikaa "+window.n(studio.analysis.historySeconds/3600)+" h" : "Vain minuuttisarja. Sensorihistorian täydennys on pois käytöstä." }
                }
                ColumnLayout {
                    RowLayout {
                        TextField { id:day;Layout.preferredWidth:150;text:studio.analysis.lastDate || "";placeholderText:"vvvv-kk-pp" }
                        Button { text:"Vertaa edelliseen päivään";onClicked:studio.setWindow(day.text,1) }
                        Label { text:"Valitse tarkistettavaan jaksoon kuuluvat kirjaukset.";Layout.fillWidth:true;wrapMode:Text.WordWrap }
                    }
                    RowLayout {
                        Layout.fillWidth:true;Layout.fillHeight:true
                        GlucoseChart { objectName:"glucoseChart";Layout.fillWidth:true;Layout.fillHeight:true;chartData:studio.chart }
                        GlucoseChart { Layout.fillWidth:true;Layout.fillHeight:true;chartData:studio.comparisonChart }
                    }
                    ListView {
                        Layout.fillWidth:true;Layout.preferredHeight:200;clip:true;model:studio.diary
                        delegate:RowLayout {
                            required property var modelData
                            width:ListView.view.width
                            CheckBox { checked:!!window.selectedIds[modelData.id];onClicked:window.select(modelData.id,checked) }
                            Label { Layout.fillWidth:true;text:modelData.timeText+" · "+modelData.label+" · "+window.n(modelData.value)+" · "+window.kindText(modelData.kind)+(modelData.local ? " · aiempi paikallinen" : " · Juggluco");elide:Text.ElideRight }
                            ComboBox { id:purpose;model:["Valitse käyttötarkoitus","ateria","korjaus","perus","muu"];currentIndex:Math.max(0,model.indexOf(modelData.purpose));onActivated:if(currentIndex>0)studio.annotate(modelData.id,currentText,"") }
                            TextField { id:excludeReason;Layout.preferredWidth:150;placeholderText:"Poissulun syy";text:modelData.exclusionReason || "" }
                            Button { text:modelData.analysisExcluded ? "Palauta arviointiin" : "Jätä pois arviosta";onClicked:{if(modelData.analysisExcluded)studio.excludeFromAnalysis(modelData.id,"");else if(excludeReason.text.trim().length)studio.excludeFromAnalysis(modelData.id,excludeReason.text)} }
                        }
                    }
                    Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Alkuperäiset kirjaukset säilyvät. Poissulku estää kyseisen tapahtuman vaikutusikkunan lääkityslaskennan; tapahtunutta insuliiniannosta ei tulkita antamattomaksi." }
                }
                ScrollView {
                    contentWidth:availableWidth
                    ColumnLayout {
                        width:parent.width
                        spacing:12
                        Kirigami.Heading { text:"Tarkista tapahtuma";level:2 }
                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Valitse kirjaukset Päivät-näkymästä tai Insuliinivasteen tapahtumalistasta." }
                        RowLayout {
                            ComboBox { objectName:"episodeKind";id:episodeKind;model:["HH-suhde / ateria","Korjauskerroin","Perusinsuliini / paasto"];onCurrentIndexChanged:window.resetEpisodeReview() }
                            TextField { objectName:"episodeStart";id:episodeDate;Layout.preferredWidth:200;placeholderText:"Alku: vvvv-kk-pp tt:mm";onTextChanged:window.resetEpisodeReview() }
                            Label { text:Object.keys(window.selectedIds).length+" merkintää valittu" }
                            Button { text:"Tyhjennä valinta";onClicked:window.selectedIds=({}) }
                        }
                        ReviewCheckBox { objectName:"episodeReviewed";id:reviewed;text:"Olen tarkistanut aterian/korjauksen/paastojakson ja siihen liitetyt annokset" }
                        ReviewCheckBox { objectName:"episodeComplete";id:complete;text:"HH- ja annostiedot ovat kattavat koko arviointi- ja vaikutusjaksolta" }
                        ReviewCheckBox { objectName:"episodeContext";id:contextClear;text:"Ei sekoittavaa liikuntaa, sairautta, alkoholia tai hypoglykemian korjaushiilihydraatteja" }
                        ReviewCheckBox { objectName:"episodeBasalStable";id:stable;visible:true;text:"Perusinsuliinin annostus on ollut vakaa profiilin tasaantumisjakson ajan" }
                        ReviewCheckBox { objectName:"episodeBasalComplete";id:basalComplete;visible:true;text:"Kaikki tasaantumisjakson perusinsuliiniannokset on kirjattu ja toteutuminen tarkistettu" }
                        ReviewCheckBox { objectName:"episodeV2";id:responseV2;text:"Käytä vastearvion mediaanimenetelmää (v2)";onCheckedChanged:window.resetEpisodeReview() }
                        ReviewCheckBox { objectName:"episodeEndStable";id:endStable;visible:responseV2.checked;text:"Vaikutus on päättynyt ja loppuvaste on vakaa; ei tunnettua jatkuvaa imeytymistä" }
                        Button {
                            objectName:"saveReviewedEpisode"
                            text:"Tallenna tarkistettu jakso"
                            enabled:!studio.busy
                            onClicked:window.saveReviewedEpisode()
                        }
                        Label { objectName:"episodeSaveResult";Layout.fillWidth:true;wrapMode:Text.WordWrap;visible:window.episodeResult.length>0;text:window.episodeResult.split("\n").slice(0,2).join("\n") }
                        Button {objectName:"episodeOpenProfile";visible:window.episodeNeedsProfile;text:"Täydennä hoitoprofiili";onClicked:{tabs.currentIndex=4;settingsTabs.currentIndex=1}}
                        Disclosure {Layout.fillWidth:true;title:"Kaikki tarkistettavat kohdat";visible:window.episodeResult.split("\n").length>2;Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:window.episodeResult}}
                        Disclosure {
                            objectName:"legacyAssessment"
                            Layout.fillWidth:true;title:"Aiempi arviointimenetelmä (v1)"
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
                        }
                        Disclosure {
                            id:savedEpisodes;objectName:"savedEpisodes"
                            Layout.fillWidth:true;title:"Tallennetut jaksot ("+studio.episodes.length+")"
                        Repeater {
                            model:studio.episodes
                            delegate: Label { required property var modelData;Layout.fillWidth:true;wrapMode:Text.WordWrap;text:modelData.startText+" · "+modelData.kind+" · "+modelData.slot+" · profiili "+modelData.profileVersion+(modelData.stale ? " · lähdetieto muuttunut, tarkistettava uudelleen" : "") }
                        }
                        }
                    }
                }
                ColumnLayout {
                    TabBar { id:settingsTabs;Layout.fillWidth:true;TabButton{text:"Tuonti ja raporttiasetukset"}TabButton{text:"Historiallinen hoitoprofiili"} }
                    StackLayout {
                        Layout.fillWidth:true;Layout.fillHeight:true;currentIndex:settingsTabs.currentIndex
                        ScrollView {
                            contentWidth:availableWidth
                            ColumnLayout {
                                width:parent.width;spacing:12
                                Kirigami.Heading { text:"Tietojen tuonti";level:2 }
                                Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Kytke puhelin USB:llä ja tuo uudet tiedot. Tuonnin jälkeen voit irrottaa puhelimen." }
                                RowLayout {
                                    Button { text:"Tuo Jugglucosta";enabled:!studio.busy&&!demoMode;onClicked:studio.sync() }
                                    Button { text:"Tarkista koko historia";enabled:!studio.busy&&!demoMode;onClicked:studio.syncAll() }
                                    Button { text:"Tuo TSV-kansiosta…";enabled:!studio.busy&&!demoMode;onClicked:studio.chooseImportFolder() }
                                    Button { text:"Varmuuskopio";onClicked:studio.backup() }
                                }
                                Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Koko historian tarkistus hakee myös vanhat muutokset. TSV-tuonti tukee Jugglucon alkuperäisiä vientitiedostoja." }
                                Disclosure {
                                    objectName:"connectionDetails"
                                    Layout.fillWidth:true;title:"Tietolähde ja yhteys"
                                    ConnectionEditor { Layout.fillWidth:true; backend:studio }
                                }
                                Disclosure {
                                    Layout.fillWidth:true;title:"Seuranta- ja tutkimusasetukset"
                                GridLayout {
                                    Layout.fillWidth:true;columns:2
                                    Label {text:"Aikavyöhyke"}
                                    TextField {id:zone;Layout.fillWidth:true;text:studio.analysisSettings.timezone}
                                    Label {text:"Tavoitealueen alaraja (mmol/l)"}
                                    TextField {id:low;Layout.fillWidth:true;text:studio.analysisSettings.low===undefined?"":studio.analysisSettings.low}
                                    Label {text:"Tavoitealueen yläraja (mmol/l)"}
                                    TextField {id:high;Layout.fillWidth:true;text:studio.analysisSettings.high===undefined?"":studio.analysisSettings.high}
                                    Label {text:"Tutkimuskäyrien katkosraja (min)"}
                                    TextField {id:gap;Layout.fillWidth:true;text:studio.analysisSettings.maxGapMinutes}
                                }
                                Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Glukoosin yhteenveto ja vuorokausiprofiili käyttävät aina streamia ja sen aukkoihin sensorihistoriaa, 20 minuutin teknisellä enimmäisrajalla. Katkosraja ja täydennysvalinta koskevat erillisiä tutkimusanalyysejä ja päiväkäyriä." }
                                ReviewCheckBox { id:fill;checked:studio.analysisSettings.fillHistory;text:"Täydennä tutkimuskäyrien aukot sensorihistorialla" }
                                Button { text:"Tallenna raporttiasetukset";onClicked:studio.saveAnalysisSettings({timezone:zone.text,low:window.decimal(low.text),high:window.decimal(high.text),maxGapMinutes:window.decimal(gap.text),fillHistory:fill.checked}) }
                                Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Tavoitealueelle ei aseteta hiljaisia hoito-oletuksia. 20 minuutin oletuskatkosraja on tekninen kuvaus-/laskentaraja. Katettuna pidetään vain havaintovälin tukema aika, ei koko pitkää katkosta." }
                                }
                                Disclosure {
                                    objectName:"researchTargetSettings";Layout.fillWidth:true;title:"Automaattisen tutkimusanalyysin rajat"
                                    summary:"Aiemman ateria- ja yömuutosanalyysin asetukset"
                                    onExpandedChanged:if(expanded){let s=studio.automaticSettings;goalLow.text=s.targetLow>0?s.targetLow.toString():"";goalHigh.text=s.targetHigh>0?s.targetHigh.toString():"";hypo.text=s.hypo>0?s.hypo.toString():""}
                                    Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Nämä rajat koskevat tutkimustilan ateria- ja yömuutosanalyysiä. Ne eivät muuta öiden keskiarvokorttia tai vuorokausianalyysin keskiarvoja."}
                                    Label {text:"Tavoitealue (mmol/l)"}
                                    RowLayout {
                                        TextField {id:goalLow;objectName:"automaticTargetLow";placeholderText:"Alaraja";Layout.fillWidth:true;Accessible.name:"Tutkimusanalyysin alaraja (mmol/l)"}
                                        Label {text:"–"}
                                        TextField {id:goalHigh;objectName:"automaticTargetHigh";placeholderText:"Yläraja";Layout.fillWidth:true;Accessible.name:"Tutkimusanalyysin yläraja (mmol/l)"}
                                    }
                                    Label {text:"Matalan raja: alle (mmol/l)"}
                                    TextField {id:hypo;objectName:"automaticHypo";Layout.fillWidth:true;Accessible.name:"Tutkimusanalyysin matalan raja (mmol/l)"}
                                    Button {text:"Tallenna tutkimusrajat";objectName:"saveAutomaticTargets";onClicked:studio.saveAutomaticTargets(window.decimal(goalLow.text),window.decimal(goalHigh.text),window.decimal(hypo.text))}
                                }
                                Disclosure {
                                    Layout.fillWidth:true;title:"Merkintöjen luokitus"
                                Repeater {
                                    model:studio.labels
                                    delegate:RowLayout {
                                        required property var modelData
                                        Label { text:modelData.label;Layout.fillWidth:true }
                                        ComboBox { id:kind;model:["Ei luokitusta","Hiilihydraatit","Pikainsuliini","Perusinsuliini","Korjausinsuliini","Muu"];currentIndex:Math.max(0,["unknown","carbs","rapid","basal","correction","other"].indexOf(modelData.kind)) }
                                        TextField { id:medicine;placeholderText:"Insuliinivalmiste";text:modelData.medicine }
                                        Button { text:"Vahvista luokitus";onClicked:studio.mapLabel(modelData.label,["unknown","carbs","rapid","basal","correction","other"][kind.currentIndex],medicine.text) }
                                    }
                                }
                                Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Luokan nimi ei yksin määritä lääketieteellistä merkitystä. Jugglucon luokkien järjestely voi muuttaa myös vanhojen tapahtumien tulkintaa. Luokitusmuutos mitätöi siitä riippuvat aiemmat arviot." }
                                }
                                Disclosure {
                                    Layout.fillWidth:true;title:"Tuontihistoria"
                                Repeater { model:studio.importHistory;delegate:Label {required property var modelData;Layout.fillWidth:true;wrapMode:Text.WordWrap;text:window.stamp(modelData.started)+" · "+({usb:"USB",network:"Verkko","files-additive":"Tiedostot"})[modelData.transport]+" · "+({complete:"Valmis",partial:"Kesken",running:"Käynnissä"})[modelData.status]+" · "+modelData.chunks+" osaa"+(modelData.status==="partial" ? " · "+(modelData.error || "") : "") } }
                                }
                            }
                        }
                        ColumnLayout {
                            ComboBox { model:studio.profileVersions;textRole:"name";onActivated:studio.selectProfile(studio.profileVersions[currentIndex].version) }
                            ProfileEditor { Layout.fillWidth:true;Layout.fillHeight:true;historicalMode:true }
                        }
                    }
                }
                ResponsePanel { Layout.fillWidth:true;Layout.fillHeight:true;onOpenProfile:{tabs.currentIndex=4;settingsTabs.currentIndex=1}onSelectCandidate:entry=>{episodeKind.currentIndex=entry.kind==="icr"?0:1;episodeDate.text=entry.startText;let ids={};for(let id of entry.linkedIds)ids[id]=true;window.selectedIds=ids;responseV2.checked=true;reviewed.checked=false;complete.checked=false;contextClear.checked=false;stable.checked=false;basalComplete.checked=false;endStable.checked=false;tabs.currentIndex=3} }
            }
        }
    }
}
