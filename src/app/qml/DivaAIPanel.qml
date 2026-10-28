// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DivaStudio

// DivaAI: describe the edit, the agent looks at the footage and does it.
ColumnLayout {
    id: panel
    spacing: 12
    property string mode: "app"

    RowLayout {
        spacing: 8
        Text { text: "✦"; color: Theme.accent; font.pixelSize: 20; font.family: "Segoe UI Symbol" }
        Text { text: "DivaAI"; color: Theme.textMain; font.pixelSize: 17; font.bold: true }
    }
    Text {
        Layout.fillWidth: true
        text: "Ne istediğini yaz; DivaAI videoyu izler, nereyi keseceğine ve hangi geçişin yakışacağına karar verir, düzenlemeyi yapar."
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    Choice {
        value: panel.mode
        options: [{ value: "app", label: "Uygulamada" }, { value: "code", label: "Claude Code ile" }]
        onPicked: (v) => panel.mode = v
    }

    // ---- In-app agent (Claude API key) ----
    ColumnLayout {
        visible: panel.mode === "app"
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 10

        ColumnLayout {
            visible: !AgentController.hasApiKey
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: "DivaAI, Claude'u senin Anthropic API anahtarınla çalıştırır (kullandığın kadar ödersin). Anahtarı platform.claude.com → Settings → API Keys sayfasından alabilirsin."
                color: Theme.textMain
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                height: 34; radius: 6
                color: Theme.panelHi
                border.color: keyField.activeFocus ? Theme.accent : "transparent"
                TextInput {
                    id: keyField
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    echoMode: TextInput.Password
                    color: Theme.textMain
                    font.pixelSize: 13
                    clip: true
                    Text {
                        visible: !keyField.text && !keyField.activeFocus
                        anchors.verticalCenter: parent.verticalCenter
                        text: "sk-ant-…"
                        color: Theme.textDim
                        font.pixelSize: 13
                    }
                }
            }
            RowLayout {
                Text {
                    Layout.fillWidth: true
                    text: "Windows Kimlik Bilgileri'nde şifreli saklanır."
                    color: Theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                DButton {
                    text: "Kaydet"
                    kind: "accent"
                    enabled: keyField.text.length > 10
                    onClicked: if (AgentController.setApiKey(keyField.text)) keyField.text = ""
                }
            }
        }

        RowLayout {
            visible: AgentController.hasApiKey
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "Anahtar: " + AgentController.keySource + " · " + AgentController.model
                color: Theme.textDim
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Text {
                text: "Kaldır"
                color: Theme.accentHot
                font.pixelSize: 11
                font.underline: true
                TapHandler { onTapped: AgentController.clearApiKey() }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            radius: 8
            color: Theme.panelHi
            border.color: promptArea.activeFocus ? Theme.accent : "transparent"
            ScrollView {
                anchors.fill: parent
                anchors.margins: 4
                TextArea {
                    id: promptArea
                    wrapMode: TextArea.Wrap
                    color: Theme.textMain
                    font.pixelSize: 13
                    placeholderText: "Örn: Öldüğüm kısımları kes, yerine geçiş ekle"
                    placeholderTextColor: Theme.textDim
                    background: null
                    enabled: !AgentController.running
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: !AgentController.running
            Repeater {
                model: ["Öldüğüm kısımları kes, geçiş ekle", "Uzun sessiz bölümleri çıkar", "Tüm kesimlere yumuşak geçiş ekle"]
                delegate: Rectangle {
                    required property string modelData
                    width: chip.implicitWidth + 18
                    height: 26
                    radius: 13
                    color: chipHover.hovered ? Theme.panelHover : Theme.panelHi
                    border.color: Theme.line
                    Text { id: chip; anchors.centerIn: parent; text: modelData; color: Theme.textMain; font.pixelSize: 11 }
                    HoverHandler { id: chipHover }
                    TapHandler { onTapped: promptArea.text = parent.modelData }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            DButton {
                Layout.fillWidth: true
                text: AgentController.running ? "Durdur" : "DivaAI ile düzenle"
                glyph: AgentController.running ? "" : ""
                kind: AgentController.running ? "plain" : "accent"
                enabled: AgentController.running || (AgentController.hasApiKey && promptArea.text.trim().length > 0
                                                       && ProjectController.clips.length > 0)
                onClicked: AgentController.running ? AgentController.cancel() : AgentController.run(promptArea.text)
            }
            IconButton {
                glyph: ""
                tip: "Günlüğü temizle"
                enabled: !AgentController.running && AgentController.log.length > 0
                onClicked: AgentController.clearLog()
            }
        }

        ListView {
            id: logView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: AgentController.log
            onCountChanged: positionViewAtEnd()
            delegate: Rectangle {
                required property var modelData
                width: logView.width
                height: entry.implicitHeight + 14
                radius: 10
                color: modelData.kind === "user" ? Theme.railSelected
                       : modelData.kind === "error" ? "#3a1a35"
                       : modelData.kind === "done" ? "#173a44" : "transparent"
                RowLayout {
                    id: entry
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    Text {
                        Layout.alignment: Qt.AlignTop
                        text: ({ user: "›", thinking: "💭", text: "✦", tool: "⚙", result: "✓", error: "!", done: "✔",
                                 status: "…", info: "i" })[modelData.kind] || "·"
                        color: modelData.kind === "error" ? Theme.record
                               : modelData.kind === "result" || modelData.kind === "done" ? "#6ee7a8" : Theme.accent
                        font.pixelSize: 13
                        font.family: "Segoe UI Emoji"
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.text
                        color: modelData.kind === "thinking" || modelData.kind === "status" ? Theme.textDim : Theme.textMain
                        font.pixelSize: 12
                        font.italic: modelData.kind === "thinking"
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }

    // ---- Claude Code (MCP) ----
    ColumnLayout {
        visible: panel.mode === "code"
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 10
        Text {
            Layout.fillWidth: true
            text: "Claude Code kullanıyorsan DivaAI'nin araçlarını oradan da kullanabilirsin; API anahtarı gerekmez, Claude Code kendi oturumunla çalışır."
            color: Theme.textMain
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Text { text: "1. Projeyi kaydet (Ctrl+S)."; color: Theme.textMain; font.pixelSize: 12 }
        Text {
            Layout.fillWidth: true
            text: "2. Bu komutu terminalde bir kez çalıştır:"
            color: Theme.textMain
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: cmd.implicitHeight + 20
            radius: 10
            color: Theme.canvas
            border.color: Theme.line
            Text {
                id: cmd
                anchors.fill: parent
                anchors.margins: 10
                text: AgentController.mcpCommand
                color: "#6ee7a8"
                font.family: "Consolas"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
        }
        DButton {
            text: "Komutu kopyala"
            glyph: ""
            onClicked: AgentController.copyText(AgentController.mcpCommand)
        }
        Text {
            Layout.fillWidth: true
            text: "3. Claude Code'a isteğini yaz, örneğin: “divastudio araçlarıyla oyunda öldüğüm yerleri kes ve geçiş ekle”. Claude Code projeyi düzenler; değişiklikler burada otomatik görünür ve Ctrl+Z ile geri alınabilir."
            color: Theme.textMain
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillHeight: true }
    }
}
