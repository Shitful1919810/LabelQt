import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    required property string iconName
    display: AbstractButton.IconOnly
    icon.source: Qt.resolvedUrl("icons/" + iconName + ".svg")
    icon.width: 22
    icon.height: 22
    implicitWidth: 48
    implicitHeight: 48
    Accessible.name: text
    ToolTip.text: text
    ToolTip.visible: hovered
    focusPolicy: Qt.NoFocus
}
