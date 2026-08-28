import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// A sidebar group: MAIN, CATEGORIES, TAGS. MAIN is not collapsible; the
// taxonomy groups are, because they can grow without bound.
ColumnLayout {
    id: section

    property string title: ""
    property var entries: []
    property string prefix: ""
    property string current: ""
    property string emptyNote: ""
    property bool collapsible: true
    property bool expanded: true
    /// Optional component appended after the entries (used for Import).
    property Component extraRow: null
    /// Keyboard focus, as a flat index into Sidebar's row list. `baseIndex` is
    /// this section's offset into that list, so a row's global index is
    /// `baseIndex + index` — see Sidebar.qml for how the list is assembled.
    property int focusedIndex: -1
    property int baseIndex: 0

    signal picked(string filter)

    spacing: 0

    Item { Layout.preferredHeight: Theme.pad }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 22

        HoverHandler { id: hover; enabled: section.collapsible }
        TapHandler {
            enabled: section.collapsible
            onTapped: section.expanded = !section.expanded
        }

        Label {
            id: chevron
            anchors.left: parent.left
            anchors.leftMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            visible: section.collapsible
            text: "▸"
            color: Theme.muted
            font.pixelSize: 9
            rotation: section.expanded ? 90 : 0
            Behavior on rotation { NumberAnimation { duration: 110 } }
        }

        Label {
            anchors.left: section.collapsible ? chevron.right : parent.left
            anchors.leftMargin: section.collapsible ? 6 : Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            text: section.title
            color: hover.hovered ? Theme.text : Theme.muted
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 1.4
        }

        Label {
            anchors.right: parent.right
            anchors.rightMargin: Theme.pad
            anchors.verticalCenter: parent.verticalCenter
            visible: section.collapsible && section.entries.length > 0
            text: section.entries.length
            color: Theme.muted
            font.pixelSize: 10
        }
    }

    Item { Layout.preferredHeight: 2 }

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.pad + 15
        Layout.rightMargin: Theme.pad
        visible: section.expanded && section.entries.length === 0 && section.emptyNote !== ""
        text: section.emptyNote
        color: Theme.muted
        font.pixelSize: 10
        font.italic: true
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: section.expanded ? section.entries : []

        delegate: SidebarRow {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            indent: section.collapsible ? Theme.pad + 15 : Theme.pad
            label: modelData.name
            iconPath: modelData.icon || ""
            count: modelData.count || 0
            note: modelData.note || ""
            selected: section.current === section.prefix + modelData.id
            focused: section.focusedIndex === section.baseIndex + index
            onClicked: section.picked(section.prefix + modelData.id)
        }
    }

    Loader {
        Layout.fillWidth: true
        active: section.extraRow !== null && section.expanded
        sourceComponent: section.extraRow
    }
}
