// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

CheckBox {
    id: root
    Layout.fillWidth: true
    contentItem: Label {
        text: root.text
        font: root.font
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator ? root.indicator.width + root.spacing : 0
    }
}
