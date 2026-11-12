import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var document
    required property var search
    required property var review
    required property var finishEditing
    signal roundRequested
    signal noteRequested
    signal editNoteRequested(var note)
    signal targetRequested(string pageKey, string labelKey)
    signal matchRequested(int row)
    property bool initialized: false
    property var knownGroups: []
    Component.onCompleted: {
        syncGroups();
        initialized = true;
        updateQuery();
    }
    function syncGroups() {
        const names = document.groups;
        if (JSON.stringify(names) !== JSON.stringify(knownGroups))
            knownGroups = names;
    }
    Connections {
        target: root.document
        function onChanged() {
            root.syncGroups();
            Qt.callLater(root.updateQuery);
        }
    }

    function updateQuery() {
        if (!initialized)
            return;
        search.configure(query.text, matchCase.checked, scope.currentIndex, first.value - 1, last.value - 1, group.currentIndex <= 0, group.currentIndex > 0 ? [group.currentText] : []);
    }
    ScrollView {
        id: headerScroll
        padding: 8
        Layout.fillWidth: true
        Layout.preferredHeight: searchHeader.implicitHeight + topPadding + bottomPadding
        Layout.maximumHeight: Layout.preferredHeight
        Layout.fillHeight: true
        Layout.minimumHeight: 0
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            id: searchHeader
            width: headerScroll.availableWidth
            ColumnLayout {
                visible: resultsTab.currentIndex === 0
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    TextField {
                        id: query
                        objectName: "labelSearchInput"
                        Layout.fillWidth: true
                        placeholderText: qsTranslate("ProjectSearchPanel", "Find text")
                        selectByMouse: true
                        onTextChanged: if (!inputMethodComposing)
                            root.updateQuery()
                        onInputMethodComposingChanged: if (!inputMethodComposing)
                            root.updateQuery()
                    }
                    IconButton {
                        iconName: "x"
                        text: qsTr("Clear search")
                        enabled: query.text.length > 0
                        onClicked: query.clear()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: scope
                        objectName: "searchPageScope"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        model: [qsTranslate("ProjectSearchPanel", "Whole project"), qsTranslate("ProjectSearchPanel", "Current page"), qsTranslate("ProjectSearchPanel", "Page range")]
                        onActivated: root.updateQuery()
                    }
                    ComboBox {
                        id: group
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        model: [qsTranslate("ProjectSearchPanel", "All groups")].concat(root.knownGroups)
                        onActivated: root.updateQuery()
                        onModelChanged: {
                            currentIndex = 0;
                            if (query)
                                root.updateQuery();
                        }
                    }
                }
                RowLayout {
                    visible: scope.currentIndex === 2
                    Layout.fillWidth: true
                    SpinBox {
                        id: first
                        objectName: "searchFirstPage"
                        from: 1
                        to: Math.max(1, root.document.pageNames.length)
                        value: 1
                        Layout.fillWidth: true
                        onValueChanged: if (root.initialized) Qt.callLater(root.updateQuery)
                    }
                    Label {
                        text: "-"
                    }
                    SpinBox {
                        id: last
                        objectName: "searchLastPage"
                        from: first.value
                        to: first.to
                        value: to
                        Layout.fillWidth: true
                        onValueChanged: if (root.initialized) Qt.callLater(root.updateQuery)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        id: matchCase
                        text: qsTranslate("ProjectSearchPanel", "Match case")
                        onToggled: root.updateQuery()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    IconButton {
                        iconName: "chevron-left"
                        text: qsTranslate("ProjectSearchPanel", "Previous match")
                        enabled: !root.search.busy && root.search.results.length > 0
                        onClicked: root.matchRequested(root.search.current < 0 ? root.search.results.length - 1 : (root.search.current - 1 + root.search.results.length) % root.search.results.length)
                    }
                    IconButton {
                        iconName: "chevron-right"
                        text: qsTranslate("ProjectSearchPanel", "Next match")
                        enabled: !root.search.busy && root.search.results.length > 0
                        onClicked: root.matchRequested((root.search.current + 1) % root.search.results.length)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: root.review.roundState === "active" ? qsTranslate("Main", "Finish review") : qsTranslate("Main", "Start review")
                    Layout.fillWidth: true
                    enabled: !root.review.busy && root.review.roundState !== "invalid"
                    onClicked: root.roundRequested()
                }
                Button {
                    text: qsTranslate("Main", "Note")
                    enabled: root.review.roundState === "active"
                    onClicked: root.noteRequested()
                }
            }
            Label {
                text: root.review.roundState === "active" ? qsTr("Review in progress") : root.review.roundState === "completed" ? qsTr("Completed review") : ""
                visible: text.length > 0 && root.height >= 320
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }
    TabBar {
        id: resultsTab
        objectName: "inspectionTabs"
        Layout.fillWidth: true
        TabButton {
            text: qsTr("Results")
        }
        TabButton {
            text: qsTranslate("Main", "Changes")
            enabled: !root.review.busy && root.review.roundState !== "none" && root.review.roundState !== "invalid"
            onClicked: if (root.finishEditing())
                root.review.calculateChanges()
        }
        TabButton {
            text: qsTr("Notes")
        }
    }
    ProgressBar {
        visible: root.search.busy || root.review.busy
        indeterminate: true
        Layout.fillWidth: true
    }
    Label {
        visible: resultsTab.currentIndex === 0
        text: root.search.error || (root.search.truncated ? qsTr("First %1 matches; narrow the search.").arg(root.search.results.length) : qsTranslate("ProjectSearchPanel", "Matches: %1").arg(root.search.results.length))
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    ListView {
        objectName: "searchResults"
        visible: resultsTab.currentIndex === 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 48
        clip: true
        model: root.search.results
        ScrollBar.vertical: ScrollBar {}
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: ListView.view.width
            highlighted: index === root.search.current
            onClicked: root.matchRequested(index)
            contentItem: ColumnLayout {
                Label {
                    text: modelData.pageName + "  #" + modelData.number + "  " + modelData.group
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pixelSize: 12
                }
                Label {
                    text: modelData.snippet
                    textFormat: Text.RichText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }
    ListView {
        visible: resultsTab.currentIndex === 1
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.review.changes
        ScrollBar.vertical: ScrollBar {}
        delegate: ItemDelegate {
            required property var modelData
            width: ListView.view.width
            contentItem: ColumnLayout {
                Label {
                    text: modelData.pageName + (modelData.number > 0 ? "  #" + modelData.number : "")
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }
                Label {
                    text: modelData.html
                    textFormat: Text.RichText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    text: modelData.noteText
                    visible: text.length > 0
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }
    }
    ReviewNotesPanel {
        visible: resultsTab.currentIndex === 2
        Layout.fillWidth: true
        Layout.fillHeight: true
        document: root.document
        review: root.review
        finishEditing: root.finishEditing
        onEditRequested: note => root.editNoteRequested(note)
        onJumpRequested: (pageKey, labelKey) => root.targetRequested(pageKey, labelKey)
    }
}
