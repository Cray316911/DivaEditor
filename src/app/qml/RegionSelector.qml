// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window

// Full-screen overlay on one monitor: drag to choose the recording area.
Window {
    id: selector
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    property var monitor: null
    property real dpr: 1
    property rect area: Qt.rect(0, 0, 0, 0) // logical pixels
    signal chosen(var region)              // monitor pixels
    signal canceled()

    function openOn(m) {
        monitor = m
        area = Qt.rect(0, 0, 0, 0)
        for (const s of Qt.application.screens) {
            if (s.name === m.name) {
                selector.screen = s
                selector.x = s.virtualX
                selector.y = s.virtualY
                selector.width = s.width
                selector.height = s.height
                dpr = s.devicePixelRatio
            }
        }
        showFullScreen()
        requestActivate()
    }

    function confirm() {
        if (area.width < 32 || area.height < 32)
            return
        const r = {
            x: Math.round(area.x * dpr), y: Math.round(area.y * dpr),
            width: Math.round(area.width * dpr) & ~1, height: Math.round(area.height * dpr) & ~1
        }
        close()
        chosen(r)
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: { selector.close(); selector.canceled() }
        Keys.onReturnPressed: selector.confirm()
        Keys.onEnterPressed: selector.confirm()

        // Darken everything except the selection
        Rectangle { x: 0; y: 0; width: parent.width; height: selector.area.y; color: "#99000000" }
        Rectangle {
            x: 0; y: selector.area.y + selector.area.height
            width: parent.width; height: parent.height - y
            color: "#99000000"
        }
        Rectangle { x: 0; y: selector.area.y; width: selector.area.x; height: selector.area.height; color: "#99000000" }
        Rectangle {
            x: selector.area.x + selector.area.width; y: selector.area.y
            width: parent.width - x; height: selector.area.height
            color: "#99000000"
        }
        Rectangle {
            x: selector.area.x; y: selector.area.y
            width: selector.area.width; height: selector.area.height
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
            visible: selector.area.width > 0
        }
        Rectangle {
            visible: selector.area.width > 0
            x: selector.area.x
            y: Math.max(0, selector.area.y - 30)
            width: sizeText.implicitWidth + 16
            height: 24
            radius: 5
            color: Theme.accent
            Text {
                id: sizeText
                anchors.centerIn: parent
                text: (Math.round(selector.area.width * selector.dpr) & ~1) + " × "
                      + (Math.round(selector.area.height * selector.dpr) & ~1)
                color: "white"
                font.pixelSize: 13
                font.bold: true
                font.family: "Consolas"
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 40
            width: hint.implicitWidth + 28
            height: 40
            radius: 20
            color: "#ee1b1b22"
            border.color: Theme.line
            Text {
                id: hint
                anchors.centerIn: parent
                text: "Kaydedilecek alanı sürükleyerek seçin  ·  Enter: onayla  ·  Esc: iptal"
                color: Theme.textMain
                font.pixelSize: 14
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.CrossCursor
            property point origin
            onPressed: (m) => { origin = Qt.point(m.x, m.y); selector.area = Qt.rect(m.x, m.y, 0, 0) }
            onPositionChanged: (m) => {
                selector.area = Qt.rect(Math.min(origin.x, m.x), Math.min(origin.y, m.y),
                                        Math.abs(m.x - origin.x), Math.abs(m.y - origin.y))
            }
            onDoubleClicked: selector.confirm()
            onReleased: if (selector.area.width >= 32 && selector.area.height >= 32) confirmButton.visible = true
        }

        Rectangle {
            id: confirmButton
            visible: false
            x: Math.min(selector.area.x + selector.area.width - width, parent.width - width - 8)
            y: Math.min(selector.area.y + selector.area.height + 10, parent.height - height - 8)
            width: 150; height: 36; radius: 8
            color: Theme.accent
            Text { anchors.centerIn: parent; text: "Bu alanı kaydet"; color: "white"; font.bold: true; font.pixelSize: 13 }
            TapHandler { onTapped: selector.confirm() }
        }
    }
    onVisibleChanged: if (!visible) confirmButton.visible = false
}
