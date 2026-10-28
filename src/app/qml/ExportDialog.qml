// SPDX-License-Identifier: GPL-3.0-or-later
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import DivaStudio

Popup {
    id: dialog
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 600
    padding: 0
    closePolicy: ProjectController.exportBusy ? Popup.NoAutoClose : Popup.CloseOnEscape

    property string outputPath

    Settings {
        id: saved
        category: "export"
        property int height: 0
        property real fps: 0
        property string codec: "hevc"
        property string preset: "balanced"
    }

    function hardwareCodec(codec) {
        for (const e of SystemInfo.encoders)
            if (e.codec === codec && e.available && e.hardware)
                return e.name
        return ""
    }

    onOpened: if (!ProjectController.exportBusy) outputPath = ProjectController.defaultExportPath()

    FileDialog {
        id: fileDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: ["MP4 video (*.mp4)", "Matroska (*.mkv)", "QuickTime (*.mov)"]
        onAccepted: dialog.outputPath = selectedFile.toString().replace(/^file:\/{3}/, "")
    }

    background: NeonFrame { radius: 18; fill: Theme.panel }

    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.bottomMargin: 0
            Glyph { text: ""; color: Theme.accent; font.pixelSize: 18 }
            Text { text: "Dışa Aktar"; color: Theme.textMain; font.pixelSize: 20; font.bold: true }
            Item { Layout.fillWidth: true }
            IconButton { glyph: ""; enabled: !ProjectController.exportBusy; onClicked: dialog.close() }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            columns: 2
            columnSpacing: 18
            rowSpacing: 12
            enabled: !ProjectController.exportBusy

            Text { text: "Dosya"; color: Theme.textMain; font.pixelSize: 13 }
            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    Layout.fillWidth: true
                    height: 32; radius: 6
                    color: Theme.panelHi
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: dialog.outputPath
                        color: Theme.textMain
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }
                DButton { text: "Gözat"; onClicked: fileDialog.open() }
            }
            Text { text: "Çözünürlük"; color: Theme.textMain; font.pixelSize: 13 }
            Choice {
                value: saved.height
                options: [{ value: 0, label: "Proje" }, { value: 720, label: "720p" }, { value: 1080, label: "1080p" },
                          { value: 1440, label: "1440p" }, { value: 2160, label: "4K" }]
                onPicked: (v) => saved.height = v
            }
            Text { text: "Kare hızı"; color: Theme.textMain; font.pixelSize: 13 }
            Choice {
                value: saved.fps
                options: [{ value: 0, label: "Proje" }, { value: 30, label: "30" }, { value: 60, label: "60" },
                          { value: 120, label: "120" }]
                onPicked: (v) => saved.fps = v
            }
            Text { text: "Codec"; color: Theme.textMain; font.pixelSize: 13 }
            Choice {
                value: saved.codec
                options: [{ value: "h264", label: "H.264" }, { value: "hevc", label: "HEVC" }, { value: "av1", label: "AV1" }]
                onPicked: (v) => saved.codec = v
            }
            Text { text: "Öncelik"; color: Theme.textMain; font.pixelSize: 13 }
            Choice {
                value: saved.preset
                options: [{ value: "speed", label: "Hız" }, { value: "balanced", label: "Dengeli" },
                          { value: "quality", label: "Kalite" }]
                onPicked: (v) => saved.preset = v
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            text: dialog.hardwareCodec(saved.codec) !== ""
                  ? "Ekran kartı ile kodlanacak: " + dialog.hardwareCodec(saved.codec)
                  : "Bu codec için donanım kodlayıcı yok; işlemciyle kodlanacak (daha yavaş)."
            color: dialog.hardwareCodec(saved.codec) !== "" ? "#6ee7a8" : "#ffb84d"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            visible: ProjectController.exportBusy || ProjectController.exportStatus !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            spacing: 6
            Rectangle {
                Layout.fillWidth: true
                height: 8; radius: 4
                color: Theme.line
                Rectangle {
                    width: parent.width * ProjectController.exportProgress
                    height: parent.height; radius: 4
                    color: Theme.accent
                }
            }
            Text {
                Layout.fillWidth: true
                text: (ProjectController.exportBusy ? Math.round(ProjectController.exportProgress * 100) + "%  ·  " : "")
                      + ProjectController.exportStatus
                color: Theme.textDim
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.topMargin: 4
            DButton {
                visible: !ProjectController.exportBusy && ProjectController.exportStatus.startsWith("Bitti")
                text: "Klasörü aç"
                onClicked: Qt.openUrlExternally("file:///" + dialog.outputPath.substring(0, Math.max(dialog.outputPath.lastIndexOf("/"), dialog.outputPath.lastIndexOf("\\"))))
            }
            Item { Layout.fillWidth: true }
            DButton {
                text: ProjectController.exportBusy ? "İptal" : "Kapat"
                onClicked: ProjectController.exportBusy ? ProjectController.cancelExport() : dialog.close()
            }
            DButton {
                visible: !ProjectController.exportBusy
                text: "Dışa Aktar"
                kind: "accent"
                glyph: ""
                onClicked: ProjectController.exportVideo("file:///" + dialog.outputPath, saved.height, saved.fps,
                                                         saved.codec, saved.preset)
            }
        }
    }
}
