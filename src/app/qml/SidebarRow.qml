import QtQuick
import QtQuick.Controls
import com.omabook.app

// One clickable line in the sidebar.
Item {
    id: row

    property string label: ""
    /// An `Icons` path. Rows without one (categories, tags) keep their text
    /// hanging at the same indent as the rest of their group.
    property string iconPath: ""
    property int count: 0
    property string note: ""
    property bool selected: false
    property bool dimmed: false
    property int indent: Theme.pad

    signal clicked()

    height: 30

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        radius: 4
        color: row.selected ? Theme.selected
                            : hover.hovered ? Theme.surfaceHover : "transparent"

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: row.clicked() }

        // A left marker on the selected row, instead of a filled highlight.
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height - 10
            radius: 1
            visible: row.selected
            color: Theme.accent
        }

        VectorIcon {
            id: leading
            anchors.left: parent.left
            anchors.leftMargin: row.indent
            anchors.verticalCenter: parent.verticalCenter
            visible: row.iconPath !== ""
            width: 15
            height: 15
            path: row.iconPath
            // The label's colour, so the row reads as one thing rather than
            // an icon with some text after it.
            color: row.dimmed ? Theme.muted : Theme.text
            opacity: row.selected ? 1 : 0.75
        }

        Label {
            id: name
            anchors.left: row.iconPath !== "" ? leading.right : parent.left
            anchors.leftMargin: row.iconPath !== "" ? 9 : row.indent
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: trailing.left
            anchors.rightMargin: 6
            text: row.label
            elide: Text.ElideRight
            color: row.dimmed ? Theme.muted : Theme.text
            opacity: row.selected ? 1 : 0.88
            font.pixelSize: 13
        }

        Label {
            id: trailing
            anchors.right: parent.right
            anchors.rightMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            text: row.note !== "" ? row.note : (row.count > 0 ? row.count : "")
            color: Theme.muted
            font.pixelSize: row.note !== "" ? 9 : 11
        }
    }
}
