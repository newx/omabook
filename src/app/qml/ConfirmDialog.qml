import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// A yes or no question, for the few actions that cannot be undone.
Popup {
    id: dialog

    property string heading: ""
    property string body: ""
    property string confirmText: "Remove"

    signal confirmed()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 80 : 420, 440)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.background
        border.width: 1
        border.color: Theme.border
        radius: Theme.radius
    }

    function ask(heading, body) {
        dialog.heading = heading
        dialog.body = body
        open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pad
        spacing: Theme.gap

        Label {
            Layout.fillWidth: true
            text: dialog.heading
            font.pixelSize: 14
            font.bold: true
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: dialog.body !== ""
            text: dialog.body
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Item { Layout.fillWidth: true }

            FlatButton {
                text: "Cancel"
                onClicked: dialog.close()
            }

            FlatButton {
                text: dialog.confirmText
                onClicked: {
                    dialog.confirmed()
                    dialog.close()
                }
            }
        }
    }
}
