import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var workspaceModel
    property bool busy: false
    property bool showHeading: true
    signal importRequested()
    signal openRequested(string key)
    signal renameRequested(string key, string title)
    signal deleteRequested(string key, string title)

    spacing: 8
    Label {
        visible: root.showHeading
        text: qsTranslate("Main", "Local projects")
        font.pixelSize: 22
        Layout.fillWidth: true
        elide: Text.ElideRight
    }
    Button {
        objectName: "importWorkspaceButton"
        text: qsTranslate("Main", "Import ZIP or TXT")
        Layout.fillWidth: true
        enabled: !root.busy
        onClicked: root.importRequested()
    }
    ListView {
        id: list
        objectName: "workspaceList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.workspaceModel
        ScrollBar.vertical: ScrollBar {}
        Label {
            anchors.centerIn: parent
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: list.count === 0
            text: qsTr("No local projects")
            color: palette.placeholderText
        }
        delegate: RowLayout {
            required property string workspaceKey
            required property string workspaceTitle
            required property date modifiedAt
            width: ListView.view.width
            spacing: 0
            ItemDelegate {
                objectName: "openWorkspace_" + workspaceKey
                Layout.fillWidth: true
                enabled: !root.busy
                onClicked: root.openRequested(workspaceKey)
                contentItem: ColumnLayout {
                    Label { text: workspaceTitle; textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label {
                        text: qsTranslate("Main", "Edited %1").arg(Qt.formatDateTime(modifiedAt, Qt.locale().dateTimeFormat(Locale.ShortFormat)))
                        color: palette.placeholderText
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
            IconButton {
                iconName: "ellipsis"
                text: qsTranslate("Main", "Manage project")
                enabled: !root.busy
                onClicked: projectMenu.open()
                Menu {
                    id: projectMenu
                    MenuItem { text: qsTranslate("Main", "Rename project"); onTriggered: root.renameRequested(workspaceKey, workspaceTitle) }
                    MenuItem { text: qsTranslate("Main", "Delete project"); onTriggered: root.deleteRequested(workspaceKey, workspaceTitle) }
                }
            }
        }
    }
}
