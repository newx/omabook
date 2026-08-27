import QtQuick
import QtQuick.Controls

// A borderless icon button, for card footers and toolbars. Takes either a
// vector path (preferred) or a text glyph.
Item {
    id: button

    property string path: ""
    property string glyph: ""
    property string tooltip: ""
    property bool active: false

    signal clicked()

    width: 28
    height: 28

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 4
        color: hover.hovered ? Theme.surfaceHover : "transparent"

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: button.clicked() }

        VectorIcon {
            anchors.centerIn: parent
            visible: button.path !== ""
            width: 15
            height: 15
            path: button.path
            filled: button.active
            color: button.active ? Theme.accent
                                 : hover.hovered ? Theme.text : Theme.muted
        }

        Label {
            anchors.centerIn: parent
            visible: button.path === "" && button.glyph !== ""
            text: button.glyph
            color: button.active ? Theme.accent : Theme.muted
            font.pixelSize: 14
        }
    }

    ToolTip.visible: button.tooltip !== "" && hover.hovered
    ToolTip.text: button.tooltip
    ToolTip.delay: 500
}
