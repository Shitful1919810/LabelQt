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
        if (mobileReview.roundState === "active") mobileReview.completeRound()
        else if (mobileReview.roundState === "completed") newRoundDialog.open()
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
                visible: mobileDocument.opened
                onClicked: actions.open()
                Menu {
                    id: actions
                    MenuItem { text: qsTr("Save"); enabled: mobileDocument.opened; onTriggered: if (window.finishEditing()) mobileDocument.save() }
                    MenuItem {
                        objectName: "shareZipAction"
                        text: qsTr("Share ZIP")
                        enabled: mobileDocument.opened && !workspaceController.busy
                        onTriggered: {
                            if (window.finishEditing()) {
                                workspaceController.clearMessage()
                                shareName.text = workspaceController.suggestedShareName
                                shareDialog.open()
                            }
                        }
                    }
                    MenuItem { text: qsTr("Groups"); enabled: mobileDocument.opened; onTriggered: groupsDialog.open() }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ProgressBar { Layout.fillWidth: true; visible: workspaceController.busy; indeterminate: true }
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
            visible: mobileDocument.opened && !window.editingText
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
            visible: mobileDocument.opened && !window.editingText
            Layout.fillWidth: true
            currentIndex: window.panel
            onCurrentIndexChanged: { window.finishEditing(); window.panel = currentIndex }
            TabButton { text: qsTr("Image") }
            TabButton { text: qsTr("Labels") }
            TabButton { text: qsTr("Review") }
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
                visible: window.wide || window.panel === 0 || window.editingText
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
                    visible: !window.editingText
                    Layout.alignment: Qt.AlignHCenter
                    ButtonGroup { id: tools }
                    Button { text: qsTr("Browse"); checkable: true; checked: window.canvasTool === 0; ButtonGroup.group: tools; onClicked: window.canvasTool = 0 }
                    Button { text: qsTr("Add"); checkable: true; checked: window.canvasTool === 1; ButtonGroup.group: tools; onClicked: window.canvasTool = 1 }
                    Button { text: qsTr("Move"); checkable: true; checked: window.canvasTool === 2; ButtonGroup.group: tools; onClicked: window.canvasTool = 2 }
                    IconButton { iconName: "maximize"; text: qsTr("Fit image"); onClicked: canvas.resetView() }
                }
            }
            ListView {
                visible: window.wide || (window.panel === 1 && !window.editingText)
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
                        Label { text: labelText || qsTr("Empty label"); textFormat: Text.PlainText; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
            }
        }
        ColumnLayout {
            visible: mobileDocument.opened && window.panel === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                Layout.fillWidth: true
                Button { text: mobileReview.roundState === "active" ? qsTr("Finish review") : qsTr("Start review"); enabled: !mobileReview.busy && mobileReview.roundState !== "invalid"; onClicked: window.changeReviewState() }
                Button { text: qsTr("Changes"); enabled: !mobileReview.busy && mobileReview.roundState !== "none"; onClicked: if (window.finishEditing()) mobileReview.calculateChanges() }
                Button { text: qsTr("Note"); enabled: mobileReview.roundState === "active"; onClicked: noteDialog.open() }
            }
            BusyIndicator { running: mobileReview.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    Repeater {
                        model: mobileReview.notes
                        delegate: CheckDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.pageName + ": " + modelData.text
                            checked: modelData.resolved
                            onClicked: if (window.finishEditing()) mobileReview.resolveNote(modelData.key, checked)
                        }
                    }
                    Repeater {
                        model: mobileReview.changes
                        delegate: Frame {
                            required property var modelData
                            Layout.fillWidth: true
                            ColumnLayout {
                                anchors.fill: parent
                                Label { text: modelData.pageName + "  #" + modelData.number; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Label { text: modelData.html; textFormat: Text.RichText; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }
        Frame {
            id: editorPanel
            visible: mobileDocument.selectedLabel.length > 0 && window.panel !== 2
            Layout.fillWidth: true
            padding: window.editingText ? 4 : 12
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    visible: !window.editingText
                    ComboBox { model: mobileDocument.groups; currentIndex: model.indexOf(mobileDocument.selectedGroup); Layout.fillWidth: true; onActivated: if (window.finishEditing()) mobileDocument.setGroup(currentText) }
                    IconButton { iconName: "trash-2"; text: qsTr("Delete label"); onClicked: deleteDialog.open() }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: window.editingText ? Math.min(104, Math.max(64, window.contentItem.height * 0.27)) : Math.min(150, Math.max(70, window.height * 0.16))
                    Layout.maximumHeight: Layout.preferredHeight
                    clip: true
                    TextArea {
                        id: editor
                        objectName: "labelTextEditor"
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
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
            Label { text: mobileDocument.error || workspaceController.message; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
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
    Dialog {
        id: newRoundDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: qsTr("Start a new review?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (window.finishEditing()) mobileReview.startRound(true)
        Label { width: parent.width; wrapMode: Text.Wrap; text: qsTr("The previous round will be replaced. Export a ZIP first to keep it.") }
    }
    Dialog {
        id: deleteDialog
        modal: true
        anchors.centerIn: parent
        title: qsTr("Delete selected label?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (window.finishEditing()) mobileDocument.deleteSelected()
    }
    SubmitDialog {
        id: noteDialog
        modal: true
        property string targetPage
        property string targetLabel
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        title: qsTr("Review note")
        onOpened: { targetPage = mobileDocument.pageId; targetLabel = mobileDocument.selectedLabel }
        onSubmitRequested: {
            if (window.finishEditing() && mobileReview.addNote(targetPage, targetLabel, noteText.text)) {
                noteText.clear()
                accept()
            }
        }
        onRejected: noteText.clear()
        ColumnLayout {
            width: parent.width
            TextArea { id: noteText; Layout.fillWidth: true; Layout.preferredHeight: 160; wrapMode: TextEdit.Wrap; placeholderText: qsTr("Note for the selected label or current page") }
            Label { text: mobileReview.error; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
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
                Button { text: qsTr("Add"); onClicked: { if (window.finishEditing()) mobileDocument.addGroup(newGroup.text); newGroup.clear() } }
            }
        }
    }
}
