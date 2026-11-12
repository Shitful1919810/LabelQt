import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SubmitDialog {
    id: root
    required property var document
    required property var review
    required property var finishEditing
    property string noteKey: ""
    property string targetPage: ""
    property string targetLabel: ""
    property string pageName: ""
    property string quote: ""
    property string token: ""
    property bool frozen: false
    objectName: "reviewNoteDialog"
    modal: true
    title: qsTr("Proofreading comment")

    function createNote() {
        if (!finishEditing())
            return;
        review.clearError();
        noteKey = "";
        targetPage = document.pageId;
        targetLabel = document.selectedLabel;
        pageName = document.pageNames[document.pageIndex] || "";
        quote = document.textDraft;
        token = review.editToken();
        frozen = false;
        scope.currentIndex = targetLabel.length > 0 ? 0 : 1;
        body.text = "";
        open();
    }
    function showNote(note) {
        if (!finishEditing())
            return;
        review.clearError();
        noteKey = note.key;
        targetPage = note.pageKey;
        targetLabel = note.labelKey;
        pageName = note.pageName;
        quote = note.quote;
        token = review.editToken();
        frozen = review.roundState !== "active";
        scope.currentIndex = targetLabel.length > 0 ? 0 : 1;
        body.text = note.text;
        open();
    }
    onSubmitRequested: {
        Qt.inputMethod.commit();
        if (frozen) {
            accept();
            return;
        }
        if (!finishEditing())
            return;
        const success = noteKey.length > 0 ? review.updateNote(noteKey, body.text, token) : review.addNote(targetPage, scope.currentIndex === 0 ? targetLabel : "", body.text, token);
        if (success)
            accept();
    }
    onClosed: Qt.inputMethod.hide()
    ColumnLayout {
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.pageName
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            ComboBox {
                id: scope
                objectName: "noteTargetScope"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: root.noteKey.length === 0
                enabled: root.targetLabel.length > 0
                model: [qsTr("Selected label"), qsTr("Current page")]
            }
        }
        TextArea {
            id: body
            objectName: "reviewNoteText"
            Layout.fillWidth: true
            Layout.minimumHeight: 120
            textFormat: TextEdit.PlainText
            readOnly: root.frozen
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            placeholderText: qsTr("Comment")
        }
        Label {
            visible: root.quote.length > 0 && scope.currentIndex === 0
            text: root.quote
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 4
            elide: Text.ElideRight
            opacity: 0.7
            Layout.fillWidth: true
        }
        Label {
            visible: root.frozen
            text: qsTr("Completed comments are read-only. Their resolved status and linked label can still be changed.")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: root.review.error
            visible: text.length > 0
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
