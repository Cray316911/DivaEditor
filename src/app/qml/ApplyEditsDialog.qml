// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import DivaStudio

// Repeat another project's edits on this video: exactly, scaled, or adapted by DivaAI.
Popup {
    id: dialog
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 600
    padding: 0

    property var info: null
    property string mode: "ai"
    signal runAgent(string prompt)

    function pick() { fileDialog.open() }

    FileDialog {
        id: fileDialog
        nameFilters: ["DivaStudio projesi (*.divaproj)"]
        onAccepted: {
            dialog.info = ProjectController.describeProject(selectedFile)
            dialog.open()
        }
    }

    background: NeonFrame { radius: 18; fill: Theme.panel }

    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.bottomMargin: 0
            Text { text: "Başka projedeki düzenlemeleri uygula"; color: Theme.textMain; font.pixelSize: 18; font.bold: true }
            Item { Layout.fillWidth: true }
            IconButton { glyph: ""; onClicked: dialog.close() }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.preferredHeight: summary.implicitHeight + 24
            radius: 8
            color: Theme.panelHi
            ColumnLayout {
                id: summary
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                Text {
                    text: dialog.info && dialog.info.ok ? "Referans: " + dialog.info.name : "Proje okunamadı"
                    color: Theme.textMain
                    font.pixelSize: 14
                    font.bold: true
                }
                Text {
                    visible: dialog.info && dialog.info.ok
                    text: dialog.info && dialog.info.ok
                          ? dialog.info.clips + " klip · " + dialog.info.removedCount + " bölüm kesilmiş ("
                            + dialog.info.removedSeconds.toFixed(1) + " sn) · geçişler: "
                            + (dialog.info.transitions || "yok") + " · kaynak " + Theme.timecode(dialog.info.sourceDuration)
                          : ""
                    color: Theme.textDim
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    visible: dialog.info && !dialog.info.ok
                    text: dialog.info ? (dialog.info.error || "") : ""
                    color: Theme.record
                    font.pixelSize: 12
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            spacing: 8
            Repeater {
                model: [
                    { value: "ai", title: "DivaAI ile uyarla",
                      text: "DivaAI referansta neyin kesildiğine bakar (ör. ölüm sahneleri), bu videoda benzer anları bulur ve aynı geçişlerle aynı düzenlemeyi yapar. İçerik farklıysa doğru seçim budur." },
                    { value: "exact", title: "Birebir uygula",
                      text: "Aynı zaman aralıkları ve aynı geçişler. Aynı çekim ya da aynı yapıdaki videolar için (ör. intro/outro kırpma)." },
                    { value: "scaled", title: "Orantılı uygula",
                      text: "Aynı düzenleme, bu videonun süresine oranlanarak." }
                ]
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool selected: dialog.mode === modelData.value
                    Layout.fillWidth: true
                    Layout.preferredHeight: optText.implicitHeight + 40
                    radius: 12
                    color: selected ? Theme.railSelected : Theme.panelHi
                    border.color: selected ? Theme.accent : "transparent"
                    border.width: 2
                    ColumnLayout {
                        id: optText
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 3
                        Text { text: modelData.title; color: Theme.textMain; font.pixelSize: 13; font.bold: true }
                        Text { text: modelData.text; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    TapHandler { onTapped: dialog.mode = parent.modelData.value }
                }
            }
            Text {
                visible: dialog.mode === "ai" && !AgentController.hasApiKey
                text: "DivaAI için API anahtarı gerekir (DivaAI panelinden ekleyin) ya da Claude Code bağlantısını kullanın."
                color: "#ffb84d"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.topMargin: 0
            DButton { text: "Başka proje seç"; onClicked: dialog.pick() }
            Item { Layout.fillWidth: true }
            DButton { text: "İptal"; onClicked: dialog.close() }
            DButton {
                text: "Uygula"
                kind: "accent"
                enabled: dialog.info && dialog.info.ok && ProjectController.clips.length > 0
                         && (dialog.mode !== "ai" || AgentController.hasApiKey)
                onClicked: {
                    if (dialog.mode === "ai") {
                        dialog.runAgent("Referans proje: " + dialog.info.path
                                        + "\nO projede videoya yapılan düzenlemeleri incele (ne tür sahneler kesilmiş, hangi geçişler kullanılmış) ve aynı düzenlemeyi bu videoya uygula. İçerik farklıysa benzer anları kendin bul.")
                    } else {
                        ProjectController.applyEditsFrom("file:///" + dialog.info.path, dialog.mode)
                    }
                    dialog.close()
                }
            }
        }
    }
}
