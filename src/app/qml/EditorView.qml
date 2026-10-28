// SPDX-License-Identifier: GPL-3.0-or-later
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DivaStudio

Rectangle {
    id: root
    color: Theme.bg

    signal recordRequested()
    signal exportRequested()
    signal newRequested()
    signal openRequested()
    signal importRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal homeRequested()
    signal applyEditsRequested()

    property int category: 1
    property int selectedClip: -1
    property real zoom: 1.0
    readonly property alias video: video

    readonly property color bg: Theme.bg
    readonly property color panel: Theme.panel
    readonly property color panelHi: Theme.panelHi
    readonly property color line: Theme.line
    readonly property color textMain: Theme.textMain
    readonly property color textDim: Theme.textDim
    readonly property color accent: Theme.accent
    readonly property color accentHot: Theme.accentHot

    readonly property var categories: [
        { glyph: "✦", name: "DivaAI", symbol: true },
        { glyph: "", name: "Medya" },
        { glyph: "", name: "Açıklamalar" },
        { glyph: "", name: "Geçişler" },
        { glyph: "", name: "Davranışlar" },
        { glyph: "", name: "Animasyonlar" },
        { glyph: "", name: "İmleç Efektleri" },
        { glyph: "", name: "Ses Efektleri" },
        { glyph: "", name: "Görsel Efektler" }
    ]
    readonly property var upcoming: ({
        4: ["Süzülme", "Zıplama", "Yazı makinesi", "Nabız", "Kayarak gel", "Titreşim"],
        5: ["Odak Zoom", "Kaydır", "Ölçekle", "Opaklık", "Özel"],
        6: ["İmleç büyüt", "Vurgula", "Spot ışığı", "Sol tık halkası", "Sağ tık halkası", "Yumuşak imleç"],
        7: ["Gürültü azaltma", "Ses seviyesi eşitle", "Fade in", "Fade out", "Klip hızı", "Ses ducking"],
        8: ["Renk düzeltme", "Gölge", "Kenarlık", "Yeşil perde", "Bulanıklaştır", "Cihaz çerçevesi"]
    })

    readonly property var selected: selectedClip >= 0 && selectedClip < ProjectController.clips.length
                                    ? ProjectController.clips[selectedClip] : null

    function cutTimes() {
        const cuts = [0]
        for (const c of ProjectController.clips)
            if (c.index > 0) cuts.push(c.start)
        cuts.push(ProjectController.duration)
        return cuts
    }
    function seekPrevCut() {
        const t = video.position - 0.2
        let best = 0
        for (const c of cutTimes()) if (c < t && c > best) best = c
        video.seek(best)
    }
    function seekNextCut() {
        const t = video.position + 0.05
        let best = ProjectController.duration
        for (const c of cutTimes()) if (c > t && c < best) best = c
        video.seek(Math.min(best, Math.max(0, ProjectController.duration - 0.05)))
    }

    Connections {
        target: ProjectController
        function onChanged() { if (root.selectedClip >= ProjectController.clips.length) root.selectedClip = -1 }
    }

    // Same keys as the recording dialog, so the quick toggles and the dialog agree.
    Settings {
        id: recordingPrefs
        category: "recording"
        property bool systemAudio: true
        property bool microphone: false
        property bool cursor: true
    }

    Shortcut { sequence: "Space"; onActivated: video.togglePlaying() }
    Shortcut { sequence: "S"; enabled: !ProjectController.locked; onActivated: ProjectController.splitAt(video.position) }
    Shortcut {
        sequences: ["Delete", "Ctrl+Backspace"]
        enabled: root.selectedClip >= 0 && !ProjectController.locked
        onActivated: ProjectController.removeClip(root.selectedClip)
    }
    Shortcut { sequence: "Ctrl+="; onActivated: root.zoom = Math.min(root.zoom * 1.5, 40) }
    Shortcut { sequence: "Ctrl+-"; onActivated: root.zoom = Math.max(root.zoom / 1.5, 1) }

    component Section: Text {
        color: root.textDim
        font.pixelSize: 11
        font.bold: true
        font.letterSpacing: 1.2
    }

    component MenuButton: Rectangle {
        id: mb
        property string text
        default property alias items: menu.contentData
        width: mbText.implicitWidth + 20
        height: 30
        radius: 6
        color: mbHover.hovered || menu.visible ? root.panelHi : "transparent"
        Text { id: mbText; anchors.centerIn: parent; text: mb.text; color: root.textDim; font.pixelSize: 13 }
        HoverHandler { id: mbHover }
        TapHandler { onTapped: menu.popup(mb, 0, mb.height + 4) }
        Menu {
            id: menu
            background: Rectangle { implicitWidth: 240; color: root.panel; border.color: root.line; radius: 8 }
        }
    }

    component MItem: MenuItem {
        id: mi
        property string shortcutText
        contentItem: RowLayout {
            Text { text: mi.text; color: mi.enabled ? root.textMain : root.textDim; font.pixelSize: 13; Layout.fillWidth: true }
            Text { text: mi.shortcutText; color: root.textDim; font.pixelSize: 11; font.family: "Consolas" }
        }
        background: Rectangle { color: mi.highlighted ? root.panelHi : "transparent"; radius: 4 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------- Header ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: root.panel
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.line }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12

                DButton { text: "Kaydet"; kind: "record"; onClicked: root.recordRequested() }

                Text {
                    text: "Diva<font color='#9d5cff'>Studio</font>"
                    textFormat: Text.StyledText
                    color: root.textMain
                    font.pixelSize: 18
                    font.bold: true
                    TapHandler { onTapped: root.homeRequested() }
                }

                Row {
                    spacing: 2
                    MenuButton {
                        text: "Dosya"
                        MItem { text: "Yeni Proje"; shortcutText: "Ctrl+N"; onTriggered: root.newRequested() }
                        MItem { text: "Proje Aç…"; shortcutText: "Ctrl+O"; onTriggered: root.openRequested() }
                        MItem { text: "Kaydet"; shortcutText: "Ctrl+S"; onTriggered: root.saveRequested() }
                        MItem { text: "Farklı Kaydet…"; shortcutText: "Ctrl+Shift+S"; onTriggered: root.saveAsRequested() }
                        MenuSeparator {}
                        MItem { text: "Video İçe Aktar…"; shortcutText: "Ctrl+I"; onTriggered: root.importRequested() }
                        MItem { text: "Yeni Kayıt…"; shortcutText: "Ctrl+R"; onTriggered: root.recordRequested() }
                        MItem { text: "Dışa Aktar…"; shortcutText: "Ctrl+E"; onTriggered: root.exportRequested() }
                        MenuSeparator {}
                        MItem { text: "Başka projedeki düzenlemeleri uygula…"; onTriggered: root.applyEditsRequested() }
                        MItem { text: "Başlangıç ekranı"; onTriggered: root.homeRequested() }
                    }
                    MenuButton {
                        text: "Düzen"
                        MItem { text: "Geri Al"; shortcutText: "Ctrl+Z"; enabled: ProjectController.canUndo; onTriggered: ProjectController.undo() }
                        MItem { text: "Yinele"; shortcutText: "Ctrl+Y"; enabled: ProjectController.canRedo; onTriggered: ProjectController.redo() }
                        MenuSeparator {}
                        MItem { text: "Oynatma kafasında böl"; shortcutText: "S"; onTriggered: ProjectController.splitAt(video.position) }
                        MItem {
                            text: "Seçili klibi sil (ripple)"
                            shortcutText: "Del"
                            enabled: root.selectedClip >= 0
                            onTriggered: ProjectController.removeClip(root.selectedClip)
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Project pill (centre of the title bar)
                Rectangle {
                    Layout.preferredWidth: Math.min(420, pillRow.implicitWidth + 40)
                    Layout.preferredHeight: 32
                    radius: 16
                    color: Theme.panelSoft
                    border.color: Theme.line
                    RowLayout {
                        id: pillRow
                        anchors.centerIn: parent
                        spacing: 8
                        Glyph { text: ""; color: Theme.neonCyan; font.pixelSize: 13 }
                        Text {
                            text: ProjectController.name
                            color: Theme.textMain
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            Layout.maximumWidth: 320
                        }
                        Rectangle {
                            visible: ProjectController.dirty
                            width: 7; height: 7; radius: 4
                            color: Theme.accentHot
                        }
                    }
                }
                Row {
                    spacing: 2
                    IconButton { glyph: ""; tip: "Geri al (Ctrl+Z)"; enabled: ProjectController.canUndo && !ProjectController.locked; onClicked: ProjectController.undo() }
                    IconButton { glyph: ""; tip: "Yinele (Ctrl+Y)"; enabled: ProjectController.canRedo && !ProjectController.locked; onClicked: ProjectController.redo() }
                }

                Item { Layout.fillWidth: true }

                DButton { text: "Dışa Aktar"; kind: "accent"; glyph: ""; onClicked: root.exportRequested() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---------- Category rail (lavender, selected item as a dark pill) ----------
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 196
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.railTop }
                    GradientStop { position: 1; color: Theme.railBottom }
                }

                // Bottom: neon audio wave + quick recording sound toggles
                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        radius: 14
                        color: "#2e275f"
                        Canvas {
                            id: wave
                            anchors.fill: parent
                            anchors.margins: 8
                            property real phase: 0
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                const g = ctx.createLinearGradient(0, 0, width, 0)
                                g.addColorStop(0, "#38e1ff")
                                g.addColorStop(0.5, "#8b5cf6")
                                g.addColorStop(1, "#ff4fd8")
                                ctx.fillStyle = g
                                const mid = height / 2
                                for (let x = 0; x < width; x += 3) {
                                    const env = Math.sin(Math.PI * x / width)
                                    const a = env * (0.25 + 0.75 * Math.abs(Math.sin(x * 0.21 + phase) * Math.cos(x * 0.047 - phase * 0.5)))
                                    const h = Math.max(2, a * (height - 4))
                                    ctx.fillRect(x, mid - h / 2, 2, h)
                                }
                            }
                            Timer {
                                interval: 90
                                repeat: true
                                running: wave.visible && video.playing
                                onTriggered: { wave.phase += 0.35; wave.requestPaint() }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: [
                                { key: "microphone", glyph: "", tip: "Mikrofonu kaydet" },
                                { key: "systemAudio", glyph: "", tip: "Sistem sesini kaydet" },
                                { key: "cursor", glyph: "", tip: "İmleci kaydet" }
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool on: recordingPrefs[modelData.key]
                                width: 38; height: 38; radius: 19
                                color: on ? "transparent" : "#2e275f"
                                border.color: on ? "transparent" : "#7d72c4"
                                gradient: on ? prefOn : null
                                Gradient {
                                    id: prefOn
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: Theme.neonCyan }
                                    GradientStop { position: 1; color: Theme.accent }
                                }
                                Glyph { anchors.centerIn: parent; text: modelData.glyph; font.pixelSize: 14; color: "white"; opacity: parent.on ? 1 : 0.6 }
                                ToolTip.visible: prefHover.hovered
                                ToolTip.text: modelData.tip + (on ? ": açık" : ": kapalı")
                                HoverHandler { id: prefHover }
                                TapHandler {
                                    onTapped: {
                                        recordingPrefs[parent.modelData.key] = !parent.on
                                        recordingPrefs.sync()
                                    }
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 38; height: 38; radius: 19
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#ff4d6d" }
                                GradientStop { position: 1; color: Theme.accentHot }
                            }
                            Rectangle { anchors.centerIn: parent; width: 12; height: 12; radius: 6; color: "white" }
                            TapHandler { onTapped: root.recordRequested() }
                        }
                    }
                }
                Column {
                    anchors.fill: parent
                    anchors.topMargin: 12
                    spacing: 4
                    Repeater {
                        model: root.categories
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool current: root.category === index
                            x: 10
                            width: parent.width - 20
                            height: 42
                            radius: 10
                            color: current ? Theme.railSelected : navHover.hovered ? "#33ffffff" : "transparent"
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 14
                                spacing: 12
                                Text {
                                    width: 20
                                    text: modelData.glyph
                                    font.family: modelData.symbol ? "Segoe UI Symbol" : Theme.icons
                                    font.pixelSize: 16
                                    horizontalAlignment: Text.AlignHCenter
                                    color: modelData.symbol ? (current ? Theme.neonCyan : "#ffe3fb") : "white"
                                }
                                Text {
                                    text: modelData.name
                                    color: "white"
                                    font.pixelSize: 13
                                    font.bold: current || modelData.symbol === true
                                }
                            }
                            HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.category = index }
                        }
                    }
                }
            }

            // ---------- Asset panel ----------
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 330
                color: Theme.panelSoft
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: root.line }

                DivaAIPanel {
                    visible: root.category === 0
                    anchors.fill: parent
                    anchors.margins: 16
                }

                ColumnLayout {
                    visible: root.category !== 0
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: root.categories[root.category].name
                            color: root.textMain
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: root.category === 1 || root.category === 3
                            text: root.category === 1 ? ProjectController.clips.length + " klip" : "Tümüne uygula"
                            color: root.textDim
                            font.pixelSize: 11
                        }
                    }

                    // Media bin
                    ColumnLayout {
                        visible: root.category === 1
                        Layout.fillWidth: true
                        spacing: 8
                        RowLayout {
                            DButton { text: "İçe Aktar"; glyph: ""; onClicked: root.importRequested() }
                            DButton { text: "Kayıt"; kind: "record"; onClicked: root.recordRequested() }
                        }
                        Repeater {
                            model: ProjectController.clips
                            delegate: InfoCard {
                                required property var modelData
                                glyph: ""
                                from: modelData.index % 2 ? Theme.accent : Theme.neonCyan
                                to: modelData.index % 2 ? Theme.accentHot : Theme.accent
                                title: "Klip " + (modelData.index + 1) + " · " + modelData.name
                                subtitle: "Kaynak " + Theme.timecode(modelData.sourceIn) + " – " + Theme.timecode(modelData.sourceOut)
                                          + (modelData.transition !== "" ? " · " + modelData.transition : "")
                                highlighted: root.selectedClip === modelData.index
                                onClicked: { root.selectedClip = modelData.index; video.seek(modelData.start) }
                            }
                        }
                        Text {
                            visible: ProjectController.clips.length === 0
                            text: "Henüz medya yok. Video içe aktarın, kayıt yapın ya da pencereye sürükleyin."
                            color: root.textDim
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // Callout styles (original DivaStudio designs; editing them comes next)
                    Grid {
                        visible: root.category === 2
                        columns: 2
                        spacing: 10
                        Repeater {
                            model: 8
                            delegate: Rectangle {
                                required property int index
                                width: 138; height: 92; radius: 8
                                color: cardHover.hovered ? Theme.panelHover : root.panelHi
                                HoverHandler { id: cardHover }
                                Rectangle {
                                    visible: index === 0
                                    anchors.centerIn: parent
                                    width: 92; height: 40; radius: 20
                                    color: root.accent
                                    Text { anchors.centerIn: parent; text: "Metin"; color: "white"; font.bold: true; font.pixelSize: 12 }
                                    Rectangle { x: 18; y: 34; width: 12; height: 12; rotation: 45; color: root.accent }
                                }
                                Rectangle {
                                    visible: index === 1
                                    anchors.centerIn: parent
                                    width: 92; height: 38; radius: 3
                                    color: root.accentHot
                                    Text { anchors.centerIn: parent; text: "Metin"; color: "white"; font.bold: true; font.pixelSize: 12 }
                                }
                                Rectangle {
                                    visible: index === 2
                                    anchors.centerIn: parent
                                    width: 96; height: 36; radius: 18
                                    color: "transparent"
                                    border.color: "#5ce1e6"; border.width: 2
                                    Text { anchors.centerIn: parent; text: "Metin"; color: "#5ce1e6"; font.bold: true; font.pixelSize: 12 }
                                }
                                Item {
                                    visible: index === 3
                                    anchors.centerIn: parent
                                    width: 96; height: 24
                                    Rectangle { width: 74; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: "#ffb84d" }
                                    Rectangle { x: 70; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; rotation: 45; color: "#ffb84d" }
                                }
                                Rectangle {
                                    visible: index === 4
                                    anchors.centerIn: parent
                                    width: 96; height: 54; radius: 6
                                    color: "#22ffd84d"
                                    border.color: "#ffd84d"; border.width: 2
                                }
                                Rectangle {
                                    visible: index === 5
                                    anchors.centerIn: parent
                                    width: 44; height: 44; radius: 22
                                    color: root.accent
                                    Text { anchors.centerIn: parent; text: "1"; color: "white"; font.bold: true; font.pixelSize: 20 }
                                }
                                Row {
                                    visible: index === 6
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Repeater {
                                        model: ["Ctrl", "S"]
                                        delegate: Rectangle {
                                            required property string modelData
                                            width: modelData.length > 1 ? 46 : 30; height: 30; radius: 5
                                            color: "#30303b"; border.color: "#4a4a58"
                                            Text { anchors.centerIn: parent; text: modelData; color: root.textMain; font.pixelSize: 12; font.bold: true }
                                        }
                                    }
                                }
                                Text {
                                    visible: index === 7
                                    anchors.centerIn: parent
                                    text: "Başlık"
                                    color: root.textMain
                                    font.pixelSize: 22
                                    font.bold: true
                                }
                            }
                        }
                    }

                    // Transitions: real, applied to the selected cut or to every cut
                    Text {
                        visible: root.category === 3
                        Layout.fillWidth: true
                        text: root.selectedClip >= 1
                              ? "Klip " + (root.selectedClip + 1) + " öncesindeki kesime uygulanır."
                              : "Bir klip seçin ya da tüm kesimlere uygulamak için tıklayın."
                        color: root.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Grid {
                        visible: root.category === 3
                        columns: 2
                        spacing: 10
                        Repeater {
                            model: ProjectController.transitionKinds
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: 138; height: 92; radius: 8
                                color: transHover.hovered ? Theme.panelHover : root.panelHi
                                border.color: transHover.hovered ? root.accent : "transparent"
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: 8
                                    height: 52; radius: 5
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: Qt.hsla(0.72 + index * 0.04, 0.55, 0.45, 1) }
                                        GradientStop { position: 1; color: Qt.hsla(0.92 + index * 0.02, 0.6, 0.55, 1) }
                                    }
                                }
                                Text {
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 9
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.label
                                    color: root.textMain
                                    font.pixelSize: 12
                                }
                                HoverHandler { id: transHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    enabled: !ProjectController.locked
                                    onTapped: {
                                        if (root.selectedClip >= 1)
                                            ProjectController.setTransition(root.selectedClip, modelData.id, 0.5)
                                        else
                                            ProjectController.setTransitionAll(modelData.id, 0.5)
                                    }
                                }
                            }
                        }
                    }

                    // Categories still in development
                    Text {
                        visible: root.category === 2 || root.category >= 4
                        Layout.fillWidth: true
                        text: "Bu bölüm geliştiriliyor; kartlar önizleme amaçlıdır."
                        color: "#ffb84d"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Grid {
                        visible: root.category >= 4
                        columns: 2
                        spacing: 10
                        Repeater {
                            model: root.upcoming[root.category] || []
                            delegate: Rectangle {
                                required property string modelData
                                required property int index
                                width: 138; height: 92; radius: 8
                                color: root.panelHi
                                opacity: 0.7
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: 8
                                    height: 52; radius: 5
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: Qt.hsla(0.72 + index * 0.04, 0.55, 0.45, 1) }
                                        GradientStop { position: 1; color: Qt.hsla(0.92 + index * 0.02, 0.6, 0.55, 1) }
                                    }
                                }
                                Text {
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 9
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData
                                    color: root.textMain
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // ---------- Canvas + transport ----------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.canvas

                    VideoItem {
                        id: video
                        objectName: "canvasVideo"
                        anchors.fill: parent
                        anchors.margins: 22
                        source: ProjectController.previewSource

                        Item {
                            id: picture
                            visible: video.videoSize.width > 0
                            readonly property real s: Math.min(video.width / Math.max(1, video.videoSize.width),
                                                               video.height / Math.max(1, video.videoSize.height))
                            width: video.videoSize.width * s
                            height: video.videoSize.height * s
                            anchors.centerIn: parent
                            Rectangle { anchors.fill: parent; color: "transparent"; border.color: root.accent; border.width: 1 }
                            Repeater {
                                model: 8
                                delegate: Rectangle {
                                    required property int index
                                    readonly property var p: [[0, 0], [0.5, 0], [1, 0], [1, 0.5], [1, 1], [0.5, 1], [0, 1], [0, 0.5]][index]
                                    x: picture.width * p[0] - 5
                                    y: picture.height * p[1] - 5
                                    width: 10; height: 10; radius: 5
                                    color: "white"
                                    border.color: root.accent; border.width: 2
                                }
                            }
                        }
                    }

                    Column {
                        visible: ProjectController.clips.length === 0
                        anchors.centerIn: parent
                        spacing: 10
                        Glyph { anchors.horizontalCenter: parent.horizontalCenter; text: ""; font.pixelSize: 40; color: root.textDim }
                        Text { text: "Bir video dosyasını buraya sürükleyin"; color: root.textDim; font.pixelSize: 15 }
                    }

                    Text {
                        visible: video.errorString !== ""
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: 30
                        text: video.errorString
                        color: "#ff7b8a"
                    }

                    // Performance overlay
                    Rectangle {
                        visible: video.videoSize.width > 0
                        x: 34; y: 34
                        width: perf.implicitWidth + 20
                        height: perf.implicitHeight + 14
                        radius: 6
                        color: "#cc121327"
                        border.color: root.line
                        Column {
                            id: perf
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                text: video.videoSize.width + "×" + video.videoSize.height + " · " + video.frameRate.toFixed(2) + " fps · " + video.codec + " · D3D11VA"
                                color: root.textMain; font.family: "Consolas"; font.pixelSize: 12
                            }
                            Text {
                                text: "Gösterim " + video.displayFps.toFixed(1) + " fps · Düşen kare " + video.droppedFrames + " · Takılma " + video.stalls
                                color: video.droppedFrames > 0 || video.stalls > 0 ? "#ffb84d" : "#6ee7a8"
                                font.family: "Consolas"; font.pixelSize: 12
                            }
                        }
                    }

                    // Preview render progress
                    Rectangle {
                        visible: ProjectController.previewBusy
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 34
                        width: 230; height: 46; radius: 8
                        color: "#ee212245"
                        border.color: root.line
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Text { text: "Önizleme hazırlanıyor… " + Math.round(ProjectController.previewProgress * 100) + "%"; color: root.textMain; font.pixelSize: 12 }
                            Rectangle {
                                width: parent.width; height: 4; radius: 2; color: root.line
                                Rectangle { width: parent.width * ProjectController.previewProgress; height: 4; radius: 2; color: root.accent }
                            }
                        }
                    }

                    // DivaAI at work
                    Rectangle {
                        visible: ProjectController.locked
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: 34
                        width: aiText.implicitWidth + 40
                        height: 38; radius: 19
                        color: "#ee2c2560"
                        border.color: root.accent
                        Text {
                            id: aiText
                            anchors.centerIn: parent
                            text: "✦  DivaAI düzenliyor…"
                            color: root.textMain
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }

                    DropArea {
                        anchors.fill: parent
                        onDropped: (drop) => { if (drop.hasUrls) ProjectController.importMedia(drop.urls[0]) }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    color: root.panel
                    Rectangle { width: parent.width; height: 1; color: root.line }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 10

                        IconButton { glyph: ""; tip: "Önceki kesim"; onClicked: root.seekPrevCut() }
                        Rectangle {
                            width: 44; height: 44; radius: 22
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.neonCyan }
                                GradientStop { position: 0.5; color: Theme.accent }
                                GradientStop { position: 1; color: Theme.accentHot }
                            }
                            Glyph { anchors.centerIn: parent; text: video.playing ? "" : ""; color: "white"; font.pixelSize: 18 }
                            TapHandler { onTapped: video.togglePlaying() }
                        }
                        IconButton { glyph: ""; tip: "Sonraki kesim"; onClicked: root.seekNextCut() }

                        Item {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            height: 16
                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 6; radius: 3; color: root.line }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * (video.duration > 0 ? video.position / video.duration : 0)
                                height: 6; radius: 3
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: Theme.neonCyan }
                                    GradientStop { position: 1; color: Theme.accentHot }
                                }
                            }
                            TapHandler { onTapped: (p) => video.seek(p.position.x / parent.width * video.duration) }
                        }

                        Text {
                            text: Theme.timecode(video.position) + " / " + Theme.timecode(ProjectController.duration)
                            color: root.textMain
                            font.family: "Consolas"
                            font.pixelSize: 14
                        }
                    }
                }
            }

            // ---------- Properties ----------
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 270
                color: root.panel
                Rectangle { width: 1; height: parent.height; color: root.line }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Özellikler"; color: root.textMain; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Text {
                            visible: root.selected !== null
                            text: "Projeye dön"
                            color: root.textDim
                            font.pixelSize: 11
                            TapHandler { onTapped: root.selectedClip = -1 }
                        }
                    }

                    // "Profile" of the selection: neon ring, name, one line of detail
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 168
                        Canvas {
                            id: ring
                            width: 112; height: 112
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 4
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                const g = ctx.createConicalGradient(width / 2, height / 2, 0)
                                g.addColorStop(0.0, "#38e1ff")
                                g.addColorStop(0.33, "#8b5cf6")
                                g.addColorStop(0.66, "#ff4fd8")
                                g.addColorStop(1.0, "#38e1ff")
                                ctx.strokeStyle = g
                                ctx.lineWidth = 4
                                ctx.beginPath()
                                ctx.arc(width / 2, height / 2, width / 2 - 4, 0, Math.PI * 2)
                                ctx.stroke()
                                ctx.globalAlpha = 0.35
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                ctx.arc(width / 2, height / 2, width / 2 - 1, 0, Math.PI * 2)
                                ctx.stroke()
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 90; height: 90; radius: 45
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "#3a3480" }
                                    GradientStop { position: 1; color: "#26285a" }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    text: root.selected ? (root.selectedClip + 1) : "✦"
                                    color: "white"
                                    font.family: root.selected ? "Poppins" : "Segoe UI Symbol"
                                    font.pixelSize: root.selected ? 34 : 30
                                    font.weight: Font.Bold
                                }
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 124
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: root.selected ? "Klip " + (root.selectedClip + 1) : ProjectController.name
                            color: root.textMain
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 146
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: root.selected ? "@" + root.selected.name
                                                : ProjectController.clips.length + " klip · " + Theme.timecode(ProjectController.duration)
                            color: root.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }

                    ColumnLayout {
                        visible: root.selected !== null
                        Layout.fillWidth: true
                        spacing: 8
                        Section { text: "KLİP BİLGİSİ" }
                        InfoCard {
                            glyph: ""
                            title: "Kaynak  " + (root.selected ? Theme.timecode(root.selected.sourceIn) + " – " + Theme.timecode(root.selected.sourceOut) : "")
                            subtitle: "Orijinal videodaki aralık"
                        }
                        InfoCard {
                            glyph: ""
                            from: Theme.accent; to: Theme.neonCyan
                            title: "Timeline  " + (root.selected ? Theme.timecode(root.selected.start) + " – " + Theme.timecode(root.selected.end) : "")
                            subtitle: (root.selected ? (root.selected.sourceOut - root.selected.sourceIn).toFixed(2) : "0") + " sn · tıklayınca başına git"
                            onClicked: if (root.selected) video.seek(root.selected.start)
                        }

                        Section { visible: root.selectedClip >= 1; text: "ÖNCEKİ KESİMDEKİ GEÇİŞ"; Layout.topMargin: 6 }
                        Flow {
                            visible: root.selectedClip >= 1
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [{ id: "", label: "Kesme" }].concat(ProjectController.transitionKinds)
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property bool current: root.selected && root.selected.transition === modelData.id
                                    width: tLabel.implicitWidth + 16; height: 26; radius: 13
                                    color: current ? root.accent : root.panelHi
                                    Text { id: tLabel; anchors.centerIn: parent; text: modelData.label; color: current ? "white" : root.textMain; font.pixelSize: 11 }
                                    TapHandler {
                                        enabled: !ProjectController.locked
                                        onTapped: ProjectController.setTransition(root.selectedClip, parent.modelData.id,
                                                                                  root.selected && root.selected.transitionDuration > 0
                                                                                  ? root.selected.transitionDuration : 0.5)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            visible: root.selectedClip >= 1 && root.selected && root.selected.transition !== ""
                            Layout.fillWidth: true
                            Text { text: "Süre"; color: root.textDim; font.pixelSize: 12 }
                            DSlider {
                                id: durationSlider
                                Layout.fillWidth: true
                                from: 0.1; to: 2.0; stepSize: 0.05
                                value: root.selected ? root.selected.transitionDuration : 0.5
                                onMoved: ProjectController.setTransition(root.selectedClip, root.selected.transition, value)
                            }
                            Text { text: durationSlider.value.toFixed(2) + " sn"; color: root.textMain; font.pixelSize: 11; font.family: "Consolas" }
                        }
                        DButton {
                            text: "Klibi sil"
                            glyph: ""
                            enabled: !ProjectController.locked
                            onClicked: ProjectController.removeClip(root.selectedClip)
                        }
                    }

                    ColumnLayout {
                        visible: root.selected === null
                        Layout.fillWidth: true
                        spacing: 8
                        Section { text: "PROJE" }
                        InfoCard {
                            glyph: ""
                            title: "Süre  " + Theme.timecode(ProjectController.duration)
                            subtitle: ProjectController.clips.length + " klip · " + (ProjectController.dirty ? "kaydedilmedi" : "kaydedildi")
                        }
                        InfoCard {
                            glyph: ""
                            from: Theme.accent; to: Theme.neonCyan
                            title: "Önizleme  " + (video.videoSize.width > 0 ? video.videoSize.width + "×" + video.videoSize.height : "—")
                            subtitle: video.codec !== "" ? video.codec + " · GPU ile çözülüyor" : "Video yok"
                        }
                        InfoCard {
                            glyph: ""
                            from: "#5b8cff"; to: Theme.accentHot
                            title: video.gpuName
                            subtitle: "Ekran kartı"
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            text: "Timeline'da bir klibe tıklayarak geçişini ve ayrıntılarını düzenleyin. S: böl · Del: sil · Ctrl+Z: geri al"
                            color: root.textDim
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        // ---------- Timeline ----------
        Rectangle {
            id: timeline
            Layout.fillWidth: true
            Layout.preferredHeight: 270
            color: "#1b1c36"
            Rectangle { width: parent.width; height: 1; color: root.line }

            readonly property real headerWidth: 118
            readonly property real viewWidth: width - headerWidth - 20
            readonly property real span: Math.max(ProjectController.duration * 1.08, 10)
            readonly property real pps: viewWidth / span * root.zoom

            Rectangle {
                id: tlTools
                width: parent.width
                height: 42
                color: root.panel
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    spacing: 2
                    IconButton { glyph: ""; tip: "Geri al"; enabled: ProjectController.canUndo && !ProjectController.locked; onClicked: ProjectController.undo() }
                    IconButton { glyph: ""; tip: "Yinele"; enabled: ProjectController.canRedo && !ProjectController.locked; onClicked: ProjectController.redo() }
                    Item { width: 12; height: 1 }
                    Rectangle {
                        width: 70; height: 32; radius: 6
                        color: splitHover.hovered ? root.panelHi : "transparent"
                        Text { anchors.centerIn: parent; text: "Böl (S)"; color: root.textMain; font.pixelSize: 12 }
                        HoverHandler { id: splitHover }
                        TapHandler { enabled: !ProjectController.locked; onTapped: ProjectController.splitAt(video.position) }
                    }
                    IconButton {
                        glyph: ""
                        tip: "Seçili klibi sil (Del)"
                        enabled: root.selectedClip >= 0 && !ProjectController.locked
                        onClicked: ProjectController.removeClip(root.selectedClip)
                    }
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    spacing: 8
                    IconButton { glyph: ""; onClicked: root.zoom = Math.max(root.zoom / 1.5, 1) }
                    DSlider {
                        width: 140
                        anchors.verticalCenter: parent.verticalCenter
                        from: 0; to: 1
                        value: Math.log(root.zoom) / Math.log(40)
                        onMoved: root.zoom = Math.pow(40, value)
                    }
                    IconButton { glyph: ""; onClicked: root.zoom = Math.min(root.zoom * 1.5, 40) }
                }
            }

            Flickable {
                id: tlFlick
                anchors.top: tlTools.bottom
                anchors.bottom: parent.bottom
                x: timeline.headerWidth
                width: parent.width - timeline.headerWidth
                contentWidth: Math.max(width, timeline.span * timeline.pps + 20)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar { policy: root.zoom > 1 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }

                // Ruler (click or drag to seek)
                Item {
                    id: ruler
                    width: tlFlick.contentWidth
                    height: 30
                    readonly property int step: timeline.pps > 60 ? 1 : timeline.pps > 12 ? 5 : timeline.pps > 3 ? 15 : 60
                    Repeater {
                        model: Math.ceil(timeline.span / ruler.step) + 1
                        delegate: Item {
                            required property int index
                            x: index * ruler.step * timeline.pps
                            height: ruler.height
                            Rectangle { y: 12; width: 1; height: ruler.height - 12; color: "#4a4c80" }
                            Text {
                                x: 4; y: 2
                                text: Theme.pad(Math.floor(index * ruler.step / 60)) + ":" + Theme.pad(index * ruler.step % 60)
                                color: root.textDim
                                font.family: "Consolas"
                                font.pixelSize: 11
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: (m) => video.seek(Math.max(0, m.x / timeline.pps))
                        onPositionChanged: (m) => { if (pressed) video.seek(Math.max(0, m.x / timeline.pps)) }
                    }
                }

                // Tracks
                Column {
                    y: ruler.height
                    width: tlFlick.contentWidth
                    Repeater {
                        model: [{ name: "annotations" }, { name: "video" }, { name: "audio" }]
                        delegate: Item {
                            required property var modelData
                            width: tlFlick.contentWidth
                            height: 64
                            Rectangle { y: 2; width: parent.width; height: parent.height - 4; radius: 8; color: "#22234a" }

                            Repeater {
                                model: modelData.name === "annotations" ? [] : ProjectController.clips
                                delegate: Rectangle {
                                    id: clipRect
                                    required property var modelData
                                    readonly property bool isAudio: parent.modelData.name === "audio"
                                    visible: !isAudio || modelData.hasAudio
                                    x: modelData.start * timeline.pps
                                    y: 5
                                    width: Math.max(2, (modelData.end - modelData.start) * timeline.pps)
                                    height: 52
                                    radius: 9
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: clipRect.isAudio ? "#202452" : "#3a35a8" }
                                        GradientStop { position: 1; color: clipRect.isAudio ? "#2a2463" : "#6a3fd4" }
                                    }
                                    border.color: root.selectedClip === modelData.index ? Theme.neonCyan
                                                  : isAudio ? "#4a4f9c" : "#8f7cff"
                                    border.width: root.selectedClip === modelData.index ? 2 : 1
                                    clip: true

                                    Canvas {
                                        visible: parent.isAudio
                                        anchors.fill: parent
                                        anchors.topMargin: 18
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            ctx.clearRect(0, 0, width, height)
                                            const wave = ctx.createLinearGradient(0, 0, width, 0)
                                            wave.addColorStop(0, "#38e1ff")
                                            wave.addColorStop(0.5, "#8b5cf6")
                                            wave.addColorStop(1, "#ff4fd8")
                                            ctx.fillStyle = wave
                                            let seed = 7 + Math.floor(parent.modelData.sourceIn * 10)
                                            for (let x = 0; x < width; x += 3) {
                                                seed = (seed * 9301 + 49297) % 233280
                                                const h = (0.2 + 0.8 * Math.abs(Math.sin(x / 23)) * (seed / 233280)) * height
                                                ctx.fillRect(x, (height - h) / 2, 2, h)
                                            }
                                        }
                                        onWidthChanged: requestPaint()
                                    }
                                    Row {
                                        visible: !parent.isAudio
                                        anchors.fill: parent
                                        anchors.topMargin: 20
                                        anchors.margins: 4
                                        spacing: 3
                                        Repeater {
                                            model: Math.max(0, Math.floor(parent.width / 50))
                                            delegate: Rectangle { width: 47; height: 26; radius: 5; color: "#33ffffff" }
                                        }
                                    }
                                    Text {
                                        x: 8; y: 4
                                        width: parent.width - 16
                                        text: parent.isAudio ? parent.modelData.name + " (ses)" : parent.modelData.name
                                        color: "white"
                                        font.pixelSize: 12
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    TapHandler {
                                        onTapped: (p) => {
                                            root.selectedClip = parent.modelData.index
                                            video.seek(parent.modelData.start + p.position.x / timeline.pps)
                                        }
                                    }
                                }
                            }

                            // Transitions sit across the overlap of two clips
                            Repeater {
                                model: modelData.name === "video" ? ProjectController.clips : []
                                delegate: Rectangle {
                                    required property var modelData
                                    visible: modelData.transition !== ""
                                    x: modelData.start * timeline.pps
                                    y: 18
                                    width: Math.max(14, modelData.transitionDuration * timeline.pps)
                                    height: 30
                                    radius: 8
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: Theme.accent }
                                        GradientStop { position: 1; color: Theme.accentHot }
                                    }
                                    border.color: "white"
                                    Text { anchors.centerIn: parent; text: "⋈"; color: "white"; font.pixelSize: 14; font.bold: true }
                                    ToolTip.visible: tHover.hovered
                                    ToolTip.text: modelData.transition + " · " + modelData.transitionDuration.toFixed(2) + " sn"
                                    HoverHandler { id: tHover }
                                    TapHandler { onTapped: root.selectedClip = parent.modelData.index }
                                }
                            }
                        }
                    }
                }

                // Playhead
                Item {
                    x: video.position * timeline.pps
                    y: 0
                    height: ruler.height + 3 * 64
                    Rectangle { x: -4; width: 8; height: parent.height; color: "#33ff4fd8" }
                    Rectangle { x: -1; width: 2; height: parent.height; color: root.accentHot }
                    Rectangle {
                        x: -34; y: 0
                        width: 68; height: 18; radius: 4
                        color: root.accentHot
                        Text { anchors.centerIn: parent; text: Theme.timecode(video.position); color: "white"; font.family: "Consolas"; font.pixelSize: 11 }
                    }
                }
            }

            // Track headers
            Column {
                anchors.top: tlTools.bottom
                anchors.topMargin: 30
                width: timeline.headerWidth
                Repeater {
                    model: ["Parça 3", "Parça 2", "Parça 1"]
                    delegate: Rectangle {
                        required property string modelData
                        width: timeline.headerWidth
                        height: 62
                        color: root.panel
                        Text { x: 14; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: root.textMain; font.pixelSize: 13 }
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            Glyph { text: ""; color: root.textDim; font.pixelSize: 13; width: 20 }
                            Glyph { text: ""; color: root.textDim; font.pixelSize: 13; width: 20 }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: "#1b1c36" }
                    }
                }
            }
        }
    }
}
