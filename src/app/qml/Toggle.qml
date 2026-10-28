// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: toggle
    property string text
    property string detail
    property bool checked
    signal toggled(bool checked)
    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 1
        Text { text: toggle.text; color: Theme.textMain; font.pixelSize: 13 }
        Text {
            visible: toggle.detail !== ""
            text: toggle.detail
            color: Theme.textDim
            font.pixelSize: 11
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }
    Item { Layout.fillWidth: true }
    Rectangle {
        width: 40; height: 22; radius: 11
        color: Theme.line
        gradient: toggle.checked ? onGradient : null
        Gradient {
            id: onGradient
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.neonCyan }
            GradientStop { position: 1; color: Theme.accent }
        }
        Rectangle {
            x: toggle.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 16; height: 16; radius: 8
            color: "white"
            Behavior on x { NumberAnimation { duration: 120 } }
        }
        TapHandler { onTapped: { toggle.checked = !toggle.checked; toggle.toggled(toggle.checked) } }
    }
}
