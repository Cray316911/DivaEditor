// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// List row in the side panels: round gradient badge, title and a small grey line.
Rectangle {
    id: card
    property string glyph
    property string title
    property string subtitle
    property bool highlighted: false
    property color from: Theme.neonCyan
    property color to: Theme.accentHot
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: 54
    radius: 12
    color: highlighted ? Theme.railSelected : hover.hovered ? Theme.panelHover : Theme.panelHi
    border.color: highlighted ? "#6d62c9" : "transparent"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10
        Rectangle {
            width: 34; height: 34; radius: 17
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: card.from }
                GradientStop { position: 1; color: card.to }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 30; height: 30; radius: 15
                color: "#262850"
                Glyph { anchors.centerIn: parent; text: card.glyph; font.pixelSize: 13; color: "white" }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: card.title
                color: Theme.textMain
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: card.subtitle !== ""
                text: card.subtitle
                color: Theme.textDim
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }
    HoverHandler { id: hover }
    TapHandler { onTapped: card.clicked() }
}
