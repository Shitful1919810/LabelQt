import QtQuick.Controls

Dialog {
    id: control
    signal submitRequested()

    // AcceptRole closes Dialog before the caller can reject invalid input.
    // An action button leaves acceptance to the owner of the operation.
    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Cancel
        Button {
            objectName: "submitDialogButton"
            text: qsTr("Confirm")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: control.submitRequested()
        }
    }
}
