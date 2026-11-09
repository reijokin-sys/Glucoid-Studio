// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import "theme.js" as Theme
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root
    property string title: ""
    property string value: "—"
    property string unit: ""
    property string note: ""
    // Optional 0–1 share drawn as a bar under the value (NaN hides the bar).
    property real fraction: NaN
    property color accent: Theme.accentTeal
    property color valueColor: Theme.text
    padding: 12
    contentItem: ColumnLayout {
        spacing: 3
        Label {
            text: root.title
            opacity: 0.75
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            text: root.value + (root.unit.length ? " " + root.unit : "")
            font.pixelSize: 24
            font.bold: true
            color: root.valueColor
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Rectangle {
            visible: !isNaN(root.fraction)
            Layout.fillWidth: true
            Layout.topMargin: 3
            Layout.preferredHeight: 5
            radius: 3
            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.18)
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, root.fraction))
                height: parent.height
                radius: 3
                color: root.accent
            }
        }
        Label {
            text: root.note
            visible: text.length > 0
            opacity: 0.75
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }
}
