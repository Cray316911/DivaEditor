// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import DivaStudio

// Floating control while recording. Kept out of the recording itself.
Window {
    id: bar
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"
    width: 430
    height: 58
    visible: RecorderController.recording

    function placeOn(monitorName) {
        for (const s of Qt.application.screens) {
            if (s.name === monitorName) {
                bar.screen = s
                bar.x = s.virtualX + (s.width - width) / 2
                bar.y = s.virtualY + s.height - height - 70
            }
        }
    }

    onVisibleChanged: if (visible) RecorderController.excludeFromCapture(bar)

    Item {
        anchors.fill: parent
        NeonFrame { anchors.fill: parent; radius: height / 2; fill: "#f5212245" }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 10
            spacing: 12

            Rectangle {
                width: 14; height: 14; radius: 7
                color: RecorderController.paused ? "#ffb84d" : Theme.record
                SequentialAnimation on opacity {
                    running: RecorderController.recording && !RecorderController.paused
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 600 }
                    NumberAnimation { to: 1; duration: 600 }
                }
            }
            ColumnLayout {
                spacing: 0
                Text {
                    text: Theme.clock(RecorderController.elapsed)
                    color: "white"
                    font.pixelSize: 18
                    font.bold: true
                    font.family: "Consolas"
                }
                Text {
                    text: RecorderController.paused ? "Duraklatıldı" : "Kaydediliyor"
                    color: Theme.textDim
                    font.pixelSize: 10
                }
            }
            Item { Layout.fillWidth: true }
            Text { text: "F9 / F10"; color: Theme.textDim; font.pixelSize: 11; font.family: "Consolas" }
            Rectangle {
                width: 96; height: 38; radius: 19
                color: pauseHover.hovered ? Theme.panelHi : "#2a2a33"
                Text {
                    anchors.centerIn: parent
                    text: RecorderController.paused ? "Devam" : "Duraklat"
                    color: Theme.textMain
                    font.pixelSize: 13
                    font.bold: true
                }
                HoverHandler { id: pauseHover }
                TapHandler { onTapped: RecorderController.togglePause() }
            }
            Rectangle {
                width: 96; height: 38; radius: 19
                color: stopHover.hovered ? "#ff6d88" : Theme.record
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle { width: 10; height: 10; radius: 2; color: "white" }
                    Text { text: "Durdur"; color: "white"; font.pixelSize: 13; font.bold: true }
                }
                HoverHandler { id: stopHover }
                TapHandler { onTapped: RecorderController.stop() }
            }
        }
        DragHandler {
            target: null
            onActiveChanged: if (active) bar.startSystemMove()
        }
    }
}
