// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Rounded card with a neon gradient edge (cyan -> violet -> pink). `lit` turns the edge on.
Item {
    id: frame
    property real radius: Theme.radius
    property color fill: Theme.panelHi
    property real edge: 1.5
    property bool lit: true

    Rectangle {
        anchors.fill: parent
        radius: frame.radius
        visible: frame.lit
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.neonCyan }
            GradientStop { position: 0.5; color: Theme.accent }
            GradientStop { position: 1.0; color: Theme.accentHot }
        }
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: frame.lit ? frame.edge : 0
        radius: frame.radius - (frame.lit ? frame.edge : 0)
        color: frame.fill
        border.color: frame.lit ? "transparent" : Theme.line
    }
}
