import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// Every highlight and note across the library, newest first.
Item {
    id: view

    property var model: null
    signal openBook(int bookId, string cfi)

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 420)
        spacing: Theme.gap
        visible: !view.model || view.model.count === 0

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 13
            text: "No highlights or notes yet.\nSelect text while reading to make one."
        }
    }

    ListView {
        id: list
        /// Room the cards leave at the right edge for the scroll bar, which
        /// floats over the content rather than taking space from it.
        readonly property int scrollGutter: 10

        anchors.fill: parent
        anchors.margins: Theme.pad
        // ...given back here, so a card still sits Theme.pad from both edges.
        anchors.rightMargin: Theme.pad - list.scrollGutter
        visible: view.model && view.model.count > 0
        model: view.model
        spacing: Theme.gap
        clip: true
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            required property var model

            width: list.width - list.scrollGutter
            height: content.implicitHeight + 2 * Theme.pad
            radius: Theme.radius
            color: rowHover.hovered ? Theme.surfaceHover : Theme.surface
            border.width: 1
            border.color: Theme.cardBorder

            HoverHandler { id: rowHover }
            TapHandler { onTapped: view.openBook(model.bookId, model.cfi) }

            ColumnLayout {
                id: content
                anchors.fill: parent
                anchors.margins: Theme.pad
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap

                    Label {
                        // A highlight and a note are different things at a glance.
                        text: model.isHighlight ? "▌" : "✎"
                        color: Theme.accent
                        font.pixelSize: 13
                    }

                    Label {
                        text: model.bookTitle
                        color: Theme.text
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: model.createdAt
                        color: Theme.muted
                        font.pixelSize: 11
                    }

                    IconButton {
                        path: Icons.trash
                        tooltip: "Delete"
                        onClicked: view.model.remove(model.noteId)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: model.quote !== ""
                    text: model.quote
                    color: Theme.muted
                    font.pixelSize: 12
                    font.italic: true
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: model.body !== ""
                    text: model.body
                    color: Theme.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
