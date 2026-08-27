import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// MAIN, then collapsible CATEGORIES and TAGS.
Rectangle {
    id: sidebar

    property var model: null          // the SidebarModel QObject from Rust
    property string current: "all"
    property bool busy: false
    property int noteCount: 0

    signal filterPicked(string filter)
    signal importRequested()
    signal searched(string query)

    color: Theme.surface

    function pick(filter) {
        sidebar.current = filter
        sidebar.filterPicked(filter)
    }

    // Categories and tags arrive as JSON (see sidebar.rs for why). Parsed
    // defensively: a malformed payload must not take the sidebar down.
    function parseList(json) {
        try {
            var list = JSON.parse(json || "[]")
            return Array.isArray(list) ? list : []
        } catch (e) {
            console.warn("sidebar: could not parse taxonomy json:", e)
            return []
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: sidebar.width
            spacing: 0

            // Theme.pad, not Theme.gap: the field is inset by Theme.pad on
            // both sides, and a narrower gap above it made the sidebar's top
            // corner read as tighter than its edges.
            Item { Layout.preferredHeight: Theme.pad }

            SearchBox {
                Layout.fillWidth: true
                placeholder: "Search books"
                onSubmitted: (query) => sidebar.searched(query)
            }

            SidebarSection {
                Layout.fillWidth: true
                title: "MAIN"
                collapsible: false
                entries: [
                    { id: "all",       name: "All",           icon: Icons.grid,     count: sidebar.model ? sidebar.model.count_all : 0 },
                    { id: "favorites", name: "Favorites",     icon: Icons.heart,    count: sidebar.model ? sidebar.model.count_favorites : 0 },
                    { id: "reading",   name: "Reading",       icon: Icons.bookOpen, count: sidebar.model ? sidebar.model.count_reading : 0 },
                    { id: "queue",     name: "Reading queue", icon: Icons.bookmark, count: sidebar.model ? sidebar.model.count_queue : 0 },
                    { id: "completed", name: "Completed",     icon: Icons.check,    count: sidebar.model ? sidebar.model.count_completed : 0 },
                    { id: "notes",     name: "Highlights & notes", icon: Icons.pencil, count: sidebar.noteCount },
                    { id: "settings",  name: "Settings",      icon: Icons.sliders }
                ]
                current: sidebar.current
                onPicked: (filter) => sidebar.pick(filter)

                // Import belongs with the library's own actions, not in the
                // window chrome.
                extraRow: importRow
            }

            Component {
                id: importRow
                SidebarRow {
                    label: sidebar.busy ? "Importing…" : "Import books…"
                    iconPath: Icons.plus
                    dimmed: sidebar.busy
                    onClicked: if (!sidebar.busy) sidebar.importRequested()
                }
            }

            SidebarSection {
                Layout.fillWidth: true
                title: "CATEGORIES"
                entries: sidebar.parseList(sidebar.model ? sidebar.model.categories_json : "[]")
                prefix: "category:"
                current: sidebar.current
                emptyNote: "none yet, books get one from their folder"
                onPicked: (filter) => sidebar.pick(filter)
            }

            SidebarSection {
                Layout.fillWidth: true
                title: "TAGS"
                entries: sidebar.parseList(sidebar.model ? sidebar.model.tags_json : "[]")
                prefix: "tag:"
                current: sidebar.current
                emptyNote: "none yet, from EPUB subjects"
                onPicked: (filter) => sidebar.pick(filter)
            }

            Item { Layout.preferredHeight: Theme.pad }
        }
    }

    // The only separation between sidebar and content: one hairline.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }
}
