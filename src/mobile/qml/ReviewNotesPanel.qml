import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var document
    required property var review
    required property var finishEditing
    signal editRequested(var note)
    signal jumpRequested(string pageKey, string labelKey)
    readonly property var filteredNotes: review.notes.filter(note => (pageScope.currentIndex === 0 || note.pageKey === document.pageId) && (state.currentIndex === 0 || (state.currentIndex === 1 && !note.resolved) || (state.currentIndex === 2 && note.resolved) || (state.currentIndex === 3 && note.orphan)))

    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            id: pageScope
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: [qsTr("All pages"), qsTr("Current page")]
        }
        ComboBox {
            id: state
            objectName: "noteStateFilter"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: [qsTr("All comments"), qsTr("Unresolved"), qsTr("Resolved"), qsTr("Missing target")]
        }
    }
    Label {
        visible: root.filteredNotes.length === 0
        text: qsTr("No comments in this view.")
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    ListView {
        objectName: "proofreadNotes"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 48
        clip: true
        model: root.filteredNotes
        ScrollBar.vertical: ScrollBar {}
        delegate: ItemDelegate {
            id: row
            required property var modelData
            width: ListView.view.width
            onClicked: root.editRequested(modelData)
            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: row.modelData.pageName + " · " + (row.modelData.labelKey.length > 0 ? qsTr("Label comment") : qsTr("Page comment"))
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.pixelSize: 12
                    }
                    IconButton {
                        iconName: "ellipsis"
                        text: qsTr("Comment actions")
                        onClicked: actions.popup()
                        Menu {
                            id: actions
                            MenuItem {
                                text: qsTr("Go to target")
                                enabled: !row.modelData.orphan
                                onTriggered: root.jumpRequested(row.modelData.pageKey, row.modelData.labelKey)
                            }
                            MenuItem {
                                text: qsTr("Attach to selected label")
                                enabled: root.document.selectedLabel.length > 0
                                onTriggered: confirm.prepare(row.modelData.key, false)
                            }
                            MenuItem {
                                text: qsTr("Delete comment")
                                enabled: root.review.roundState === "active"
                                onTriggered: confirm.prepare(row.modelData.key, true)
                            }
                        }
                    }
                }
                Label {
                    text: row.modelData.quote
                    visible: text.length > 0
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    opacity: 0.65
                    Layout.fillWidth: true
                }
                Label {
                    text: row.modelData.text
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        checked: row.modelData.resolved
                        text: checked ? qsTr("Resolved") : qsTr("Unresolved")
                        Material.accent: checked ? "#25834b" : "#b27614"
                        Material.foreground: checked ? (Material.theme === Material.Dark ? "#86d99c" : "#226c3d") : (Material.theme === Material.Dark ? "#ffd18a" : "#875400")
                        onClicked: if (root.finishEditing())
                            root.review.resolveNote(row.modelData.key, checked)
                    }
                    Label {
                        visible: row.modelData.orphan
                        text: qsTr("Missing target")
                        color: Material.theme === Material.Dark ? "#ffb870" : "#a74800"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
    SubmitDialog {
        id: confirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 440)
        modal: true
        property bool removing: false
        property string noteKey
        property string pageKey
        property string labelKey
        property string token
        title: removing ? qsTr("Delete comment") : qsTr("Attach to selected label")
        function prepare(key, remove) {
            if (!root.finishEditing())
                return;
            root.review.clearError();
            noteKey = key;
            removing = remove;
            pageKey = root.document.pageId;
            labelKey = root.document.selectedLabel;
            token = root.review.editToken();
            open();
        }
        onSubmitRequested: {
            if (!root.finishEditing())
                return;
            const success = removing ? root.review.removeNote(noteKey, token) : root.review.relinkNote(noteKey, pageKey, labelKey, token);
            if (success)
                accept();
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: confirm.removing ? qsTr("Delete this comment?") : qsTr("Attach this comment to the selected label? Its original quote will be preserved.")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: root.review.error
            visible: text.length > 0
        }
    }
}
