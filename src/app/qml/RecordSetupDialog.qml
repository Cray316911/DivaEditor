// SPDX-License-Identifier: GPL-3.0-or-later
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DivaStudio

// "Yeni Kayıt": which screen / region, frame rate, quality, audio sources.
Popup {
    id: dialog
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 640
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property int monitorIndex: 0
    property var region: null // {x, y, width, height} in monitor pixels, null = full screen
    signal startRequested(var options)
    signal regionRequested(var monitor)

    Settings {
        id: saved
        category: "recording"
        property int fps: 60
        property string quality: "balanced"
        property string codec: "hevc"
        property bool systemAudio: true
        property bool microphone: false
        property bool cursor: true
        property bool countdown: true
    }

    readonly property var monitor: RecorderController.monitors.length > monitorIndex
                                   ? RecorderController.monitors[monitorIndex] : null

    function hardwareCodec(codec) {
        for (const e of SystemInfo.encoders)
            if (e.codec === codec && e.available && e.hardware)
                return true
        return false
    }

    onOpened: {
        saved.sync() // pick up the quick toggles in the editor rail
        RecorderController.refreshMonitors()
    }

    background: NeonFrame { radius: 18; fill: Theme.panel }

    contentItem: ColumnLayout {
        spacing: 0

        // Title
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.bottomMargin: 8
            Rectangle { width: 14; height: 14; radius: 7; color: Theme.record }
            Text { text: "Yeni Kayıt"; color: Theme.textMain; font.pixelSize: 20; font.bold: true }
            Item { Layout.fillWidth: true }
            IconButton { glyph: ""; onClicked: dialog.close() }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.topMargin: 6
            spacing: 18

            // Screen
            ColumnLayout {
                spacing: 8
                Text { text: "EKRAN"; color: Theme.textDim; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.2 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: RecorderController.monitors
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool selected: dialog.monitorIndex === index
                            width: 190; height: 64; radius: 14
                            color: selected ? Theme.railSelected : Theme.panelHi
                            border.color: selected ? Theme.accent : "transparent"
                            border.width: 2
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: "Ekran " + (index + 1) + (modelData.primary ? " · Birincil" : "")
                                    color: Theme.textMain
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                                Text {
                                    text: modelData.width + "×" + modelData.height + " · " + modelData.refresh + " Hz"
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                }
                            }
                            TapHandler { onTapped: { dialog.monitorIndex = index; dialog.region = null } }
                        }
                    }
                }
                RowLayout {
                    spacing: 10
                    Choice {
                        value: dialog.region ? "region" : "full"
                        options: [{ value: "full", label: "Tam ekran" }, { value: "region", label: "Bölge seç…" }]
                        onPicked: (v) => {
                            if (v === "full") dialog.region = null
                            else dialog.regionRequested(dialog.monitor)
                        }
                    }
                    Text {
                        visible: dialog.region !== null
                        text: dialog.region ? dialog.region.width + "×" + dialog.region.height
                                              + "  (" + dialog.region.x + ", " + dialog.region.y + ")" : ""
                        color: Theme.accent
                        font.pixelSize: 13
                        font.family: "Consolas"
                    }
                }
            }

            // Frame rate / quality / codec
            GridLayout {
                columns: 2
                columnSpacing: 18
                rowSpacing: 12
                Text { text: "Kare hızı"; color: Theme.textMain; font.pixelSize: 13 }
                Choice {
                    value: saved.fps
                    options: [{ value: 30, label: "30 fps" },
                              { value: 60, label: "60 fps" },
                              { value: 120, label: "120 fps",
                                enabled: !dialog.monitor || dialog.monitor.refresh >= 120 }]
                    onPicked: (v) => saved.fps = v
                }
                Text { text: "Kalite"; color: Theme.textMain; font.pixelSize: 13 }
                Choice {
                    value: saved.quality
                    options: [{ value: "low", label: "Küçük dosya" },
                              { value: "balanced", label: "Dengeli" },
                              { value: "high", label: "Yüksek" }]
                    onPicked: (v) => saved.quality = v
                }
                Text { text: "Codec"; color: Theme.textMain; font.pixelSize: 13 }
                Choice {
                    value: saved.codec
                    options: [{ value: "h264", label: "H.264" },
                              { value: "hevc", label: "HEVC" },
                              { value: "av1", label: "AV1", enabled: dialog.hardwareCodec("av1") }]
                    onPicked: (v) => saved.codec = v
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

            // Sources
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12
                Toggle {
                    Layout.fillWidth: true
                    text: "Sistem sesi"
                    detail: "Oyun, müzik, tarayıcı: hoparlörden çıkan her şey (ayrı ses izi)"
                    checked: saved.systemAudio
                    onToggled: (c) => saved.systemAudio = c
                }
                Toggle {
                    Layout.fillWidth: true
                    text: "Mikrofon"
                    detail: "Varsayılan mikrofon (ayrı ses izi)"
                    checked: saved.microphone
                    onToggled: (c) => saved.microphone = c
                }
                Toggle {
                    Layout.fillWidth: true
                    text: "İmleci kaydet"
                    checked: saved.cursor
                    onToggled: (c) => saved.cursor = c
                }
                Toggle {
                    Layout.fillWidth: true
                    text: "3 saniye geri sayım"
                    checked: saved.countdown
                    onToggled: (c) => saved.countdown = c
                }
            }

            Text {
                Layout.fillWidth: true
                text: "Kayıt sırasında: F9 duraklat / devam, F10 durdur. Kayıtlar Videolar\\DivaStudio klasörüne MKV olarak yazılır (elektrik kesilse bile dosya bozulmaz)."
                color: Theme.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DButton { text: "İptal"; onClicked: dialog.close() }
                DButton {
                    text: "Kaydı Başlat"
                    kind: "record"
                    enabled: dialog.monitor !== null
                    onClicked: {
                        const r = dialog.region
                        dialog.startRequested({
                            monitor: dialog.monitorIndex,
                            monitorInfo: dialog.monitor,
                            x: r ? r.x : 0, y: r ? r.y : 0,
                            width: r ? r.width : 0, height: r ? r.height : 0,
                            fps: saved.fps, quality: saved.quality, codec: saved.codec,
                            systemAudio: saved.systemAudio, microphone: saved.microphone,
                            cursor: saved.cursor, countdown: saved.countdown
                        })
                        dialog.close()
                    }
                }
            }
        }
    }
}
