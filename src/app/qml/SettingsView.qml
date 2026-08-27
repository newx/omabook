import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import com.omabook.app

// Settings: the choices that outlive a session. AI and speech had sections
// here; with both gone, the only thing left to configure is where the
// library imports from.
Item {
    id: view

    property var library: null

    readonly property bool ready: view.library !== null && view.library !== undefined

    FolderDialog {
        id: folderDialog
        title: "Choose a folder of books to import"
        onAccepted: {
            // FolderDialog yields a file:// URL; the setter wants a path.
            view.library.setLibraryFolder(selectedFolder.toString().replace(/^file:\/\//, ""))
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: view.width
            spacing: 0

            // ---------------------------------------------------------------
            SettingsHeading { text: "LIBRARY" }

            SettingsRow {
                label: "Import folder"
                hint: "Books added here are found automatically on the next import."

                Label {
                    Layout.fillWidth: true
                    text: view.ready && view.library.library_folder !== ""
                        ? view.library.library_folder : "not set"
                    color: view.ready && view.library.library_folder !== "" ? Theme.text : Theme.muted
                    elide: Text.ElideRight
                    font.pixelSize: 12
                }

                FlatButton {
                    text: "Change…"
                    onClicked: folderDialog.open()
                }
            }

            SettingsRow {
                label: "Import now"
                hint: view.ready && view.library.library_folder === ""
                    ? "Choose a folder above first."
                    : "Rescans the folder and adds anything new."

                FlatButton {
                    text: "Import now"
                    enabled: view.ready && !view.library.busy && view.library.library_folder !== ""
                    onClicked: view.library.importDirectory()
                }

                Label {
                    Layout.fillWidth: true
                    visible: view.ready && view.library.busy
                    text: view.ready ? view.library.status_line : ""
                    color: Theme.muted
                    elide: Text.ElideRight
                    font.pixelSize: 12
                }
            }

            Item { Layout.preferredHeight: Theme.pad * 2 }
        }
    }
}
