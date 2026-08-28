import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebChannel
import QtWebEngine
import com.omabook.app

// The reader: QML chrome around a WebEngineView running foliate-js.
// Rendering, pagination, CFI, and visible-text extraction live in the page;
// everything else lives in Rust (SPEC §5.3).
Item {
    id: reader

    property var library: null
    property var notes: null
    property int bookId: -1
    property string filePath: ""
    /// Where to open. Empty means "wherever I left off"; set when a highlight
    /// or note is opened from the Highlights view, so the book lands on that
    /// passage instead of on the reading position.
    property string startCfi: ""

    /// Where the page actually settled, as opposed to where it was sent.
    function landedCfi() {
        return bridgeObject.last_cfi
    }

    /// How many highlight shapes are actually painted, counted on the overlays
    /// rather than in the document: foliate's own <svg> is attached by the
    /// renderer and lives in neither the section's document nor the top one.
    ///
    /// Counts two of them. foliate keeps one per rendered section, but only
    /// for a reflowable book; a fixed-layout one gets no overlayer at all, so
    /// reader.html paints its highlights into an overlay of its own.
    /// How many highlight shapes are actually painted, counted on the overlays
    /// rather than in the document: foliate's own <svg> is attached by the
    /// renderer and lives in neither the section's document nor the top one.
    ///
    /// Counts two of them. foliate keeps one per rendered section, but only
    /// for a reflowable book; a fixed-layout one gets no overlayer at all, so
    /// reader.html paints its highlights into an overlay of its own.
    function countDrawnHighlights(callback) {
        view.runJavaScript(`(() => {
            const contents = view.renderer?.getContents?.() || []
            let shapes = 0
            for (const c of contents) {
                const el = c?.overlayer?.element
                if (el) shapes += el.children.length
                const own = c?.doc?.getElementById?.('omabook-fixed-overlay')
                if (own) shapes += own.children.length
            }
            return shapes
        })()`, callback)
    }

    /// The one URL the view is allowed to show in its main frame. Remembered
    /// here rather than read back off the view, because the guard has to know
    /// where the page was *sent* even while a navigation is in flight.
    property string readerUrl: ""

    function load() {
        reader.readerUrl = library ? library.readerUrlFor(reader.bookId, reader.startCfi) : ""
        view.url = reader.readerUrl
    }

    // Paint saved highlights once the page says it is ready to be called into.
    function paintAnnotations() {
        if (!reader.notes) return
        var json = reader.notes.annotationsJson(reader.bookId)
        view.runJavaScript("window.omabookSetAnnotations(" + JSON.stringify(json) + ")")
    }

    // Push the light/dark choice into the page. The book is rendered by
    // foliate inside the view, so it does not inherit the QML palette the way
    // the rest of the chrome does, and without this the window retints around
    // a book that stays as it was.
    //
    // Gated on `connected` rather than on the view finishing its load: at
    // LoadSucceeded the page's module has not run yet, so the function below
    // does not exist and the call is silently dropped.
    function applyAppearance() {
        view.runJavaScript("window.omabookSetAppearance("
                           + JSON.stringify({ dark: Theme.dark }) + ")")
    }

    Connections {
        target: Theme
        function onDarkChanged() {
            if (bridgeObject.connected) reader.applyAppearance()
        }
    }

    /// Asks the window to put the library back. The reader does not own its
    /// own lifetime -- Main.qml loaded it and Main.qml unloads it.
    signal closeRequested()

    // Escape leaves the book, which is what every reader does and what the
    // "← Library" button does with the mouse.
    //
    // Disabled while a dialog is up, because Popup.CloseOnEscape is already
    // bound to that key: without the guard, one press would dismiss the note
    // you were writing *and* throw you out of the book.
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: !noteDialog.opened
        onActivated: reader.closeRequested()
    }

    NoteDialog {
        id: noteDialog
        onAccepted: (body) => {
            if (!reader.notes) return
            reader.notes.saveAnnotation(reader.bookId, noteDialog.cfi,
                                        noteDialog.quote, body, noteDialog.fraction)
            view.runJavaScript(
                "window.omabookAddAnnotation(" + JSON.stringify(noteDialog.cfi) + ", true)")
        }
    }

    // Verification mode: report what the reader actually managed to do, then
    // quit. Proves the whole chain without needing a screenshot.
    property bool verifying: Qt.application.arguments.indexOf("--verify-reader") !== -1

    Timer {
        id: verifyTimeout
        interval: 25000
        running: reader.verifying
        onTriggered: {
            console.log("VERIFY result: TIMEOUT — reader never became ready")
            Qt.exit(1)
        }
    }

    ReaderBridge {
        id: bridgeObject
        WebChannel.id: "reader"

        onHighlightRequested: (cfi, quote, fraction) => {
            if (!reader.notes) return
            // An empty body means "a highlight"; the repository will not erase
            // a note that already exists at this anchor.
            reader.notes.saveAnnotation(reader.bookId, cfi, quote, "", fraction)
            view.runJavaScript(
                "window.omabookAddAnnotation(" + JSON.stringify(cfi) + ", false)")
        }

        onNoteRequested: (cfi, quote, fraction) => noteDialog.show(cfi, quote, fraction)

        onRelocated: {
            // Persist position as the reader moves. Debounced so a fast page
            // flick does not mean a write per frame.
            saveTimer.restart()
        }

        onConnectedChanged: {
            if (connected) reader.paintAnnotations()
            if (connected) reader.applyAppearance()
            if (reader.probingHighlight && connected) highlightProbe.start()
            if (!reader.verifying || !connected) return
            verifyTimeout.stop()
            // Give the first relocate a moment to land.
            verifyReport.restart()
        }

        onErrorChanged: {
            if (reader.verifying && error !== "") {
                console.log("VERIFY result: FAILED —", error)
                Qt.exit(1)
            }
        }
    }

    // Highlight probe: select a real passage in the page, drive the toolbar's
    // Highlight action, and confirm the annotation is stored and painted.
    property bool probingHighlight: Qt.application.arguments.indexOf("--probe-highlight") !== -1

    Timer {
        id: highlightProbe
        interval: 1500
        running: false
        onTriggered: {
            view.runJavaScript(`(() => {
                const doc = view.renderer?.getContents?.()[0]?.doc
                    || document.querySelector('foliate-view').renderer.getContents()[0].doc
                const p = [...doc.querySelectorAll('p')].find(el => el.textContent.trim().length > 40)
                if (!p) return 'no paragraph found'
                const range = doc.createRange()
                range.selectNodeContents(p)
                const sel = doc.getSelection()
                sel.removeAllRanges(); sel.addRange(range)
                doc.dispatchEvent(new Event('pointerup'))
                return 'selected: ' + p.textContent.trim().slice(0, 50)
            })()`, function(r) {
                console.log("PROBE-HL", r)
                // The page shows the toolbar on a short timer after pointerup,
                // so give it time rather than racing it.
                selectionSettle.start()
            })
        }
    }

    Timer {
        id: selectionSettle
        interval: 500
        onTriggered: {
            (function() {
                    view.runJavaScript(`(() => {
                        const tb = document.getElementById('sel-toolbar')
                        return JSON.stringify({
                            toolbarShown: tb.classList.contains('show'),
                            pending: window.__omabookDebugPending || null,
                            bridge: !!window.__omabookDebugBridge
                        })
                    })()`, function(state) {
                        console.log("PROBE-HL state:", state)
                        view.runJavaScript("document.getElementById('btn-highlight').click()",
                            function() { highlightCheck.start() })
                    })
            })()
        }
    }

    Timer {
        id: highlightCheck
        interval: 1200
        onTriggered: {
            console.log("PROBE-HL stored notes:", reader.notes ? reader.notes.count : -1)
            console.log("PROBE-HL annotations json:",
                        reader.notes ? reader.notes.annotationsJson(reader.bookId) : "")
            Qt.exit(0)
        }
    }

    Timer {
        id: verifyReport
        interval: 2500
        onTriggered: {
            var text = bridgeObject.pageText()
            console.log("VERIFY result: OPENED")
            console.log("VERIFY cfi:", bridgeObject.last_cfi)
            console.log("VERIFY fraction:", bridgeObject.last_fraction.toFixed(4))
            console.log("VERIFY chapter:", bridgeObject.chapter)
            console.log("VERIFY textChars:", text.length)
            console.log("VERIFY textHead:", text.substring(0, 110).replace(/\s+/g, " "))
            Qt.exit(0)
        }
    }

    Timer {
        id: saveTimer
        interval: 700
        onTriggered: {
            if (reader.library && bridgeObject.last_cfi !== "")
                reader.library.saveProgress(reader.bookId,
                                            bridgeObject.last_cfi,
                                            bridgeObject.last_fraction)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        WebEngineView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            backgroundColor: Theme.background

            webChannel: WebChannel { registeredObjects: [bridgeObject] }

            // The reader page is local and must fetch the book file, which is
            // also local. Both flags are required for that, and the page only
            // ever loads content this app points it at.
            settings.localContentCanAccessFileUrls: true
            settings.localContentCanAccessRemoteUrls: false
            settings.javascriptCanAccessClipboard: false
            settings.showScrollBars: false

            // The arrow keys are handled inside the page, so the view has to
            // hold focus for them to arrive. Without this the first press
            // after opening a book goes nowhere and you have to click the
            // text first, which reads as the keys being broken.
            focus: true
            onLoadingChanged: function(request) {
                if (request.status === WebEngineView.LoadSucceededStatus)
                    view.forceActiveFocus()
            }

            onJavaScriptConsoleMessage: function(level, message, line, source) {
                console.log("reader.js:" + line + " " + message)
            }

            // Navigation is an exfiltration path, and a book can drive it.
            // foliate renders each chapter into an iframe from a blob: URL, so
            // whatever script survives in a book runs inside this view; a
            // chapter that sets `top.location` sends the app -- and anything
            // it managed to read -- to an address of its choosing.
            // `localContentCanAccessRemoteUrls: false` does not cover that: it
            // stops fetch and XHR, not a navigation.
            //
            // So the view goes where this QML sent it and nowhere else. Local
            // schemes for the subframes and resources foliate creates, and for
            // the main frame the reader URL `load()` assigned, compared
            // without its fragment because foliate moves through the book by
            // pushing one.
            readonly property var allowedSchemes: ["file", "blob", "qrc", "about", "data"]

            function withoutFragment(url) {
                var hash = url.indexOf("#")
                return hash === -1 ? url : url.substring(0, hash)
            }

            onNavigationRequested: function(request) {
                var target = request.url.toString()
                var match = target.match(/^([A-Za-z][A-Za-z0-9+.-]*):/)
                var scheme = match ? match[1].toLowerCase() : ""

                var allowed = view.allowedSchemes.indexOf(scheme) !== -1
                if (allowed && request.isMainFrame)
                    allowed = view.withoutFragment(target)
                              === view.withoutFragment(reader.readerUrl)

                if (allowed)
                    return

                // Named, because a silently dropped navigation looks exactly
                // like a book that simply does nothing when you click a link.
                request.action = WebEngineNavigationRequest.IgnoreRequest
                console.warn("reader: blocked navigation to " + target
                             + (request.isMainFrame ? " (main frame)" : " (subframe)"))
            }

            // foliate hands an external link to window.open. There is no
            // browser inside this app to give it to, and a book must not be
            // able to raise a view the app does not control, so the request is
            // logged and dropped rather than left to WebEngine's default.
            onNewWindowRequested: function(request) {
                console.warn("reader: blocked a new window for "
                             + (request.requestedUrl ? request.requestedUrl.toString()
                                                     : "an unnamed URL"))
            }
        }

        // Progress bar and position, driven by the bridge.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                spacing: Theme.gap

                Button {
                    text: "‹"
                    flat: true
                    implicitWidth: 36
                    onClicked: view.runJavaScript("window.omabookPrev()")
                }

                Button {
                    text: "›"
                    flat: true
                    implicitWidth: 36
                    onClicked: view.runJavaScript("window.omabookNext()")
                }

                Label {
                    text: bridgeObject.chapter
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    font.pixelSize: 12
                }

                ProgressBar {
                    id: progressBar
                    Layout.preferredWidth: 220
                    from: 0; to: 1
                    value: bridgeObject.last_fraction

                    // A quarter of the stock 6px slab, rounded into a rail:
                    // position in the book is a background fact, and the bar
                    // should not carry more weight than the chapter beside it.
                    readonly property real railHeight: 1.5

                    background: Rectangle {
                        implicitWidth: 200
                        implicitHeight: progressBar.railHeight
                        y: (progressBar.height - height) / 2
                        height: progressBar.railHeight
                        radius: height / 2
                        color: Theme.border
                    }

                    contentItem: Item {
                        implicitHeight: progressBar.railHeight

                        Rectangle {
                            width: progressBar.visualPosition * parent.width
                            height: progressBar.railHeight
                            y: (parent.height - height) / 2
                            radius: height / 2
                            color: Theme.accent
                        }
                    }
                }

                Label {
                    text: Math.round(bridgeObject.last_fraction * 100) + "%"
                    color: Theme.muted
                    font.pixelSize: 12
                    Layout.preferredWidth: 38
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }

    // Failure has to be visible; a blank page reads as a hang.
    Rectangle {
        anchors.fill: parent
        visible: bridgeObject.error !== ""
        color: Theme.background

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 60, 420)
            spacing: Theme.gap

            Label {
                Layout.fillWidth: true
                text: "Could not open this book"
                font.pixelSize: 16
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text
            }
            Label {
                Layout.fillWidth: true
                text: bridgeObject.error
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.muted
                font.pixelSize: 12
            }
        }
    }
}
