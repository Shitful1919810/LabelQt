import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollDialog {
    objectName: "aboutDialog"
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 440)
    title: qsTr("About LabelQt")
    standardButtons: Dialog.Close

    Label {
        text: "LabelQt"
        font.pixelSize: 24
        font.bold: true
        Layout.fillWidth: true
    }
    Label {
        objectName: "aboutVersion"
        text: qsTr("Version: %1").arg(Qt.application.version)
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label {
        text: qsTr("Author: %1").arg("Shitful")
        Layout.fillWidth: true
    }
    Label {
        text: '<a href="https://github.com/Shitful1919810/LabelQt">github.com/Shitful1919810/LabelQt</a>'
        textFormat: Text.RichText
        wrapMode: Text.WrapAnywhere
        Layout.fillWidth: true
        onLinkActivated: link => Qt.openUrlExternally(link)
    }
}
