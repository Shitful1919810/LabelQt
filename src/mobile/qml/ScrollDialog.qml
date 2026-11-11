import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: control
    default property alias bodyData: body.data
    height: Math.min(implicitHeight, Math.max(0, parent.height - 32))

    contentItem: ScrollView {
        id: scroll
        implicitHeight: body.implicitHeight
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: body
            width: scroll.availableWidth
        }
    }
}
