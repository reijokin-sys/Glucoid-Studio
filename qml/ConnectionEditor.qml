// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var backend
    readonly property bool nightscout: sourceChoice.currentIndex === 1
    readonly property string chosenSource: nightscout ? "nightscout" : "juggluco"
    spacing: 8

    function loadSelection() {
        const saved = editor.backend.savedConnection(editor.chosenSource)
        mode.currentIndex = saved.mode === "network" || saved.mode === "wireguard" ? 1 : 0
        address.text = saved.address || ""
        serial.text = saved.serial || ""
        tunnelInterface.text = saved.wireguardInterface || "wg0"
        auth.currentIndex = Math.max(0, ["apisecret", "token", "none"].indexOf(saved.auth))
        credential.text = ""
    }
    function loadActive() {
        sourceChoice.currentIndex = editor.backend.connection.source === "nightscout" ? 1 : 0
        loadSelection()
    }
    Component.onCompleted: loadActive()
    onVisibleChanged: if (visible && editor.backend) Qt.callLater(editor.loadActive)

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.bold: true
        text: "Aktiivinen lähde: " + (editor.backend.connection.source === "nightscout"
            ? "Nightscout" : "Juggluco")
    }
    Label { text: "Mistä tiedot tuodaan" }
    ComboBox {
        id: sourceChoice
        objectName: "connectionSource"
        Layout.fillWidth: true
        model: ["Juggluco", "Nightscout"]
        onActivated: editor.loadSelection()
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: editor.nightscout
            ? "HTTPS-yhteys omaan Nightscout-palvelimeesi. WireGuardia ei tarvita. Lukutunnuksella tulee olla lukuoikeus glukoosiin ja kirjauksiin."
            : "Jugglucon omat glukoosi- ja merkintäviennit USB:n kautta tai WireGuard-verkkoyhteydellä. Verkkohaku edellyttää toimivaa WireGuard-tunnelia."
    }
    Label { text: "Tuontitapa"; visible: !editor.nightscout }
    ComboBox {
        id: mode
        objectName: "sourceMode"
        visible: !editor.nightscout
        Layout.fillWidth: true
        model: ["USB / ADB", "Verkko / WireGuard (vaaditaan)"]
    }
    Label { text: "Palvelimen osoite"; visible: editor.nightscout || mode.currentIndex === 1 }
    TextField {
        id: address
        objectName: "connectionAddress"
        Layout.fillWidth: true
        visible: editor.nightscout || mode.currentIndex === 1
        placeholderText: editor.nightscout ? "https://nightscout.example" : "Jugglucon palvelinosoite"
        inputMethodHints: Qt.ImhUrlCharactersOnly
    }
    Label { text: "WireGuard-liittymä"; visible: !editor.nightscout && mode.currentIndex === 1 }
    TextField {
        id: tunnelInterface
        objectName: "wireguardInterface"
        Layout.fillWidth: true
        visible: !editor.nightscout && mode.currentIndex === 1
        placeholderText: "wg0"
        maximumLength: 15
    }
    Label { text: "Tunnistautuminen"; visible: !editor.nightscout }
    ComboBox {
        id: auth
        objectName: "sourceAuth"
        Layout.fillWidth: true
        visible: !editor.nightscout
        model: ["API-salaisuus", "Tunnus", "Ei tunnistautumista"]
    }
    TextField {
        id: credential
        objectName: "sourceCredential"
        Layout.fillWidth: true
        visible: editor.nightscout || auth.currentIndex !== 2
        echoMode: TextInput.Password
        placeholderText: editor.nightscout
            ? "Uusi Nightscout-lukutunnus; tyhjä säilyttää tallennetun"
            : "Uusi tunnus; tyhjä säilyttää tallennetun"
    }
    TextField {
        id: serial
        objectName: "sourceSerial"
        Layout.fillWidth: true
        visible: !editor.nightscout && mode.currentIndex === 0
        placeholderText: "ADB-laite (valinnainen)"
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: "Yksi lähde on aktiivinen kerrallaan. Nightscoutin ja Jugglucon historiat säilyvät erillään; lähteeseen voi palata myöhemmin."
    }
    Button {
        objectName: "saveSourceConnection"
        text: "Tallenna tietolähde"
        enabled: !editor.backend.busy && !editor.backend.automaticBusy
        onClicked: {
            const saved = editor.backend.saveSourceConnection(editor.chosenSource,
                editor.nightscout ? "network" : (mode.currentIndex === 0 ? "usb" : "wireguard"),
                address.text, editor.nightscout ? "token" : ["apisecret", "token", "none"][auth.currentIndex],
                credential.text, serial.text, editor.nightscout ? "" : tunnelInterface.text)
            if (saved) editor.loadActive()
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        Button { objectName: "syncSelectedSource"; text: "Päivitä aktiivinen lähde"; enabled: !editor.backend.busy; onClicked: editor.backend.sync() }
        Button { objectName: "fullSyncSelectedSource"; text: "Tarkista koko historia"; enabled: !editor.backend.busy; onClicked: editor.backend.syncAll() }
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        visible: editor.backend.connection.source === "nightscout" && editor.backend.connection.unsupportedEvents > 0
        text: "Nightscoutissa on " + (editor.backend.connection.unsupportedEvents || 0)
            + " tapahtumaa, joiden annostyyppiä ei tulkittu. Esimerkiksi pumpun perusnopeuksia ja jatkettuja boluksia ei muuteta pistosannoksiksi. Alkuperäiset tiedot säilyvät."
    }
    Disclosure {
        objectName: "glucoidConnectionImport"
        Layout.fillWidth: true
        title: "Kopioi yhteys Glucoid-widgetistä"
        visible: !editor.nightscout
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Jos käytät Glucoid-widgetiä, voit korvata Studion Juggluco-yhteysasetukset sen asetuksilla."
        }
        Button {
            text: "Kopioi yhteysasetukset"
            enabled: !editor.backend.busy && !editor.backend.automaticBusy
            onClicked: { editor.backend.importGlucoid(); editor.loadActive() }
        }
    }
}
