// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// DivaStudio look: deep indigo surfaces, a lavender navigation rail, rounded glass cards and
// neon cyan -> violet -> pink accents.
QtObject {
    readonly property color bg: "#181930"
    readonly property color canvas: "#121327"
    readonly property color panel: "#212245"
    readonly property color panelSoft: "#1d1e3c"
    readonly property color panelHi: "#2c2d57"
    readonly property color panelHover: "#363770"
    readonly property color line: "#34366a"
    readonly property color textMain: "#f3f2ff"
    readonly property color textDim: "#a6a5cf"
    readonly property color accent: "#8b5cf6"
    readonly property color accentHot: "#ff4fd8"
    readonly property color neonCyan: "#38e1ff"
    readonly property color record: "#ff4d6d"
    readonly property color railTop: "#8b80c9"
    readonly property color railBottom: "#5a4f9c"
    readonly property color railSelected: "#3d3678"
    readonly property int radius: 12
    readonly property string icons: "Segoe Fluent Icons"

    function pad(n) { return n < 10 ? "0" + n : "" + n }
    function timecode(sec) {
        if (!(sec > 0)) sec = 0
        const m = Math.floor(sec / 60), s = Math.floor(sec % 60), f = Math.floor((sec % 1) * 100)
        return pad(m) + ":" + pad(s) + "." + pad(f)
    }
    function clock(sec) {
        if (!(sec > 0)) sec = 0
        const h = Math.floor(sec / 3600), m = Math.floor(sec % 3600 / 60), s = Math.floor(sec % 60)
        return (h > 0 ? h + ":" : "") + pad(m) + ":" + pad(s)
    }
    function fileName(path) {
        const s = path.toString()
        return decodeURIComponent(s.substring(Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\")) + 1))
    }
}
