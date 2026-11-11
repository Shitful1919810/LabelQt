import QtQuick.Controls

ScrollDialog {
    id: control
    signal submitRequested()
    standardButtons: Dialog.Cancel

    // AcceptRole closes Dialog before the caller can reject invalid input.
    // An action button leaves acceptance to the owner of the operation.
    footer: DialogButtonBox {
        Button {
            objectName: "submitDialogButton"
            text: qsTr("Confirm")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: control.submitRequested()
        }
    }
}
