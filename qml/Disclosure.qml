// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
    property string title: ""
    property string summary: ""
    property bool expanded: false
    default property alias sectionContent: body.data
    function toggle() { expanded = !expanded }
    padding: 10
    contentItem: ColumnLayout {
        spacing: root.expanded ? 10 : 0
        ToolButton {
            id:header
            Layout.fillWidth: true
            checkable: true
            checked: root.expanded
            onClicked: root.toggle()
            // Keep native Plasma styles from painting a second copy of the title.
            text: ""
            contentItem: ColumnLayout {
                Label { text: (root.expanded ? "▾  " : "▸  ") + root.title; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Label { text: root.summary; visible: text.length > 0 && !root.expanded; opacity: 0.7; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            Accessible.name: root.title
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            visible: root.expanded
            spacing: 10
        }
    }
}
