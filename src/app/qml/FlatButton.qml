import QtQuick
import QtQuick.Controls

// A button with a border and no fill, gradient, or shadow.
Item {
    id: button

    property string text: ""
    property string tooltip: ""
    // `enabled` is inherited from Item; redeclaring it shadows the base
    // property and QML warns on every instance.

    signal clicked()

    implicitWidth: Math.max(34, label.implicitWidth + 22)
    implicitHeight: 30

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: hover.hovered && button.enabled ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: Theme.border
        opacity: button.enabled ? 1 : 0.45

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: if (button.enabled) button.clicked() }

        Label {
            id: label
            anchors.centerIn: parent
            text: button.text
            color: Theme.text
            font.pixelSize: 13
        }
    }

    ToolTip.visible: button.tooltip !== "" && hover.hovered
    ToolTip.text: button.tooltip
    ToolTip.delay: 400
}
