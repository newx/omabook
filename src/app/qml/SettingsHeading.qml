import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// A group title on the settings page, in the sidebar's idiom.
Item {
    id: heading

    property string text: ""

    Layout.fillWidth: true
    Layout.preferredHeight: 46

    Label {
        anchors.left: parent.left
        anchors.leftMargin: Theme.pad + 4
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        text: heading.text
        color: Theme.muted
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 1.4
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.border
    }
}
