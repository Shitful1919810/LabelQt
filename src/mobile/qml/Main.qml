import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    width: 460
    height: 820
    visible: true
    title: "LabelQt"
    Material.primary: "#165f78"
    Material.accent: "#147d70"
    property bool wide: width >= 840
    property int panel: 0
    property int canvasTool: 0
    readonly property bool editingText: editor.activeFocus && mobileDocument.selectedLabel.length > 0 && panel !== 2
    // Focus can outlive the Android keyboard. Compact only while it is visible or space is constrained.
    readonly property bool compactEditor: editingText && (Qt.inputMethod.visible || contentItem.height < 480)

    function selectCanvasTool(tool) {
        if (!finishEditing()) return
        canvas.forceActiveFocus()
        Qt.inputMethod.hide()
        canvasTool = tool
    }

    function finishEditing() {
        Qt.inputMethod.commit()
        return mobileDocument.commitText()
    }
    function importProject() {
        if (Qt.platform.os === "android") androidFiles.chooseFile()
        else localFile.open()
    }
    function renameProject(key, title) {
        workspaceController.clearMessage()
        renameProjectDialog.projectKey = key
        projectName.text = title
        renameProjectDialog.open()
    }
    function deleteProject(key, title) {
        workspaceController.clearMessage()
        deleteProjectDialog.projectKey = key
        deleteProjectDialog.projectTitle = title
        deleteProjectDialog.open()
    }
    function changeReviewState() {
        if (!finishEditing()) return
        mobileReview.clearError()
        newRoundDialog.token = mobileReview.editToken()
        if (mobileReview.roundState === "active" || mobileReview.roundState === "completed") {
            newRoundDialog.completing = mobileReview.roundState === "active"
            newRoundDialog.open()
        }
        else mobileReview.startRound()
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 0
            IconButton {
                objectName: "openProjectsButton"
                visible: mobileDocument.opened
                iconName: "menu"
                text: qsTr("Projects")
                onClicked: projects.open()
            }
            Label {
                objectName: "projectTitle"
                text: mobileDocument.opened ? mobileDocument.title + (mobileDocument.dirty ? " *" : "") : "LabelQt"
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            IconButton {
                iconName: "undo-2"
                visible: mobileDocument.opened
                text: qsTr("Undo")
                enabled: mobileDocument.canUndo || mobileDocument.dirty
                onClicked: if (window.finishEditing()) mobileDocument.undo()
            }
            IconButton {
                iconName: "redo-2"
                visible: mobileDocument.opened
                text: qsTr("Redo")
                enabled: mobileDocument.canRedo
                onClicked: if (window.finishEditing()) mobileDocument.redo()
            }
            IconButton {
                iconName: "keyboard"
                text: qsTr("Finish editing")
                visible: window.editingText
                onClicked: {
                    if (window.finishEditing()) {
                        canvas.forceActiveFocus()
                        Qt.inputMethod.hide()
                    }
                }
            }
            IconButton {
                iconName: "ellipsis"
                text: qsTr("Project actions")
                onClicked: actions.open()
                Menu {
                    id: actions
                    objectName: "projectActionsMenu"
                    MenuItem { text: qsTr("Save"); visible: mobileDocument.opened; height: visible ? implicitHeight : 0; enabled: mobileDocument.opened; onTriggered: if (window.finishEditing()) mobileDocument.save() }
                    MenuItem {
                        objectName: "shareZipAction"
                        text: qsTr("Share ZIP")
                        visible: mobileDocument.opened
                        height: visible ? implicitHeight : 0
                        enabled: mobileDocument.opened && !workspaceController.busy
                        onTriggered: {
                            if (window.finishEditing()) {
                                workspaceController.clearMessage()
                                shareName.text = workspaceController.suggestedShareName
                                shareDialog.open()
                            }
                        }
                    }
                    MenuItem { text: qsTr("Groups"); visible: mobileDocument.opened; height: visible ? implicitHeight : 0; enabled: mobileDocument.opened; onTriggered: groupsDialog.open() }
                    MenuItem {
                        objectName: "shareLastExportAction"
                        text: qsTr("Share last export")
                        visible: workspaceController.lastExport.length > 0
                        height: visible ? implicitHeight : 0
                        enabled: visible && !workspaceController.busy
                        onTriggered: workspaceController.shareLastExport()
                    }
                    MenuItem {
                        objectName: "sharePdfAction"
                        text: qsTr("Share proofreading PDF")
                        visible: mobileDocument.opened
                        height: visible ? implicitHeight : 0
                        enabled: mobileDocument.opened && !workspaceController.busy
                            && (mobileReview.roundState === "active" || mobileReview.roundState === "completed")
                        onTriggered: {
                            if (!window.finishEditing()) return
                            workspaceController.clearMessage()
                            reportName.text = workspaceController.suggestedReportName
                            reportScope.currentIndex = 0
                            reportDialog.open()
                        }
                    }
                    MenuItem { objectName: "aboutAction"; text: qsTr("About"); onTriggered: aboutDialog.open() }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ProgressBar { Layout.fillWidth: true; visible: workspaceController.busy; indeterminate: true }
        RowLayout {
            visible: workspaceController.reportExporting
            Layout.fillWidth: true
            Label { text: qsTr("Preparing PDF report..."); Layout.fillWidth: true; wrapMode: Text.Wrap }
            IconButton { iconName: "x"; text: qsTr("Cancel export"); onClicked: workspaceController.cancelReportExport() }
        }
        Frame {
            Layout.fillWidth: true
            visible: errorText.text.length > 0
            RowLayout {
                anchors.fill: parent
                Label {
                    id: errorText
                    text: mobileDocument.error || mobileReview.error || workspaceController.message
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    font.pixelSize: 13
                }
                IconButton {
                    iconName: "x"
                    text: qsTr("Dismiss")
                    onClicked: { mobileDocument.clearError(); mobileReview.clearError(); workspaceController.clearMessage() }
                }
            }
        }
        RowLayout {
            objectName: "pageNavigation"
            visible: mobileDocument.opened && !window.compactEditor && window.panel !== 2
            Layout.fillWidth: true
            IconButton { iconName: "chevron-left"; text: qsTr("Previous page"); enabled: mobileDocument.pageIndex > 0; onClicked: if (window.finishEditing()) mobileDocument.setPage(mobileDocument.pageIndex - 1) }
            ComboBox {
                Layout.fillWidth: true
                model: mobileDocument.pageNames
                currentIndex: mobileDocument.pageIndex
                onActivated: if (window.finishEditing()) mobileDocument.setPage(currentIndex)
            }
            IconButton { iconName: "chevron-right"; text: qsTr("Next page"); enabled: mobileDocument.pageIndex + 1 < mobileDocument.pageNames.length; onClicked: if (window.finishEditing()) mobileDocument.setPage(mobileDocument.pageIndex + 1) }
        }
        TabBar {
            objectName: "mainTabs"
            visible: mobileDocument.opened && !window.compactEditor
            Layout.fillWidth: true
            currentIndex: window.panel
            onCurrentIndexChanged: { window.finishEditing(); window.panel = currentIndex }
            TabButton { text: qsTr("Image") }
            TabButton { text: qsTr("Labels") }
            TabButton { text: qsTr("Find and Review") }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !mobileDocument.opened
            WorkspaceBrowser {
                objectName: "homeProjects"
                anchors.fill: parent
                anchors.margins: 16
                workspaceModel: workspaceController.workspaces
                busy: workspaceController.busy
                onImportRequested: window.importProject()
                onOpenRequested: key => { if (window.finishEditing()) workspaceController.openWorkspace(key) }
                onRenameRequested: (key, title) => window.renameProject(key, title)
                onDeleteRequested: (key, title) => window.deleteProject(key, title)
            }
        }
        RowLayout {
            visible: mobileDocument.opened && window.panel !== 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 1
            ColumnLayout {
                visible: window.wide || window.panel === 0 || window.compactEditor
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: window.width * 0.6
                ProjectCanvas {
                    id: canvas
                    objectName: "projectCanvas"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    imageSource: mobileDocument.imageSource
                    labelModel: mobileDocument.labels
                    selectedLabel: mobileDocument.selectedLabel
                    toolMode: window.canvasTool
                    onLabelTapped: key => { if (window.finishEditing()) mobileDocument.selectLabel(key) }
                    onLabelAdded: (x, y) => { if (window.finishEditing()) mobileDocument.addLabel(x, y) }
                    onLabelMoved: (key, x, y) => mobileDocument.moveLabel(key, x, y)
                }
                RowLayout {
                    objectName: "canvasTools"
                    visible: !window.compactEditor || window.canvasTool !== 0
                    Layout.alignment: Qt.AlignHCenter
                    ButtonGroup { id: tools }
                    Button { objectName: "browseTool"; text: qsTr("Browse"); checkable: true; checked: window.canvasTool === 0; ButtonGroup.group: tools; onClicked: window.selectCanvasTool(0) }
                    Button { text: qsTr("Add"); checkable: true; checked: window.canvasTool === 1; ButtonGroup.group: tools; onClicked: window.selectCanvasTool(1) }
                    Button { text: qsTr("Move"); checkable: true; checked: window.canvasTool === 2; ButtonGroup.group: tools; onClicked: window.selectCanvasTool(2) }
                    IconButton { iconName: "maximize"; text: qsTr("Fit image"); onClicked: canvas.resetView() }
                }
            }
            ListView {
                visible: window.wide || (window.panel === 1 && !window.compactEditor)
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: window.width * 0.4
                clip: true
                model: mobileDocument.labels
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property string labelKey
                    required property string labelText
                    required property string groupName
                    required property color markerColor
                    required property int markerNumber
                    width: ListView.view.width
                    highlighted: labelKey === mobileDocument.selectedLabel
                    onClicked: if (window.finishEditing()) mobileDocument.selectLabel(labelKey)
                    contentItem: ColumnLayout {
                        Label { text: "#" + markerNumber + "  " + groupName; textFormat: Text.PlainText; font.pixelSize: 12; color: markerColor }
                        Label { visible: labelText.trim().length > 0; text: labelText; textFormat: Text.PlainText; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            visible: labelText.trim().length === 0
                            text: qsTr("Empty label")
                            font.bold: true
                            padding: 6
                            background: Rectangle { color: "transparent"; border.color: palette.mid; radius: 3 }
                        }
                    }
                }
            }
        }
        InspectionPanel {
            visible: mobileDocument.opened && window.panel === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            document: mobileDocument
            search: mobileSearch
            review: mobileReview
            finishEditing: window.finishEditing
            onRoundRequested: window.changeReviewState()
            onNoteRequested: noteDialog.createNote()
            onEditNoteRequested: note => noteDialog.showNote(note)
            onTargetRequested: (pageKey, labelKey) => {
                if (window.finishEditing() && mobileDocument.navigateToLabel(pageKey, labelKey)) {
                    window.panel = 0
                    Qt.inputMethod.hide()
                    if (labelKey.length > 0) canvas.focusSelected()
                }
            }
            onMatchRequested: row => {
                if (!window.finishEditing()) return
                const match = mobileSearch.results[row]
                if (mobileSearch.navigate(row)) {
                    window.panel = 1
                    Qt.inputMethod.hide()
                    Qt.callLater(() => {
                        if (window.panel === 1 && mobileDocument.selectedLabel === match.labelKey)
                            editor.select(match.start, match.start + match.length)
                    })
                }
            }
        }
        Frame {
            id: editorPanel
            visible: mobileDocument.selectedLabel.length > 0 && window.panel !== 2
            Layout.fillWidth: true
            padding: window.compactEditor ? 4 : 12
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    visible: !window.compactEditor
                    IconButton {
                        objectName: "focusMarkerButton"
                        iconName: "locate-fixed"
                        text: qsTr("Locate marker")
                        onClicked: {
                            if (!window.finishEditing()) return
                            window.panel = 0
                            canvas.forceActiveFocus()
                            Qt.inputMethod.hide()
                            canvas.focusSelected()
                        }
                    }
                    ComboBox { model: mobileDocument.groups; currentIndex: model.indexOf(mobileDocument.selectedGroup); Layout.fillWidth: true; onActivated: if (window.finishEditing()) mobileDocument.setGroup(currentText) }
                    IconButton { iconName: "trash-2"; text: qsTr("Delete label"); onClicked: deleteDialog.open() }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: window.compactEditor ? Math.min(104, Math.max(64, window.contentItem.height * 0.27)) : Math.min(150, Math.max(70, window.height * 0.16))
                    Layout.maximumHeight: Layout.preferredHeight
                    clip: true
                    TextArea {
                        id: editor
                        objectName: "labelTextEditor"
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        persistentSelection: true
                        text: mobileDocument.textDraft
                        onTextChanged: if (activeFocus && !inputMethodComposing) mobileDocument.updateText(text)
                        onInputMethodComposingChanged: if (!inputMethodComposing && activeFocus) mobileDocument.updateText(text)
                        onActiveFocusChanged: if (!activeFocus) { mobileDocument.updateText(text); mobileDocument.commitText() }
                    }
                }
            }
        }
    }

    Drawer {
        id: projects
        objectName: "projectsDrawer"
        width: Math.max(0, Math.min(window.width - 48, 420))
        height: window.height
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside
        ColumnLayout {
            focus: true
            Keys.onBackPressed: event => { projects.close(); event.accepted = true }
            anchors.fill: parent
            anchors.margins: 16
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Local projects")
                    font.pixelSize: 22
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                IconButton {
                    objectName: "closeProjectsButton"
                    iconName: "x"
                    text: qsTr("Close project list")
                    onClicked: projects.close()
                }
            }
            WorkspaceBrowser {
                Layout.fillWidth: true
                Layout.fillHeight: true
                showHeading: false
                workspaceModel: workspaceController.workspaces
                busy: workspaceController.busy
                onImportRequested: window.importProject()
                onOpenRequested: key => { if (window.finishEditing() && workspaceController.openWorkspace(key)) projects.close() }
                onRenameRequested: (key, title) => window.renameProject(key, title)
                onDeleteRequested: (key, title) => window.deleteProject(key, title)
            }
            Label { text: qsTr("Edits are saved to a local copy. Export a ZIP to send your changes."); Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
        }
    }
    FileDialog { id: localFile; nameFilters: [qsTr("Project files (*.zip *.7z *.rar *.tar *.tar.gz *.tgz *.txt)")]; onAccepted: workspaceController.importLocal(selectedFile) }
    SubmitDialog {
        id: shareDialog
        objectName: "shareZipDialog"
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: qsTr("Share ZIP")
        onSubmitRequested: if (window.finishEditing() && workspaceController.exportProject(shareName.text)) accept()
        ColumnLayout {
            width: parent.width
            Label { text: qsTr("ZIP file name"); Layout.fillWidth: true }
            TextField { id: shareName; objectName: "shareZipName"; Layout.fillWidth: true; selectByMouse: true }
            Label { text: qsTr("The ZIP includes comments, proofreading records and their referenced images. LabelQt on PC or Android can continue the review."); Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: mobileDocument.error || workspaceController.message; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    SubmitDialog {
        id: reportDialog
        objectName: "sharePdfDialog"
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: qsTr("Share proofreading PDF")
        onSubmitRequested: if (window.finishEditing() && workspaceController.exportProofreadingPdf(reportName.text, reportScope.currentIndex === 1)) accept()
        ColumnLayout {
            Layout.fillWidth: true
            Label { text: qsTr("PDF file name"); Layout.fillWidth: true }
            TextField { id: reportName; objectName: "reportFileName"; Layout.fillWidth: true; selectByMouse: true }
            ComboBox { id: reportScope; Layout.fillWidth: true; model: [qsTr("All pages in this round"), qsTr("Current page only")] }
            Label {
                text: qsTr("The PDF includes text differences, images and comments. It does not replace the editable project ZIP.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label { text: mobileDocument.error || workspaceController.message; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    SubmitDialog {
        id: renameProjectDialog
        objectName: "renameProjectDialog"
        property string projectKey
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: qsTr("Rename project")
        onSubmitRequested: if (workspaceController.renameWorkspace(projectKey, projectName.text)) accept()
        ColumnLayout {
            width: parent.width
            TextField { id: projectName; Layout.fillWidth: true; maximumLength: 200; selectByMouse: true }
            Label { text: workspaceController.message; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    SubmitDialog {
        id: deleteProjectDialog
        objectName: "deleteProjectDialog"
        property string projectKey
        property string projectTitle
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: qsTr("Delete project")
        onSubmitRequested: if (window.finishEditing() && workspaceController.deleteWorkspace(projectKey)) accept()
        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("Delete the local copy of \"%1\" and its images? This cannot be undone. The original imported file and shared ZIPs will be kept.").arg(deleteProjectDialog.projectTitle)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label { text: mobileDocument.error || workspaceController.message; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    SubmitDialog {
        id: newRoundDialog
        objectName: "reviewRoundDialog"
        property bool completing: false
        property string token
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: completing ? qsTr("Finish this review?") : qsTr("Start a new review?")
        onSubmitRequested: {
            if (!window.finishEditing()) return
            const success = completing ? mobileReview.completeRound(token) : mobileReview.startRound(true, token)
            if (success) accept()
        }
        ColumnLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: newRoundDialog.completing
                    ? qsTr("Freeze the reviewed text and comment bodies? You can still resolve comments and change their linked labels.")
                    : qsTr("The previous round and its notes will be replaced. Share a ZIP first to keep a copy.")
            }
            Label { text: mobileReview.error; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    ScrollDialog {
        id: deleteDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: qsTr("Delete selected label?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (window.finishEditing()) mobileDocument.deleteSelected()
    }
    ReviewNoteDialog {
        id: noteDialog
        parent: Overlay.overlay
        document: mobileDocument
        review: mobileReview
        finishEditing: window.finishEditing
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
    }
    Dialog {
        id: groupsDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 400)
        height: Math.min(window.height - 64, 480)
        title: qsTr("Groups")
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: mobileDocument.groups
                delegate: CheckDelegate {
                    required property string modelData
                    width: ListView.view.width
                    text: modelData
                    checked: mobileDocument.visibleGroups.indexOf(modelData) >= 0
                    onClicked: {
                        let visible = mobileDocument.visibleGroups.slice()
                        if (checked) visible.push(modelData)
                        else visible.splice(visible.indexOf(modelData), 1)
                        if (window.finishEditing()) mobileDocument.setVisibleGroups(visible)
                    }
                }
            }
            RowLayout {
                TextField { id: newGroup; Layout.fillWidth: true; placeholderText: qsTr("New group") }
                Button { text: qsTr("Add"); onClicked: { if (window.finishEditing() && mobileDocument.addGroup(newGroup.text)) newGroup.clear() } }
            }
            Label { text: mobileDocument.error; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    AboutDialog { id: aboutDialog }
}
