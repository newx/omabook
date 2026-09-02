import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// MAIN, then collapsible CATEGORIES and TAGS.
FocusScope {
    id: sidebar

    property var model: null          // the SidebarModel QObject
    property string current: "all"
    property bool busy: false
    property int noteCount: 0

    signal filterPicked(string filter)
    signal importRequested()
    signal searched(string query)
    /// Escape while a row has keyboard focus, as opposed to the search field,
    /// which already handles its own Escape. Main.qml owns `library`, so it is
    /// the one that clears the active search.
    signal escaped()
    /// Right arrow while a row has keyboard focus: Main.qml owns the grid, so
    /// it is the one that moves focus there — same shape as `escaped()`.
    signal rightPressed()

    /// The search field, exposed so Main.qml's global "s" / Ctrl+F bindings
    /// can gate on it and drive it without reaching past this component.
    property alias searchField: searchBox

    activeFocusOnTab: true

    // Every row in the sidebar, MAIN then Import then CATEGORIES then TAGS, as
    // one flat list. Three Repeaters build the visible rows, but keyboard
    // navigation needs a single index across all of them, so it is computed
    // here once rather than re-derived in each section.
    readonly property var mainEntries: [
        { id: "all",       name: "All",           icon: Icons.grid,     count: sidebar.model ? sidebar.model.count_all : 0 },
        { id: "favorites", name: "Favorites",     icon: Icons.heart,    count: sidebar.model ? sidebar.model.count_favorites : 0 },
        { id: "reading",   name: "Reading",       icon: Icons.bookOpen, count: sidebar.model ? sidebar.model.count_reading : 0 },
        { id: "queue",     name: "Reading queue", icon: Icons.bookmark, count: sidebar.model ? sidebar.model.count_queue : 0 },
        { id: "completed", name: "Completed",     icon: Icons.check,    count: sidebar.model ? sidebar.model.count_completed : 0 },
        { id: "notes",     name: "Highlights & notes", icon: Icons.pencil, count: sidebar.noteCount },
        { id: "settings",  name: "Settings",      icon: Icons.sliders }
    ]
    readonly property var categoryEntries: sidebar.parseList(sidebar.model ? sidebar.model.categories_json : "[]")
    readonly property var tagEntries: sidebar.parseList(sidebar.model ? sidebar.model.tags_json : "[]")

    readonly property int importIndex: mainEntries.length
    readonly property int categoriesBaseIndex: importIndex + 1
    readonly property int tagsBaseIndex: categoriesBaseIndex + categoryEntries.length
    readonly property int rowCount: tagsBaseIndex + tagEntries.length

    /// Which row keyboard navigation is on, as a flat index; -1 while nothing
    /// is focused. Cleared whenever focus leaves the sidebar entirely, so a
    /// stale ring cannot linger once the grid or search field takes over.
    property int focusedIndex: -1
    onActiveFocusChanged: if (!activeFocus) focusedIndex = -1

    function pick(filter) {
        sidebar.current = filter
        sidebar.filterPicked(filter)
    }

    // Categories and tags arrive as JSON (see sidebarmodel.h for why). Parsed
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

    /// The filter string a flat row index resolves to, or "" for the Import
    /// row (which is not a filter) and for an out-of-range index.
    function filterForIndex(idx) {
        if (idx < 0 || idx >= rowCount) return ""
        if (idx < mainEntries.length) return mainEntries[idx].id
        if (idx === importIndex) return ""
        if (idx < tagsBaseIndex) return "category:" + categoryEntries[idx - categoriesBaseIndex].id
        return "tag:" + tagEntries[idx - tagsBaseIndex].id
    }

    /// Driven by Main.qml's "s": jump here and land on the first row.
    function focusFirst() {
        // Otherwise a FocusScope hands focus straight back to the search
        // field if it was ever focused, instead of to the scope itself.
        searchBox.blur()
        sidebar.forceActiveFocus()
        focusedIndex = rowCount > 0 ? 0 : -1
    }

    /// Driven by the grid's Left arrow from its leftmost column: land back on
    /// whichever row is already selected, rather than always the first — so
    /// moving right and straight back left does not silently change which
    /// filter is highlighted.
    function focusSelected() {
        searchBox.blur()
        sidebar.forceActiveFocus()
        var idx = -1
        for (var i = 0; i < rowCount; i++) {
            if (filterForIndex(i) === sidebar.current) {
                idx = i
                break
            }
        }
        focusedIndex = idx >= 0 ? idx : (rowCount > 0 ? 0 : -1)
    }

    /// Return/Enter on the focused row — exactly what clicking it does.
    function activateFocused() {
        if (focusedIndex === importIndex) {
            if (!sidebar.busy) sidebar.importRequested()
            return
        }
        var filter = filterForIndex(focusedIndex)
        if (filter !== "") sidebar.pick(filter)
    }

    /// Up/Down: one step in the flat row list, clamped to its ends. Factored
    /// out of Keys.onPressed so --probe-keys can drive exactly this rather
    /// than duplicating the clamping logic.
    function moveFocus(delta) {
        if (rowCount === 0) return
        focusedIndex = focusedIndex < 0 ? 0 : Math.max(0, Math.min(rowCount - 1, focusedIndex + delta))
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Down) {
            moveFocus(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            moveFocus(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            sidebar.activateFocused()
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            sidebar.rightPressed()
            event.accepted = true
        }
    }
    Keys.onEscapePressed: {
        focusedIndex = -1
        sidebar.escaped()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
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
                id: searchBox
                Layout.fillWidth: true
                placeholder: "Search books"
                onSubmitted: (query) => sidebar.searched(query)
            }

            SidebarSection {
                Layout.fillWidth: true
                title: "MAIN"
                collapsible: false
                entries: sidebar.mainEntries
                current: sidebar.current
                focusedIndex: sidebar.focusedIndex
                baseIndex: 0
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
                    focused: sidebar.focusedIndex === sidebar.importIndex
                    onClicked: if (!sidebar.busy) sidebar.importRequested()
                }
            }

            SidebarSection {
                Layout.fillWidth: true
                title: "CATEGORIES"
                entries: sidebar.categoryEntries
                prefix: "category:"
                current: sidebar.current
                emptyNote: "none yet, books get one from their folder"
                focusedIndex: sidebar.focusedIndex
                baseIndex: sidebar.categoriesBaseIndex
                onPicked: (filter) => sidebar.pick(filter)
            }

            SidebarSection {
                Layout.fillWidth: true
                title: "TAGS"
                entries: sidebar.tagEntries
                prefix: "tag:"
                current: sidebar.current
                emptyNote: "none yet, from EPUB subjects"
                focusedIndex: sidebar.focusedIndex
                baseIndex: sidebar.tagsBaseIndex
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
