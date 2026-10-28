// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Segmented choice: options = [{ value, label, enabled? }]
Row {
    id: choice
    property var options: []
    property var value
    signal picked(var value)
    spacing: 4

    Repeater {
        model: choice.options
        delegate: Rectangle {
            required property var modelData
            readonly property bool selected: choice.value === modelData.value
            readonly property bool usable: modelData.enabled === undefined || modelData.enabled
            width: label.implicitWidth + 26
            height: 30
            radius: 15
            opacity: usable ? 1 : 0.35
            color: hover.hovered && usable ? Theme.panelHover : Theme.panelHi
            gradient: selected ? selectedGradient : null
            Gradient {
                id: selectedGradient
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Theme.accentHot }
            }
            Text {
                id: label
                anchors.centerIn: parent
                text: modelData.label
                color: selected ? "white" : Theme.textMain
                font.pixelSize: 13
                font.bold: selected
            }
            HoverHandler { id: hover }
            TapHandler {
                onTapped: if (parent.usable) { choice.value = parent.modelData.value; choice.picked(parent.modelData.value) }
            }
        }
    }
}
