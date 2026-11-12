pragma ComponentBehavior: Bound

import QtQuick 6.8
import QtQuick.Controls 6.8

Item {
    id: root

    property url imageSource
    property var labelModel: null
    property string selectedLabel: ""
    property int toolMode: 0

    signal labelTapped(string key)
    signal labelAdded(real x, real y)
    signal labelMoved(string key, real x, real y)

    implicitWidth: 640
    implicitHeight: 480
    clip: true

    function resetView() {
        if (!view)
            return;
        cancelFocusRequest();
        cancelInteraction();
        view.zoom = 1;
        view.centerX = 0.5;
        view.centerY = 0.5;
    }

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value));
    }

    function focusSelected() {
        cancelFocusRequest();
        if (!view || selectedLabel.length === 0 || imageSource.toString().length === 0)
            return false;
        cancelInteraction();
        view.focusSource = imageSource;
        view.focusKey = selectedLabel;
        Qt.callLater(root.applyPendingFocus);
        return true;
    }

    function cancelFocusRequest() {
        if (view) {
            view.focusSource = "";
            view.focusKey = "";
        }
    }

    function applyPendingFocus() {
        if (!view || view.focusKey.length === 0)
            return;
        if (view.focusSource !== imageSource || view.focusKey !== selectedLabel) {
            cancelFocusRequest();
            return;
        }
        const marker = markerForKey(view.focusKey);
        if (!view.ready || !marker)
            return;
        cancelFocusRequest();
        view.centerX = marker.positionX;
        view.centerY = marker.positionY;
    }

    function constrainView() {
        if (!view || !view.ready)
            return;
        const halfX = Math.min(0.5, width / (2 * view.imageWidth));
        const halfY = Math.min(0.5, height / (2 * view.imageHeight));
        view.centerX = clamp(view.centerX, halfX, 1 - halfX);
        view.centerY = clamp(view.centerY, halfY, 1 - halfY);
    }

    function normalized(point) {
        return Qt.point((point.x - page.x) / view.imageWidth, (point.y - page.y) / view.imageHeight);
    }

    function insideImage(point) {
        if (!view.ready || point.x < 0 || point.y < 0 || point.x > width || point.y > height)
            return false;
        const p = normalized(point);
        return p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1;
    }

    function markerForKey(key) {
        for (let i = 0; i < markers.count; ++i) {
            const marker = markers.itemAt(i) as CanvasMarker;
            if (marker && marker.labelKey === key)
                return marker;
        }
        return null;
    }

    function hitMarkers(point) {
        const hits = [];
        for (let i = 0; i < markers.count; ++i) {
            const marker = markers.itemAt(i) as CanvasMarker;
            if (!marker || !marker.visible)
                continue;
            const dx = point.x - marker.x - marker.width / 2;
            const dy = point.y - marker.y - marker.height / 2;
            if (dx * dx + dy * dy <= 24 * 24)
                hits.push({
                    key: marker.labelKey,
                    number: marker.markerNumber,
                    text: marker.labelText,
                    group: marker.groupName
                });
        }
        return hits;
    }

    function tapAt(point) {
        if (view.blockEdits || !view.ready)
            return;
        cancelFocusRequest();
        const hits = hitMarkers(point);
        if (hits.length === 1) {
            labelTapped(hits[0].key);
        } else if (hits.length > 1) {
            view.candidates = hits;
            view.candidatePoint = point;
            candidates.open();
        } else if (insideImage(point)) {
            if (toolMode === 1) {
                const p = normalized(point);
                labelAdded(p.x, p.y);
            } else {
                // The empty key requests deselection; selection itself remains parent-owned.
                labelTapped("");
            }
        }
    }

    function cancelDrag() {
        view.dragKind = 0;
        view.moveKey = "";
    }

    function cancelInteraction() {
        if (!view)
            return;
        cancelDrag();
        view.blockEdits = true;
        view.pinchValid = false;
        view.pinchPoints = [];
        if (candidates)
            candidates.close();
    }

    function beginDrag(point) {
        cancelFocusRequest();
        cancelDrag();
        // A finger left over from a pinch is not a new pan or marker gesture.
        // The next fresh press clears blockEdits through TapHandler.
        if (view.blockEdits || !view.ready)
            return;
        view.dragStart = point;
        view.dragCenter = Qt.point(view.centerX, view.centerY);
        view.dragKind = 1;
        if (view.blockEdits || toolMode !== 2 || selectedLabel.length === 0)
            return;
        const hits = hitMarkers(point);
        for (let i = 0; i < hits.length; ++i) {
            if (hits[i].key !== selectedLabel)
                continue;
            const marker = markerForKey(selectedLabel);
            if (!marker)
                return;
            view.moveStart = Qt.point(marker.positionX, marker.positionY);
            view.movePreview = view.moveStart;
            view.moveKey = marker.labelKey;
            view.dragKind = 2;
            return;
        }
    }

    function updateDrag(point) {
        if (!view.ready || view.dragKind === 0)
            return;
        const dx = (point.x - view.dragStart.x) / view.imageWidth;
        const dy = (point.y - view.dragStart.y) / view.imageHeight;
        if (view.dragKind === 2) {
            view.movePreview = Qt.point(clamp(view.moveStart.x + dx, 0, 1), clamp(view.moveStart.y + dy, 0, 1));
        } else {
            view.centerX = view.dragCenter.x - dx;
            view.centerY = view.dragCenter.y - dy;
            constrainView();
        }
    }

    function finishDrag(point) {
        updateDrag(point);
        const key = view.moveKey;
        const destination = view.movePreview;
        const marker = markerForKey(key);
        const commit = view.dragKind === 2 && !view.blockEdits && view.ready && toolMode === 2 && selectedLabel === key && marker && marker.positionX === view.moveStart.x && marker.positionY === view.moveStart.y && (Math.abs(destination.x - view.moveStart.x) > 0.000001 || Math.abs(destination.y - view.moveStart.y) > 0.000001);
        // Clear before emitting: the receiver may synchronously reset/delete the model.
        cancelDrag();
        if (commit)
            labelMoved(key, destination.x, destination.y);
    }

    function zoomAt(point, zoom) {
        if (!view.ready)
            return;
        cancelFocusRequest();
        const anchor = normalized(point);
        view.zoom = clamp(zoom, 1, 8);
        view.centerX = anchor.x - (point.x - width / 2) / view.imageWidth;
        view.centerY = anchor.y - (point.y - height / 2) / view.imageHeight;
        constrainView();
    }

    function updatePinch() {
        if (!pinch.active || !view.pinchValid || !view.ready)
            return;
        view.zoom = clamp(view.pinchZoom * pinch.activeScale, 1, 8);
        view.centerX = view.pinchAnchor.x - (pinch.centroid.position.x - width / 2) / view.imageWidth;
        view.centerY = view.pinchAnchor.y - (pinch.centroid.position.y - height / 2) / view.imageHeight;
        constrainView();
    }

    onImageSourceChanged: resetView()
    onLabelModelChanged: cancelInteraction()
    onSelectedLabelChanged: {
        cancelFocusRequest();
        if (view && view.moveKey !== selectedLabel)
            cancelDrag();
    }
    onToolModeChanged: cancelInteraction()
    onWidthChanged: {
        cancelInteraction();
        constrainView();
    }
    onHeightChanged: {
        cancelInteraction();
        constrainView();
    }
    onVisibleChanged: if (!visible)
        cancelInteraction()
    onEnabledChanged: if (!enabled)
        cancelInteraction()
    Component.onCompleted: resetView()

    QtObject {
        id: view

        property size previewSize: Qt.size(0, 0)
        readonly property bool ready: page.status === Image.Ready && previewSize.width > 0 && previewSize.height > 0 && root.width > 0 && root.height > 0
        onReadyChanged: if (ready) Qt.callLater(root.applyPendingFocus)
        property url focusSource
        property string focusKey: ""
        readonly property real fit: ready ? Math.min(Math.max(1, root.width - 16) / previewSize.width, Math.max(1, root.height - 16) / previewSize.height) : 0
        readonly property real imageWidth: previewSize.width * fit * zoom
        readonly property real imageHeight: previewSize.height * fit * zoom
        property real zoom: 1
        property real centerX: 0.5
        property real centerY: 0.5
        property bool blockEdits: true
        property int dragKind: 0 // 0 idle, 1 pan, 2 uncommitted marker preview
        property point dragStart
        property point dragCenter
        property string moveKey: ""
        property point moveStart
        property point movePreview
        property bool pinchValid: false
        property real pinchZoom: 1
        property point pinchAnchor
        property var pinchPoints: []
        property var candidates: []
        property point candidatePoint
    }

    SystemPalette {
        id: colors
    }

    Rectangle {
        anchors.fill: parent
        color: colors.window
    }

    Image {
        id: page
        objectName: "projectCanvasImage"

        x: root.width / 2 - view.centerX * view.imageWidth
        y: root.height / 2 - view.centerY * view.imageHeight
        width: view.imageWidth
        height: view.imageHeight
        source: root.imageSource
        // Fixed decode budget, independent of zoom and viewport size. Some codecs still
        // decode at full size internally; this is a preview, not the M2 tiled-image pipeline.
        sourceSize: Qt.size(2048, 3072)
        // The canvas supplies aspect-correct geometry. PreserveAspectFit can enlarge
        // the requested decode size beyond sourceSize for wide or tall images.
        fillMode: Image.Stretch
        autoTransform: true
        asynchronous: true
        cache: false
        smooth: true
        mipmap: true
        visible: view.ready
        onStatusChanged: {
            if (status === Image.Error)
                root.cancelFocusRequest();
            if (status !== Image.Ready) {
                view.previewSize = Qt.size(0, 0);
                root.cancelInteraction();
            } else {
                // Snapshot decoded dimensions: Image's implicit-size updates must not feed
                // back into its own width binding during an asynchronous source replacement.
                view.previewSize = Qt.size(implicitWidth, implicitHeight);
                root.constrainView();
            }
        }
    }

    Repeater {
        id: markers
        model: root.labelModel
        onItemAdded: Qt.callLater(root.applyPendingFocus)
        onItemRemoved: root.cancelInteraction()

        delegate: CanvasMarker {}
    }

    component CanvasMarker: Item {
        id: marker

        required property string labelKey
        required property string labelText
        required property int markerNumber
        required property real positionX
        required property real positionY
        required property string groupName
        required property color markerColor
        required property bool markerSquare
        readonly property bool selected: labelKey === root.selectedLabel
        readonly property bool previewing: view.moveKey === labelKey && view.dragKind === 2
        readonly property point location: previewing ? view.movePreview : Qt.point(positionX, positionY)

        objectName: "projectMarker_" + labelKey
        x: page.x + location.x * view.imageWidth - width / 2
        y: page.y + location.y * view.imageHeight - height / 2
        width: 48
        height: 48
        z: selected ? 2 : 1
        visible: view.ready && isFinite(positionX) && isFinite(positionY) && positionX >= 0 && positionX <= 1 && positionY >= 0 && positionY <= 1
        activeFocusOnTab: visible
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Label %1").arg(markerNumber)
        Accessible.description: groupName + (groupName.length > 0 ? ": " : "") + labelText
        Accessible.onPressAction: root.labelTapped(marker.labelKey)
        Keys.onReturnPressed: root.labelTapped(marker.labelKey)
        Keys.onSpacePressed: root.labelTapped(marker.labelKey)
        onPositionXChanged: if (view.moveKey === labelKey)
            root.cancelInteraction()
        onPositionYChanged: if (view.moveKey === labelKey)
            root.cancelInteraction()
        onLabelKeyChanged: root.cancelInteraction()

        Rectangle {
            anchors.centerIn: parent
            width: 28
            height: 28
            radius: marker.markerSquare ? 4 : 14
            visible: marker.selected || marker.activeFocus
            color: "transparent"
            border.width: 2
            border.color: colors.highlight
        }

        Rectangle {
            anchors.centerIn: parent
            objectName: "markerBadge"
            width: 22
            height: 22
            radius: marker.markerSquare ? 2 : 11
            color: marker.markerColor
            border.width: 2
            border.color: "#ffffff"

            Text {
                anchors.fill: parent
                anchors.margins: 2
                text: marker.markerNumber
                textFormat: Text.PlainText
                color: "#ffffff"
                font.pixelSize: 11
                font.bold: true
                fontSizeMode: Text.Fit
                minimumPixelSize: 8
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // One set of handlers owns all canvas gestures, including presses over markers.
    TapHandler {
        id: tap
        enabled: view.ready && !candidates.visible
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onGrabChanged: (transition, point) => {
            if (transition === PointerDevice.GrabPassive && point.state === EventPoint.Pressed && view.pinchPoints.length === 0 && !pinch.active)
                view.blockEdits = false;
        }
        onTapped: (point, button) => root.tapAt(root.mapFromItem(null, point.scenePosition))
        onDoubleTapped: (point, button) => {
            const local = root.mapFromItem(null, point.scenePosition);
            if (!view.blockEdits && root.toolMode === 0 && root.hitMarkers(local).length === 0)
                root.zoomAt(local, view.zoom < 2 ? 2.5 : 1);
        }
    }

    DragHandler {
        id: drag
        target: null
        enabled: view.ready && !candidates.visible
        acceptedButtons: Qt.LeftButton
        minimumPointCount: 1
        maximumPointCount: 1
        onActiveChanged: {
            if (active) {
                root.beginDrag(centroid.pressPosition);
                root.updateDrag(centroid.position);
            } else {
                // Inactivity also means an extra finger arrived, not necessarily release.
                // Only the Released/UngrabExclusive path below may commit an edit.
                Qt.callLater(function () {
                    if (!drag.active)
                        root.cancelDrag();
                });
            }
        }
        onCentroidChanged: if (active)
            root.updateDrag(centroid.position)
        onCanceled: root.cancelDrag()
        onGrabChanged: (transition, point) => {
            if (transition === PointerDevice.UngrabExclusive && point.state === EventPoint.Released)
                root.finishDrag(root.mapFromItem(null, point.scenePosition));
            else if (transition === PointerDevice.CancelGrabExclusive || transition === PointerDevice.CancelGrabPassive)
                root.cancelDrag();
        }
    }

    PinchHandler {
        id: pinch
        target: null
        enabled: view.ready && !candidates.visible
        minimumPointCount: 2
        maximumPointCount: 2
        rotationAxis.enabled: false
        onGrabChanged: (transition, point) => {
            const ids = view.pinchPoints.slice();
            const index = ids.indexOf(point.id);
            if (transition === PointerDevice.GrabPassive || transition === PointerDevice.GrabExclusive) {
                if (index < 0)
                    ids.push(point.id);
                view.blockEdits = true;
                root.cancelDrag();
            } else if ((transition === PointerDevice.UngrabPassive || transition === PointerDevice.CancelGrabPassive || transition === PointerDevice.CancelGrabExclusive || (transition === PointerDevice.UngrabExclusive && point.state === EventPoint.Released)) && index >= 0) {
                ids.splice(index, 1);
            }
            view.pinchPoints = ids;
        }
        onCentroidChanged: {
            // Centroid updates when two points are accepted, before the pinch threshold.
            // Thus even a stationary second finger cancels a pending marker move/tap.
            view.blockEdits = true;
            root.cancelDrag();
        }
        onActiveChanged: {
            if (active) {
                view.pinchZoom = view.zoom;
                view.pinchAnchor = root.normalized(centroid.position);
                view.pinchValid = true;
            } else {
                view.pinchValid = false;
            }
        }
        // Transform only on actual gesture deltas, not centroid/activeScale resets
        // while touch points are being released.
        onScaleChanged: root.updatePinch()
        onTranslationChanged: root.updatePinch()
    }

    WheelHandler {
        target: null
        enabled: view.ready && !drag.active && !pinch.active && !candidates.visible
        onWheel: event => {
            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y;
            root.zoomAt(Qt.point(event.x, event.y), view.zoom * Math.pow(1.0015, delta));
            event.accepted = true;
        }
    }

    Connections {
        target: root.labelModel
        ignoreUnknownSignals: true
        function onModelAboutToBeReset() {
            root.cancelInteraction();
        }
        function onRowsAboutToBeRemoved() {
            root.cancelInteraction();
        }
        function onLayoutAboutToBeChanged() {
            root.cancelInteraction();
        }
    }

    Connections {
        target: Qt.application
        function onStateChanged(state) {
            if (state !== Qt.ApplicationActive)
                root.cancelInteraction();
        }
    }

    Text {
        anchors.centerIn: parent
        width: Math.max(0, root.width - 32)
        visible: !view.ready
        text: page.status === Image.Loading ? qsTr("Loading image...") : qsTr("Image unavailable")
        color: colors.windowText
        font.pixelSize: 14
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    Popup {
        id: candidates
        objectName: "projectCanvasCandidates"
        parent: root
        width: Math.max(1, Math.min(320, root.width - 16))
        height: Math.max(1, Math.min(48 + candidateList.contentHeight, root.height - 16))
        x: root.clamp(view.candidatePoint.x - width / 2, 8, Math.max(8, root.width - width - 8))
        y: root.clamp(view.candidatePoint.y + 24, 8, Math.max(8, root.height - height - 8))
        padding: 8
        focus: true
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 6
            color: colors.window
            border.color: colors.mid
        }

        contentItem: Item {
            Label {
                id: candidateTitle
                width: parent.width
                height: 32
                text: qsTr("Select label")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }

            ListView {
                id: candidateList
                anchors.top: candidateTitle.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                clip: true
                model: view.candidates
                boundsBehavior: Flickable.StopAtBounds
                delegate: ItemDelegate {
                    id: candidateDelegate
                    required property var modelData
                    width: candidateList.width
                    height: 64
                    text: qsTr("Label %1").arg(modelData.number) + (modelData.group ? " - " + modelData.group : "") + (modelData.text ? "\n" + modelData.text : "")
                    contentItem: Text {
                        text: candidateDelegate.text
                        color: colors.windowText
                        textFormat: Text.PlainText
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        const key = modelData.key;
                        candidates.close();
                        if (root.markerForKey(key))
                            root.labelTapped(key);
                    }
                }
            }
        }
    }
}
