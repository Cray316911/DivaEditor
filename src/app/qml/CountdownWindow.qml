// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window

// 3-2-1 on the monitor about to be recorded.
Window {
    id: countdown
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowTransparentForInput
    color: "transparent"
    width: 220
    height: 220

    property int remaining: 3
    signal done()

    function begin(monitorName) {
        for (const s of Qt.application.screens) {
            if (s.name === monitorName) {
                countdown.screen = s
                countdown.x = s.virtualX + (s.width - width) / 2
                countdown.y = s.virtualY + (s.height - height) / 2
            }
        }
        remaining = 3
        show()
        tick.start()
    }

    Timer {
        id: tick
        interval: 1000
        repeat: true
        onTriggered: {
            if (--countdown.remaining <= 0) {
                stop()
                countdown.close()
                countdown.done()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "#dd1b1b22"
        border.color: Theme.record
        border.width: 4
        Text {
            anchors.centerIn: parent
            text: countdown.remaining
            color: "white"
            font.pixelSize: 110
            font.bold: true
        }
    }
}
