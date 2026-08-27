import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The reading queue's own header.
//
// Every other page in the library is sorted for you; this one is not, and
// dragging has no affordance of its own to announce that. So the page says it.
Rectangle {
    id: help

    property int count: 0

    implicitHeight: layout.implicitHeight + 2 * Theme.pad
    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.cardBorder

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.pad
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            VectorIcon {
                width: 15
                height: 15
                path: Icons.bookmark
                color: Theme.text
            }

            Label {
                text: "Reading queue"
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Label {
                text: help.count === 0 ? "empty"
                    : help.count === 1 ? "1 book" : help.count + " books"
                color: Theme.muted
                font.pixelSize: 11
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 11
            text: "Drag a cover in the queue to change its order in the reading "
                + "queue. The number in its corner is its place, and it is saved as "
                + "you drop it."
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 11
            text: "Add a book from anywhere with the bookmark on its card and it "
                + "joins the end. The same bookmark takes it out again, and the books "
                + "behind it close the gap."
        }
    }
}
