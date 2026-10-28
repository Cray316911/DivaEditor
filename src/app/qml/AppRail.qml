// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Slim app rail on the far left: round gradient buttons for the main places of the app.
Rectangle {
    id: rail
    property string current: "editor" // home | editor | ai
    signal homeRequested()
    signal editorRequested()
    signal recordRequested()
    signal aiRequested()
    signal newRequested()

    implicitWidth: 74
    gradient: Gradient {
        GradientStop { position: 0; color: "#6a5fb3" }
        GradientStop { position: 1; color: "#3f3683" }
    }

    component RailButton: Item {
        id: rb
        property string glyph
        property string tip
        property bool active: false
        property bool symbol: false
        property bool record: false
        property bool outline: false
        property color from: Theme.neonCyan
        property color to: Theme.accent
        signal clicked()
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 74
        implicitHeight: 52

        // Active marker hugging the left edge, like a tab notch
        Rectangle {
            visible: rb.active
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: 4; height: 30; radius: 2
            color: "white"
        }
        Rectangle {
            id: bubble
            anchors.centerIn: parent
            width: 42; height: 42; radius: 21
            color: rb.outline ? "transparent" : "#2d2766"
            border.color: rb.outline ? "#b9b0ee" : rb.active ? "white" : "transparent"
            border.width: rb.outline ? 1.5 : 2
            gradient: rb.outline ? null : fill
            scale: hover.hovered ? 1.08 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }
            Gradient {
                id: fill
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: rb.from }
                GradientStop { position: 1; color: rb.to }
            }
            Rectangle {
                visible: rb.record
                anchors.centerIn: parent
                width: 14; height: 14; radius: 7
                color: "white"
            }
            Text {
                visible: !rb.record
                anchors.centerIn: parent
                text: rb.glyph
                color: "white"
                font.family: rb.symbol ? "Segoe UI Symbol" : Theme.icons
                font.pixelSize: rb.symbol ? 18 : 17
            }
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: rb.clicked() }
        ToolTip.visible: hover.hovered && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 400
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 18
        anchors.bottomMargin: 18
        spacing: 6

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "DIVA"
            color: "white"
            font.pixelSize: 14
            font.weight: Font.Bold
            font.letterSpacing: 2
        }
        Item { Layout.preferredHeight: 18 }

        RailButton {
            glyph: ""
            tip: "Başlangıç"
            active: rail.current === "home"
            from: "#5b8cff"; to: "#8b5cf6"
            onClicked: rail.homeRequested()
        }
        RailButton {
            glyph: ""
            tip: "Editör"
            active: rail.current === "editor"
            from: "#8b5cf6"; to: "#ff4fd8"
            onClicked: rail.editorRequested()
        }
        RailButton {
            record: true
            tip: "Yeni kayıt (Ctrl+R)"
            from: "#ff4d6d"; to: "#ff4fd8"
            onClicked: rail.recordRequested()
        }
        RailButton {
            glyph: "✦"
            symbol: true
            tip: "DivaAI"
            active: rail.current === "ai"
            from: Theme.neonCyan; to: "#5b8cff"
            onClicked: rail.aiRequested()
        }
        Item { Layout.fillHeight: true }
        RailButton {
            glyph: ""
            tip: "Yeni proje (Ctrl+N)"
            outline: true
            onClicked: rail.newRequested()
        }
    }
}
