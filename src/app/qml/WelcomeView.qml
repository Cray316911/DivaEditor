// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DivaStudio

// First screen: start a recording, a new project, open one, or pick up a recent project.
Rectangle {
    id: welcome
    color: Theme.bg

    signal newRecording()
    signal newProject()
    signal openProject()
    signal importVideo()
    signal openRecent(string path)
    signal openAI()
    signal copyEdits()

    component ActionCard: Rectangle {
        id: card
        property string title
        property string subtitle
        property string glyph
        property bool recordDot: false
        property string shortcut
        signal activated()
        Layout.fillWidth: true
        Layout.preferredHeight: 112
        radius: 16
        color: "transparent"

        NeonFrame {
            anchors.fill: parent
            radius: 16
            fill: cardHover.hovered ? Theme.panelHi : Theme.panel
            lit: cardHover.hovered
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16
            Rectangle {
                width: 52; height: 52; radius: 26
                color: card.recordDot ? "#3a1a35" : "#2c2a5c"
                border.color: card.recordDot ? "#7a2a55" : "#4a3f99"
                Rectangle {
                    visible: card.recordDot
                    anchors.centerIn: parent
                    width: 20; height: 20; radius: 10
                    color: Theme.record
                }
                Glyph {
                    visible: !card.recordDot
                    anchors.centerIn: parent
                    text: card.glyph
                    color: Theme.accent
                    font.pixelSize: 22
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: card.title; color: Theme.textMain; font.pixelSize: 17; font.bold: true }
                Text {
                    text: card.subtitle
                    color: Theme.textDim
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            Text { text: card.shortcut; color: Theme.textDim; font.pixelSize: 11; font.family: "Consolas" }
        }
        HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: card.activated() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 48
        spacing: 48

        // Left: brand + actions
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            spacing: 22

            // Hero banner
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                radius: 18
                clip: true
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#231f55" }
                    GradientStop { position: 0.55; color: "#3b2384" }
                    GradientStop { position: 1.0; color: "#7a2378" }
                }
                // Soft neon glows (radial gradients), kept inside the rounded corners
                Canvas {
                    anchors.fill: parent
                    anchors.margins: 6
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const glow = (x, y, r, color) => {
                            const g = ctx.createRadialGradient(x, y, 0, x, y, r)
                            g.addColorStop(0, color)
                            g.addColorStop(1, "rgba(0,0,0,0)")
                            ctx.fillStyle = g
                            ctx.fillRect(0, 0, width, height)
                        }
                        glow(width * 0.70, height * 0.35, 190, "rgba(255,79,216,0.55)")
                        glow(width * 0.86, height * 0.75, 150, "rgba(139,92,246,0.55)")
                        glow(width * 0.55, height * 0.95, 120, "rgba(56,225,255,0.25)")
                    }
                    onWidthChanged: requestPaint()
                }
                ColumnLayout {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 34
                    spacing: 8
                    Text {
                        text: "Diva<font color='#ff8fe9'>Studio</font>"
                        textFormat: Text.StyledText
                        color: "white"
                        font.pixelSize: 46
                        font.bold: true
                    }
                    Text {
                        text: "Ekranını kaydet, düzenle, paylaş. Gerisini DivaAI halletsin."
                        color: "#e4dcff"
                        font.pixelSize: 16
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 16
                rowSpacing: 16
                ActionCard {
                    title: "Yeni Kayıt"
                    subtitle: "Ekranı, sistem sesini ve mikrofonu 60 fps kaydet. Oyunlarda FPS'i korur."
                    recordDot: true
                    shortcut: "Ctrl+R"
                    onActivated: welcome.newRecording()
                }
                ActionCard {
                    title: "Yeni Proje"
                    subtitle: "Boş bir projeyle başla, videoları sonra ekle."
                    glyph: ""
                    shortcut: "Ctrl+N"
                    onActivated: welcome.newProject()
                }
                ActionCard {
                    title: "Proje Aç"
                    subtitle: "Kaydedilmiş bir .divaproj dosyasını aç."
                    glyph: ""
                    shortcut: "Ctrl+O"
                    onActivated: welcome.openProject()
                }
                ActionCard {
                    title: "Video İçe Aktar"
                    subtitle: "Var olan bir videoyu açıp hemen düzenlemeye başla."
                    glyph: ""
                    shortcut: "Ctrl+I"
                    onActivated: welcome.importVideo()
                }
            }

            // Featured: what DivaAI can do
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Text { text: "Öne çıkanlar"; color: Theme.textMain; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Text { text: "DivaAI"; color: Theme.textDim; font.pixelSize: 11 }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Repeater {
                    model: [
                        { title: "DivaAI ile düzenle", text: "“Öldüğüm yerleri kes, geçiş ekle” yaz; DivaAI videoyu izleyip düzenlesin.",
                          from: "#38e1ff", to: "#8b5cf6", glyph: "✦", action: "ai" },
                        { title: "Düzenlemeyi kopyala", text: "Bir projedeki kesme ve geçişleri başka bir videoya birebir ya da akıllıca uygula.",
                          from: "#8b5cf6", to: "#ff4fd8", glyph: "⧉", action: "copy" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        radius: 18
                        color: "transparent"
                        NeonFrame { anchors.fill: parent; radius: 18; fill: Theme.panel; lit: featHover.hovered }
                        Rectangle {
                            x: 8; y: 8
                            width: parent.width - 16
                            height: 98
                            radius: 13
                            clip: true
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#231f55" }
                                GradientStop { position: 1; color: "#3b2384" }
                            }
                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    const g = ctx.createRadialGradient(width * 0.5, height * 0.55, 0, width * 0.5, height * 0.55, height)
                                    g.addColorStop(0, modelData.to)
                                    g.addColorStop(1, "rgba(0,0,0,0)")
                                    ctx.globalAlpha = 0.6
                                    ctx.fillStyle = g
                                    ctx.fillRect(0, 0, width, height)
                                }
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 54; height: 54; radius: 27
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: modelData.from }
                                    GradientStop { position: 1; color: modelData.to }
                                }
                                border.color: "white"
                                border.width: 2
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.glyph
                                    color: "white"
                                    font.family: "Segoe UI Symbol"
                                    font.pixelSize: 22
                                }
                            }
                        }
                        ColumnLayout {
                            x: 16
                            y: 114
                            width: parent.width - 32
                            spacing: 3
                            Text { text: modelData.title; color: Theme.textMain; font.pixelSize: 14; font.weight: Font.DemiBold }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.text
                                color: Theme.textDim
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        HoverHandler { id: featHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: modelData.action === "ai" ? welcome.openAI() : welcome.copyEdits() }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // System status: what this PC can do
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: sys.implicitHeight + 28
                radius: 14
                color: Theme.panel
                border.color: Theme.line
                ColumnLayout {
                    id: sys
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6
                    RowLayout {
                        spacing: 10
                        Glyph { text: ""; color: Theme.accent; font.pixelSize: 15 }
                        Text { text: SystemInfo.gpuName; color: Theme.textMain; font.pixelSize: 13; font.bold: true }
                        Text { text: "· " + SystemInfo.windowsVersion; color: Theme.textDim; font.pixelSize: 12 }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: SystemInfo.encoderSummary
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: SystemInfo.captureRateCap
                              ? "Kayıt hız sınırı destekleniyor (Windows 11 24H2+): yüksek Hz ekranda bile yalnızca seçilen fps yakalanır."
                              : "Windows 11 24H2 öncesi: kayıt yine çalışır, yüksek Hz ekranda biraz daha fazla GPU işi yapar."
                        color: SystemInfo.captureRateCap ? "#6ee7a8" : "#ffb84d"
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // Right: recent projects
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            Layout.fillWidth: true
            radius: 18
            color: Theme.panel
            border.color: Theme.line

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                // Header: neon ring like a profile card
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 176
                    Canvas {
                        id: welcomeRing
                        width: 116; height: 116
                        anchors.horizontalCenter: parent.horizontalCenter
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
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 94; height: 94; radius: 47
                            gradient: Gradient {
                                GradientStop { position: 0; color: "#3a3480" }
                                GradientStop { position: 1; color: "#26285a" }
                            }
                            Text { anchors.centerIn: parent; text: "DIVA"; color: "white"; font.pixelSize: 20; font.weight: Font.Bold; font.letterSpacing: 2 }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 126
                        text: "Hoş geldin"
                        color: Theme.textMain
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 150
                        text: "@divastudio · v" + Qt.application.version
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Son projeler"; color: Theme.textMain; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Text { text: ProjectController.recentProjects.length + " proje"; color: Theme.textDim; font.pixelSize: 11 }
                }
                Text {
                    visible: ProjectController.recentProjects.length === 0
                    text: "Henüz proje yok. Bir kayıt yapın ya da video içe aktarın."
                    color: Theme.textDim
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: ProjectController.recentProjects
                    delegate: Rectangle {
                        required property string modelData
                        width: ListView.view.width
                        height: 58
                        radius: 12
                        color: recentHover.hovered ? Theme.panelHover : Theme.panelHi
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12
                            Rectangle {
                                width: 34; height: 34; radius: 17
                                color: "#2c2a5c"
                                border.color: "#4a3f99"
                                Glyph { anchors.centerIn: parent; text: ""; color: Theme.accent; font.pixelSize: 15 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: Theme.fileName(modelData).replace(".divaproj", "")
                                    color: Theme.textMain
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: modelData
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        HoverHandler { id: recentHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: welcome.openRecent(modelData) }
                    }
                }
            }
        }
    }
}
