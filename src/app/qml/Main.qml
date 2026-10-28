// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import DivaStudio

ApplicationWindow {
    id: window

    property url initialSource
    property string testPanel
    property bool showWelcome: true
    property var pendingRecording: null

    width: 1600
    height: 940
    minimumWidth: 1200
    minimumHeight: 720
    visible: false
    color: Theme.bg
    title: ProjectController.hasProject && !showWelcome
           ? "DivaStudio — " + ProjectController.name + (ProjectController.dirty ? " •" : "") : "DivaStudio"

    function saveProject() { if (!ProjectController.save()) saveDialog.open() }
    function startEditing() { showWelcome = false }
    function toast(message) { toastText.text = message; toastPopup.open(); toastTimer.restart() }

    Component.onCompleted: {
        if (initialSource.toString() !== "") {
            if (initialSource.toString().endsWith(".divaproj"))
                ProjectController.openProject(initialSource)
            else
                ProjectController.importMedia(initialSource)
            showWelcome = false
        }
        if (testPanel === "record") recordSetup.open()
        else if (testPanel === "export") exportDialog.open()
        else if (testPanel === "ai") editor.category = 0
        else if (testPanel === "record-test") {
            recordSetup.startRequested({ monitor: 0, monitorInfo: RecorderController.monitors[0],
                                         x: 0, y: 0, width: 0, height: 0, fps: 60, quality: "balanced",
                                         codec: "hevc", systemAudio: true, microphone: false, cursor: true,
                                         countdown: false })
            recordTestStop.start()
        }
        else if (testPanel === "apply") {
            applyEdits.info = ProjectController.describeProject(initialSource)
            applyEdits.open()
        }
    }

    Shortcut { sequence: "Ctrl+S"; onActivated: window.saveProject() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: saveDialog.open() }
    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+N"; onActivated: { ProjectController.newProject(); window.startEditing() } }
    Shortcut { sequence: "Ctrl+I"; onActivated: importDialog.open() }
    Shortcut { sequence: "Ctrl+R"; onActivated: recordSetup.open() }
    Shortcut { sequence: "Ctrl+E"; enabled: !window.showWelcome; onActivated: exportDialog.open() }
    Shortcut { sequences: ["Ctrl+Z"]; onActivated: ProjectController.undo() }
    Shortcut { sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]; onActivated: ProjectController.redo() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

    AppRail {
        Layout.fillHeight: true
        current: window.showWelcome ? "home" : (editor.category === 0 ? "ai" : "editor")
        onHomeRequested: window.showWelcome = true
        onEditorRequested: {
            if (!ProjectController.hasProject) ProjectController.newProject()
            if (editor.category === 0) editor.category = 1
            window.startEditing()
        }
        onRecordRequested: recordSetup.open()
        onAiRequested: {
            if (!ProjectController.hasProject) ProjectController.newProject()
            editor.category = 0
            window.startEditing()
        }
        onNewRequested: { ProjectController.newProject(); window.startEditing() }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: window.showWelcome ? 0 : 1

        WelcomeView {
            onNewRecording: recordSetup.open()
            onNewProject: { ProjectController.newProject(); window.startEditing() }
            onOpenProject: openDialog.open()
            onImportVideo: importDialog.open()
            onOpenRecent: (path) => { if (ProjectController.openProject("file:///" + path)) window.startEditing() }
            onOpenAI: {
                if (!ProjectController.hasProject) ProjectController.newProject()
                editor.category = 0
                window.startEditing()
            }
            onCopyEdits: {
                if (!ProjectController.hasProject) ProjectController.newProject()
                window.startEditing()
                applyEdits.pick()
            }
        }
        EditorView {
            id: editor
            onRecordRequested: recordSetup.open()
            onExportRequested: exportDialog.open()
            onNewRequested: ProjectController.newProject()
            onOpenRequested: openDialog.open()
            onImportRequested: importDialog.open()
            onSaveRequested: window.saveProject()
            onSaveAsRequested: saveDialog.open()
            onHomeRequested: window.showWelcome = true
            onApplyEditsRequested: applyEdits.pick()
        }
    }
    }

    // ---------- Files ----------
    FileDialog {
        id: openDialog
        title: "Proje aç"
        nameFilters: ["DivaStudio projesi (*.divaproj)"]
        onAccepted: if (ProjectController.openProject(selectedFile)) window.startEditing()
    }
    FileDialog {
        id: importDialog
        title: "Video içe aktar"
        nameFilters: ["Videolar (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.ts)", "Tüm dosyalar (*)"]
        onAccepted: if (ProjectController.importMedia(selectedFile)) window.startEditing()
    }
    FileDialog {
        id: saveDialog
        title: "Projeyi kaydet"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "divaproj"
        nameFilters: ["DivaStudio projesi (*.divaproj)"]
        onAccepted: ProjectController.saveAs(selectedFile)
    }

    ExportDialog { id: exportDialog }
    ApplyEditsDialog {
        id: applyEdits
        onRunAgent: (prompt) => { editor.category = 0; AgentController.run(prompt) }
    }

    // ---------- Recording flow: setup -> (region) -> countdown -> record -> import ----------
    RecordSetupDialog {
        id: recordSetup
        onRegionRequested: (monitor) => { recordSetup.close(); regionSelector.openOn(monitor) }
        onStartRequested: (options) => {
            window.pendingRecording = options
            window.hide()
            if (options.countdown)
                countdown.begin(options.monitorInfo.name)
            else
                startTimer.start()
        }
    }
    RegionSelector {
        id: regionSelector
        onChosen: (region) => { recordSetup.region = region; recordSetup.open() }
        onCanceled: recordSetup.open()
    }
    CountdownWindow {
        id: countdown
        onDone: startTimer.start()
    }
    // Give the main window a moment to disappear before the first frame is captured.
    Timer {
        id: startTimer
        interval: 250
        onTriggered: {
            const options = window.pendingRecording
            recordingBar.placeOn(options.monitorInfo.name)
            if (!RecorderController.start(options)) {
                window.show()
                window.raise()
            }
        }
    }
    RecordingBar { id: recordingBar }
    Timer { id: recordTestStop; interval: 4500; onTriggered: RecorderController.stop() }

    Connections {
        target: RecorderController
        function onFinished(path) {
            console.info("recording finished:", path)
            window.show()
            window.raise()
            window.requestActivate()
            if (window.showWelcome || !ProjectController.hasProject)
                ProjectController.newProject()
            ProjectController.importMedia("file:///" + path)
            window.startEditing()
            window.toast("Kayıt eklendi: " + Theme.fileName(path))
        }
        function onFailed(message) {
            console.warn("recording problem:", message)
            window.show()
            window.toast(message)
        }
    }
    Connections {
        target: ProjectController
        function onErrorOccurred(message) { window.toast(message) }
        function onExportFinished(path) { window.toast("Dışa aktarıldı: " + path) }
    }

    // ---------- Toast ----------
    Popup {
        id: toastPopup
        x: (window.width - width) / 2
        y: window.height - height - 40
        width: Math.min(toastText.implicitWidth + 40, window.width - 80)
        height: toastText.implicitHeight + 24
        closePolicy: Popup.NoAutoClose
        background: Rectangle { radius: 10; color: "#f0241d33"; border.color: Theme.accent }
        contentItem: Text {
            id: toastText
            color: Theme.textMain
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Timer { id: toastTimer; interval: 5000; onTriggered: toastPopup.close() }
    }

    // ---------- Unsaved changes ----------
    property bool closingConfirmed: false
    onClosing: (close) => {
        if (ProjectController.dirty && !closingConfirmed && ProjectController.clips.length > 0) {
            close.accepted = false
            unsavedDialog.open()
        }
    }
    MessageDialog {
        id: unsavedDialog
        title: "DivaStudio"
        text: "Projede kaydedilmemiş değişiklikler var."
        informativeText: "Çıkmadan önce kaydetmek ister misiniz?"
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button) => {
            if (button === MessageDialog.Save) {
                if (ProjectController.save()) { window.closingConfirmed = true; window.close() }
                else saveDialog.open()
            } else if (button === MessageDialog.Discard) {
                window.closingConfirmed = true
                window.close()
            }
        }
    }
}
