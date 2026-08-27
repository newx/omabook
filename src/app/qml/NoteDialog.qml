import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// Writing a note about a selected passage. Native rather than in the page, so
// it matches the rest of the app.
Popup {
    id: dialog

    property string quote: ""
    property string cfi: ""
    property real fraction: 0

    signal accepted(string body)

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 80 : 520, 560)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.background
        border.width: 1
        border.color: Theme.border
        radius: Theme.radius
    }

    onOpened: body.forceActiveFocus()

    function show(cfi, quote, fraction) {
        dialog.cfi = cfi
        dialog.quote = quote
        dialog.fraction = fraction
        body.text = ""
        open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pad
        spacing: Theme.gap

        Label {
            text: "Add a note"
            font.pixelSize: 15
            font.bold: true
            color: Theme.text
        }

        // The passage being annotated, so it is clear what this is about.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(quoteText.implicitHeight + 16, 120)
            color: Theme.surface
            border.width: 1
            border.color: Theme.cardBorder
            radius: 4

            ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true

                Label {
                    id: quoteText
                    width: dialog.width - 2 * Theme.pad - 16
                    text: dialog.quote
                    wrapMode: Text.WordWrap
                    color: Theme.muted
                    font.pixelSize: 12
                    font.italic: true
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 130

            TextArea {
                id: body
                placeholderText: "What do you want to remember about this?"
                wrapMode: TextArea.Wrap
                color: Theme.text
                placeholderTextColor: Theme.muted
                background: Rectangle {
                    color: Theme.background
                    border.width: 1
                    border.color: body.activeFocus ? Theme.accent : Theme.border
                    radius: 4
                }
                Keys.onPressed: (event) => {
                    // Ctrl+Enter saves, since Enter inserts a newline here.
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier)) {
                        save()
                        event.accepted = true
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Label {
                text: "Ctrl+Enter to save"
                color: Theme.muted
                font.pixelSize: 11
                Layout.fillWidth: true
            }

            FlatButton {
                text: "Cancel"
                onClicked: dialog.close()
            }

            FlatButton {
                text: "Save note"
                enabled: body.text.trim() !== ""
                onClicked: save()
            }
        }
    }

    function save() {
        if (body.text.trim() === "") return
        dialog.accepted(body.text.trim())
        dialog.close()
    }
}
