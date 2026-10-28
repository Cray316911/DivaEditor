// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// Text button in the DivaStudio style: "accent" (neon gradient), "record" or plain.
Rectangle {
    id: btn
    property string text
    property string glyph
    property string kind: "plain"
    signal clicked()

    implicitWidth: row.implicitWidth + 32
    implicitHeight: 36
    radius: 10
    opacity: enabled ? 1 : 0.45
    color: kind === "record" ? (hover.hovered ? "#4a1f3e" : "#3a1a35")
           : kind === "accent" ? Theme.accent
           : (hover.hovered ? Theme.panelHover : Theme.panelHi)
    border.color: kind === "record" ? "#7a2a55" : kind === "plain" && hover.hovered ? Theme.line : "transparent"
    gradient: kind === "accent" ? accentGradient : null

    Gradient {
        id: accentGradient
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: hover.hovered ? "#ff6be0" : Theme.accentHot }
        GradientStop { position: 1; color: hover.hovered ? "#9d74ff" : Theme.accent }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Rectangle {
            visible: btn.kind === "record"
            width: 11; height: 11; radius: 6
            color: Theme.record
        }
        Glyph {
            visible: btn.glyph !== ""
            text: btn.glyph
            color: btn.kind === "accent" ? "white" : Theme.textMain
            font.pixelSize: 14
        }
        Text {
            text: btn.text
            color: btn.kind === "accent" ? "white" : Theme.textMain
            font.pixelSize: 14
            font.bold: true
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: if (btn.enabled) btn.clicked() }
}
