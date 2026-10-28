// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: btn
    property string glyph
    property int size: 32
    property color tint: Theme.textMain
    property string tip
    signal clicked()

    width: size
    height: size
    radius: 6
    opacity: enabled ? 1 : 0.4
    color: hover.hovered && enabled ? Theme.panelHi : "transparent"

    Glyph { anchors.centerIn: parent; text: btn.glyph; color: btn.tint; font.pixelSize: btn.size * 0.48 }
    HoverHandler { id: hover }
    TapHandler { onTapped: if (btn.enabled) btn.clicked() }
    ToolTip.visible: tip !== "" && hover.hovered
    ToolTip.text: tip
    ToolTip.delay: 500
}
