// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A compact colour legend: items = [{color: "#rrggbb", text: "...", shape: "square"|"line"}]
Flow {
    id: root
    property var items: []
    spacing: 14
    Repeater {
        model: root.items
        delegate: RowLayout {
            required property var modelData
            spacing: 5
            Rectangle {
                Layout.preferredWidth: 12; Layout.preferredHeight: 12; radius: modelData.shape === "square" ? 2 : 6
                color: modelData.color; Layout.alignment: Qt.AlignVCenter
            }
            Label { text: modelData.text; opacity: 0.85; Layout.alignment: Qt.AlignVCenter; wrapMode: Text.WordWrap }
        }
    }
}
