import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import com.omabook.app

ApplicationWindow {
    id: window
    width: 1240
    height: 780
    visible: true
    title: readerLoader.active ? "OmaBooks: " + readerLoader.bookTitle : "OmaBooks"

    color: Theme.background

    LibraryModel { id: library }
    SidebarModel { id: sidebarData }
    ThemeModel { id: themeModel }
    NotesModel { id: notesModel }

    // Which view the content area shows. Some sidebar rows are destinations
    // rather than library filters, so they are tracked separately.
    property bool showingNotes: sidebarView.current === "notes"
    property bool showingSettings: sidebarView.current === "settings"
    /// The queue is the one list ordered by hand, so it alone can be dragged.
    property bool showingQueue: sidebarView.current === "queue" && library.search === ""
    /// True while a card is being dragged: the grid must stop flicking, or it
    /// takes the drag away from the card halfway through.
    property bool queueDragging: false
    /// The book a probe is dragging, so a synthetic drag goes through the same
    /// Drag and DropArea path a mouse does. Identified by book rather than by
    /// row, because the rows move underneath it.
    property int probeDragBookId: -1

    // The Theme singleton holds the palette; the Rust model holds the choice.
    Binding { target: Theme; property: "dark";   value: themeModel.dark }
    Binding { target: Theme; property: "accent"; value: themeModel.accent }

    function refresh() {
        library.reload()
        sidebarData.reload()
        notesModel.reload()
    }

    Connections {
        target: library
        // The import runs off the UI thread; refresh the sidebar when it lands.
        function onBusyChanged() { if (!library.busy) sidebarData.reload() }
    }

    Component.onCompleted: {
        refresh()

        // Follow the desktop from here on: omarchy-theme-set rewrites the
        // palette under a running app, and nothing re-read it until now.
        themeModel.followSystemTheme()

        // Open straight onto a sidebar destination — "notes", "settings",
        // "queue" — so any page can be reached without clicking to it.
        var showIdx = Qt.application.arguments.indexOf("--show")
        if (showIdx !== -1 && showIdx + 1 < Qt.application.arguments.length)
            sidebarView.pick(Qt.application.arguments[showIdx + 1])

        var openIdx = Qt.application.arguments.indexOf("--open")
        if (openIdx !== -1 && openIdx + 1 < Qt.application.arguments.length)
            readerLoader.open(parseInt(Qt.application.arguments[openIdx + 1]))

        // Async-import probe: start an import and count event-loop ticks while
        // it runs. A blocking import would tick zero times.
        if (Qt.application.arguments.indexOf("--probe-import") !== -1) {
            probeTicks.start()
            library.importDirectory(Qt.application.arguments[
                Qt.application.arguments.indexOf("--probe-import") + 1])
        }

        if (Qt.application.arguments.indexOf("--probe-queue") !== -1) queueProbe.start()

        // Open-note probe: take a stored annotation by id and open it exactly
        // as the Highlights view does, so the reported landing CFI can be
        // compared with the one that was asked for. This is the check that
        // would have caught the reader opening every highlight at the last
        // reading position.
        var noteIdx = Qt.application.arguments.indexOf("--probe-open-note")
        if (noteIdx !== -1 && noteIdx + 1 < Qt.application.arguments.length)
            window.probeOpenNote(parseInt(Qt.application.arguments[noteIdx + 1]))

        if (Qt.application.arguments.indexOf("--headless-check") !== -1) {
            console.log("SLICE count:", library.count,
                        "| notes:", notesModel.count,
                        "| theme:", themeModel.theme_name, themeModel.mode, themeModel.dark,
                        "| categories:", sidebarData.categories_json,
                        "| tags:", sidebarData.tags_json)
            Qt.callLater(Qt.quit)
        }
    }

    // Queue probe: switch to the reading queue and drag a card through the
    // same Drag and DropArea path a mouse takes, then report the order the
    // grid ends up in. What the database holds is checked from outside.
    function queueOrder() {
        var out = []
        for (var i = 0; i < library.count; i++) {
            var cell = grid.itemAtIndex(i)
            out.push(cell ? cell.card.queuePosition + ":" + cell.model.title.substring(0, 14)
                          : "?")
        }
        return out.join("  ")
    }

    Timer {
        id: queueProbe
        interval: 1200
        running: false
        onTriggered: {
            sidebarView.pick("queue")
            queueProbeDrag.start()
        }
    }

    Timer {
        id: queueProbeDrag
        interval: 600
        onTriggered: {
            console.log("PROBE-QUEUE panel:", window.showingQueue ? "shown" : "HIDDEN",
                        "| queued:", library.count)
            console.log("PROBE-QUEUE before:", window.queueOrder())

            var from = 0, to = 3
            var source = grid.itemAtIndex(from)
            var target = grid.itemAtIndex(to)
            if (!source || !target) {
                console.log("PROBE-QUEUE result: FAILED — fewer than", to + 1, "books queued")
                Qt.exit(1)
                return
            }

            // Exactly what the handler does: hold the card, and move it over
            // the cell it is being dropped onto.
            window.probeDragBookId = source.model.bookId
            source.card.x = target.x
            source.card.y = target.y
            queueProbeReport.start()
        }
    }

    Timer {
        id: queueProbeReport
        // Long enough for the view to relayout: the badges follow the move at
        // once, but the view's own index-to-item map settles a frame later.
        interval: 400
        onTriggered: {
            window.probeDragBookId = -1   // the drop
            console.log("PROBE-QUEUE after: ", window.queueOrder())
            Qt.exit(0)
        }
    }

    /// Opens one stored annotation the way NotesView does, and reports where
    /// the reader actually landed against where it was told to go.
    function probeOpenNote(noteId) {
        var note = notesModel.noteById(noteId)
        var target = note.cfi || ""
        var book = note.bookId || 0
        if (book <= 0 || target === "") {
            console.log("PROBE-NOTE result: FAILED — no note", noteId)
            Qt.exit(1)
            return
        }
        console.log("PROBE-NOTE asked:", target)
        readerLoader.open(book, target)
        probeNoteReport.start()
    }

    Timer {
        id: probeNoteReport
        // Long enough for the page to open the book and settle on the target;
        // a PDF section takes noticeably longer than a reflowable chapter.
        interval: 6000
        onTriggered: {
            var landed = readerLoader.item ? readerLoader.item.landedCfi() : ""
            console.log("PROBE-NOTE landed:", landed)
            // Landing on the passage is only half of it: the highlight has to
            // be painted there too, and a failed draw is swallowed by design
            // so a section that has not rendered yet can be retried later.
            // Count what foliate's overlayer actually drew.
            if (readerLoader.item) {
                readerLoader.item.countDrawnHighlights(function (drawn) {
                    console.log("PROBE-NOTE drawn:", drawn)
                    // Landing is the assertion; painting is reported but not
                    // required, because foliate cannot paint into a
                    // fixed-layout book at all and a PDF would fail forever.
                    console.log("PROBE-NOTE result:", landed !== "" ? "OPENED" : "NO LOCATION")
                    Qt.exit(landed !== "" ? 0 : 1)
                })
            } else {
                console.log("PROBE-NOTE result: NO READER")
                Qt.exit(1)
            }
        }
    }

    property int tickCount: 0
    Timer {
        id: probeTicks
        interval: 20
        repeat: true
        running: false
        onTriggered: window.tickCount++
    }
    Connections {
        target: library
        enabled: probeTicks.running
        function onBusyChanged() {
            if (library.busy) return
            probeTicks.stop()
            console.log("PROBE ticks-during-import:", window.tickCount,
                        "| status:", library.status_line,
                        "| books:", library.count)
            Qt.callLater(Qt.quit)
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose a folder of books to import"
        onAccepted: {
            // FolderDialog yields a file:// URL; the importer wants a path.
            library.importDirectory(selectedFolder.toString().replace(/^file:\/\//, ""))
        }
    }

    // Header: taller, flat, separated by a single hairline. No toolbar
    // gradient, no drop shadow.
    header: Rectangle {
        implicitHeight: Theme.headerHeight
        color: Theme.background

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.pad
            anchors.rightMargin: Theme.pad
            spacing: Theme.gap

            BrandMark {
                glyphSize: 22
                // Reading is still the app, so its mark stays; the book's
                // title takes the place of the name beside it.
                wordmark: !readerLoader.active
            }

            Label {
                visible: readerLoader.active
                text: readerLoader.bookTitle
                font.pixelSize: 19
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
                Layout.maximumWidth: 520
            }

            Item { Layout.fillWidth: true }

            Label {
                text: library.status_line
                color: Theme.muted
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.maximumWidth: 380
                visible: text !== ""
            }

            BusyIndicator {
                running: library.busy
                visible: library.busy
                implicitWidth: 20
                implicitHeight: 20
            }

            // Leaving the book sits with the window's other controls rather
            // than beside its title. Named rather than an ×: in a window with
            // no titlebar of its own, an × at the top right is the one thing
            // that closes the app — and the assistant panel's × already means
            // "close this panel", which is a different thing again.
            FlatButton {
                text: "← Library"
                tooltip: "Close the book and go back to the library"
                visible: readerLoader.active
                onClicked: readerLoader.close()
            }

            FlatButton {
                // system → dark → light
                text: themeModel.mode === "system" ? "◐" : (themeModel.dark ? "☾" : "☀")
                tooltip: "Theme: " + themeModel.mode
                onClicked: themeModel.cycleMode()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.border
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        visible: !readerLoader.active

        Sidebar {
            id: sidebarView
            Layout.preferredWidth: Theme.sidebarWidth
            Layout.fillHeight: true
            model: sidebarData
            busy: library.busy
            noteCount: notesModel.count
            onFilterPicked: (filter) => {
                if (filter === "notes") {
                    notesModel.book_id = 0
                    notesModel.reload()
                } else if (filter === "settings") {
                    // Nothing to prime; the page reads library state directly.
                } else {
                    library.setFilterAndReload(filter)
                }
            }
            onSearched: (query) => {
                if (sidebarView.current === "notes") sidebarView.current = "all"
                library.setSearchAndReload(query)
            }
            onImportRequested: folderDialog.open()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: !window.showingNotes && !window.showingSettings

                QueueHelp {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.pad + 4
                    Layout.rightMargin: Theme.pad + 4
                    Layout.topMargin: Theme.pad + 4
                    visible: window.showingQueue
                    count: library.count
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    GridView {
                        id: grid
                        anchors.fill: parent
                        anchors.margins: Theme.pad + 4
                        visible: library.count > 0
                        cellWidth: 196 + Theme.gridGap
                        cellHeight: Math.round(196 * Theme.coverRatio) + 106 + Theme.gridGap
                        clip: true
                        model: library
                        ScrollBar.vertical: ScrollBar {}
                        // A card being dragged and the view being flicked are
                        // the same gesture; the view yields for the duration.
                        interactive: !window.queueDragging

                        // Reordering moves rows rather than resetting the
                        // model, so the books left behind can slide into their
                        // new places instead of blinking into them.
                        move: Transition {
                            NumberAnimation { properties: "x,y"; duration: 160; easing.type: Easing.OutQuad }
                        }
                        moveDisplaced: Transition {
                            NumberAnimation { properties: "x,y"; duration: 160; easing.type: Easing.OutQuad }
                        }

                        // The cell holds the place in the grid; the card is
                        // what leaves it. Keeping them separate is what lets a
                        // dragged card float above a grid that reflows beneath.
                        delegate: Item {
                            id: cell
                            required property var model
                            required property int index
                            readonly property Item card: cardItem

                            width: grid.cellWidth - Theme.gridGap
                            height: grid.cellHeight - Theme.gridGap

                            DropArea {
                                anchors.fill: parent
                                enabled: window.showingQueue
                                // Reorder as the card passes over, not on
                                // release: the grid then shows the order you
                                // are about to get while you are still holding
                                // the book.
                                onEntered: (event) => {
                                    var from = event.source.cellIndex
                                    if (from !== undefined && from !== cell.index)
                                        library.moveQueued(from, cell.index)
                                }
                            }

                            BookCard {
                                id: cardItem
                                property int cellIndex: cell.index

                                width: cell.width
                                height: cell.height
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.verticalCenter: parent.verticalCenter

                                bookTitle: cell.model.title
                                bookAuthor: cell.model.author
                                readProgress: cell.model.progress
                                bookFormat: cell.model.format
                                cover: cell.model.coverUrl
                                favorite: cell.model.isFavorite
                                queued: cell.model.isQueued
                                queuePosition: window.showingQueue ? cell.index + 1 : 0
                                textQuality: cell.model.textQuality

                                readonly property bool held:
                                    dragHandler.active || window.probeDragBookId === cell.model.bookId

                                z: held ? 2 : 0
                                opacity: held ? 0.9 : 1
                                scale: held ? 1.03 : 1
                                Behavior on scale { NumberAnimation { duration: 90 } }

                                Drag.active: cardItem.held
                                Drag.source: cardItem
                                Drag.hotSpot.x: width / 2
                                Drag.hotSpot.y: height / 2

                                DragHandler {
                                    id: dragHandler
                                    enabled: window.showingQueue
                                    target: cardItem
                                    cursorShape: Qt.ClosedHandCursor
                                    onActiveChanged: window.queueDragging = active
                                }

                                // While dragged, the card belongs to the view
                                // rather than to its cell, so the cell can be
                                // moved out from under it without dragging the
                                // card along with it.
                                states: State {
                                    when: cardItem.held
                                    ParentChange { target: cardItem; parent: grid.contentItem }
                                    AnchorChanges {
                                        target: cardItem
                                        anchors.horizontalCenter: undefined
                                        anchors.verticalCenter: undefined
                                    }
                                }

                                onOpened: readerLoader.open(cell.model.bookId)
                                onFavoriteToggled: {
                                    library.toggleFavorite(cell.model.bookId)
                                    sidebarData.reload()
                                }
                                onQueueToggled: {
                                    library.toggleQueued(cell.model.bookId)
                                    sidebarData.reload()
                                }
                                onDeleteRequested: {
                                    // Removing cannot be undone, and the button
                                    // sits beside two that can.
                                    confirmDelete.pendingId = cell.model.bookId
                                    confirmDelete.ask(
                                        "Remove \u201c" + cell.model.title + "\u201d?",
                                        "It leaves your library along with its reading "
                                        + "position, highlights and notes. The file on "
                                        + "disk is not deleted, so importing that folder "
                                        + "again brings the book back.")
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 40, 380)
                        spacing: Theme.gap
                        visible: library.count === 0 && !library.busy

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Theme.muted
                            font.pixelSize: 13
                            text: library.search !== ""
                                ? "Nothing matches \u201c" + library.search + "\u201d."
                                : library.filter === "all"
                                  ? "No books yet.\nUse Import books in the sidebar to add a folder."
                                  : "Nothing here yet."
                        }
                    }
                }
            }

            NotesView {
                anchors.fill: parent
                visible: window.showingNotes
                model: notesModel
                onOpenBook: (bookId, cfi) => readerLoader.open(bookId, cfi)
            }

            // Built when first opened, and torn down when left, like the
            // reader loader below.
            Loader {
                anchors.fill: parent
                active: window.showingSettings
                visible: active
                source: "SettingsView.qml"
                onLoaded: item.library = library
            }
        }
    }

    ConfirmDialog {
        id: confirmDelete
        property int pendingId: -1
        confirmText: "Remove"
        onConfirmed: {
            library.deleteBook(confirmDelete.pendingId)
            sidebarData.reload()
            notesModel.reload()
        }
    }

    Loader {
        id: readerLoader
        anchors.fill: parent
        active: false
        visible: active

        property int bookId: -1
        property string bookTitle: ""

        property string startCfi: ""

        // `cfi` is optional: empty opens where the reader left off, and a
        // highlight or note passes its own anchor so the book opens on the
        // passage that was clicked.
        function open(id, cfi) {
            bookId = id
            startCfi = cfi || ""
            bookTitle = library.titleFor(id)
            source = "Reader.qml"
            active = true
        }

        function close() {
            active = false
            source = ""
            window.refresh()
        }

        onLoaded: {
            item.library = library
            item.notes = notesModel
            item.bookId = readerLoader.bookId
            item.startCfi = readerLoader.startCfi
            item.closeRequested.connect(readerLoader.close)
            item.load()
        }
    }
}
