// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GlucoidStudio

RowLayout {
    id: selector
    required property Backend studio
    Layout.fillWidth: true
    Label {
        text: qsTranslate("Studio", "Kieli")
    }
    ComboBox {
        objectName: "languageSelector"
        Layout.fillWidth: true
        textRole: "label"
        valueRole: "value"
        model: [{label: "English", value: "en"}, {label: "Suomi", value: "fi"}]
        currentIndex: selector.studio.language === "fi" ? 1 : 0
        onActivated: selector.studio.setLanguage(currentValue)
        Accessible.name: qsTranslate("Studio", "Kieli")
    }
}
