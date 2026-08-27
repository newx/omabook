import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebChannel
import QtWebEngine
import QtMultimedia
import QtTextToSpeech
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

    Component.onDestruction: tts.stop()

    // "good" | "poor" | "none" | "". Reading aloud is hidden when there is no
    // text to read, and flagged when the text is noisy (SPEC §7.2).
    property string textQuality: ""
    readonly property bool canReadAloud: textQuality !== "none"

    // Whether anything can actually speak. Kokoro brings its own audio, but the
    // "system" engine is Qt's, and on a desktop with neither speech-dispatcher
    // nor flite installed the plugin behind it fails to load: the engine sits
    // in Error with no voices, say() is silent, and the Ready that advances the
    // chunk loop never arrives. Offering the buttons then buys a session that
    // claims to be reading, plays nothing, and never ends.
    readonly property bool canSpeak: tts.engine !== "system" || systemVoice.usable

    // Why the read-aloud buttons are dead, in the terms of the thing the reader
    // would have to install. Both routes are real: Kokoro is the good one.
    readonly property string noVoiceHint:
        "No voice available. Start Kokoro (docker compose up -d kokoro), " +
        "or install speech-dispatcher for a system voice."

    function load() {
        textQuality = library ? library.textQualityFor(reader.bookId) : ""
        view.url = library ? library.readerUrlFor(reader.bookId) : ""
    }

    // The text of what is on screen. EPUBs supply it from the page's visible
    // range; PDFs have none to give, so it comes from pdftotext instead.
    function currentPageText() {
        var text = bridgeObject.pageText()
        if (text && text.length > 0) return text
        if (reader.library && bridgeObject.pdf_page > 0)
            return reader.library.pdfPageText(reader.bookId, bridgeObject.pdf_page)
        return ""
    }

    // Ask the page what is on screen *now*, then hand it to `callback`.
    // Reading aloud must always start at the top of the page in front of the
    // reader, so it takes the text at the moment of the click rather than the
    // copy cached at the last relocate, which can be a page behind.
    function fetchPageText(callback) {
        view.runJavaScript("window.omabookPageText()", function(text) {
            if (text && text.length > 0) callback(text)
            else callback(reader.currentPageText())
        })
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

    // Rust owns the reading loop; these two only play what it hands over.
    TtsController {
        id: tts

        onPlayAudio: (path) => {
            player.stop()
            player.source = "file://" + path
            player.play()
        }

        onSpeakSystem: (text) => {
            // Kokoro can hand the session back to the system engine mid-book
            // when synthesis fails, so this is reachable even though the
            // buttons are gated on canSpeak.
            if (!systemVoice.usable) {
                tts.chunkFailed("no system voice is installed")
                return
            }
            systemVoice.stop()
            systemVoice.say(text)
        }

        onPauseRequested: (paused) => {
            if (paused) { player.pause(); systemVoice.pause() }
            else { player.play(); systemVoice.resume() }
        }

        onStopPlayback: {
            player.stop()
            systemVoice.stop()
        }

        onNeedNextPage: {
            // Turn the page in the reader, then feed the new text back.
            view.runJavaScript("window.omabookAdvance()", function(text) {
                // A PDF returns nothing here; give pdftotext a moment to see
                // the new page number, then read that instead.
                if (text && text.length > 0) tts.continueWithPage(text)
                else pdfPageSettle.start()
            })
        }
    }

    // Audio probe: use the real MediaPlayer and report whether it reaches
    // PlayingState with a moving position. Proves the output path, not just
    // that files were synthesized.
    property bool probingAudio: Qt.application.arguments.indexOf("--probe-audio") !== -1

    // AI probe: ask the open book a question through the real controller and
    // report what comes back.
    property bool probingAi: Qt.application.arguments.indexOf("--probe-ai") !== -1

    // Summarize probe: drive the panel's own action, so the binding the user
    // clicks is the one under test.
    property bool probingSummary: Qt.application.arguments.indexOf("--probe-summary") !== -1

    Timer {
        id: summaryProbeStart
        interval: 1500
        running: false
        onTriggered: {
            reader.aiOpen = true
            Qt.callLater(function() {
                var panel = aiPanel.item
                console.log("PROBE-SUM panel:", panel ? "loaded" : "NULL",
                            "| panel.ai:", (panel && panel.ai) ? "bound" : "NULL",
                            "| ready:", panel ? panel.ready : "n/a",
                            "| pageChars:", panel ? panel.pageText.length : 0)
                if (panel && panel.ready)
                    panel.ai.summarizePage(panel.pageText, panel.bookTitle, panel.chapter)
                else
                    Qt.exit(1)
            })
        }
    }

    Connections {
        target: aiController
        enabled: reader.probingSummary
        function onBusyChanged() {
            if (aiController.busy) return
            console.log("PROBE-SUM status:", aiController.status || "(ok)")
            console.log("PROBE-SUM provider:", aiController.provider)
            console.log("PROBE-SUM summary:", aiController.answer.substring(0, 300).replace(/\s+/g, " "))
            summaryProbeLinger.start()
        }
    }

    Timer {
        id: summaryProbeLinger
        // As --probe-ask does: hold the panel open for a moment once the
        // summary lands, so it can be looked at as well as read.
        interval: 4000
        onTriggered: Qt.exit(0)
    }

    Timer {
        id: aiProbeStart
        interval: 1500
        running: false
        onTriggered: {
            console.log("PROBE-AI providers:", aiController.available_providers,
                        "| onMains:", aiController.on_mains,
                        "| indexState:", aiController.indexState(reader.bookId))
            var idx = Qt.application.arguments.indexOf("--probe-ai")
            var q = Qt.application.arguments[idx + 1] || "What does Ishmael do when he feels grim?"
            aiController.askBook(reader.bookId, q, "book", reader.currentPageText(), -1)
        }
    }

    Connections {
        target: aiController
        enabled: reader.probingAi
        function onBusyChanged() {
            if (aiController.busy) return
            console.log("PROBE-AI status:", aiController.status || "(ok)")
            console.log("PROBE-AI provider:", aiController.provider)
            console.log("PROBE-AI answer:", aiController.answer.substring(0, 240).replace(/\s+/g, " "))
            console.log("PROBE-AI sourceCount:", aiController.sources === "" ? 0 : aiController.sources.split("\n\n").length)
            Qt.exit(0)
        }
    }

    Timer {
        id: audioProbeStart
        interval: 1500
        running: false
        onTriggered: {
            console.log("PROBE-AUDIO engine:", tts.engine)
            reader.fetchPageText(function(text) {
                tts.startReading(text, false)
                audioProbeCheck.start()
            })
        }
    }

    Timer {
        id: audioProbeCheck
        interval: 6000
        onTriggered: {
            console.log("PROBE-AUDIO playbackState:", player.playbackState,
                        "(1=Playing)",
                        "| position:", player.position, "ms",
                        "| duration:", player.duration, "ms",
                        "| error:", player.errorString || "none",
                        "| source:", String(player.source).slice(-24))
            // Prove pause works on the real player too.
            tts.togglePause()
            Qt.callLater(function() {
                console.log("PROBE-AUDIO after pause:", player.playbackState, "(2=Paused)",
                            "| tts.paused:", tts.paused)
                // ...and that Stop silences it at once, rather than at the end
                // of the chunk already handed over.
                tts.stop()
                Qt.callLater(function() {
                    console.log("PROBE-AUDIO after stop:", player.playbackState,
                                "(0=Stopped)", "| tts.speaking:", tts.speaking)
                    Qt.exit(0)
                })
            })
        }
    }

    Timer {
        id: pdfPageSettle
        interval: 250
        onTriggered: tts.continueWithPage(reader.currentPageText())
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput {}
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia) tts.chunkFinished()
        }
        onErrorOccurred: (error, str) => tts.chunkFailed(str)
    }

    TextToSpeech {
        id: systemVoice

        // Read once: engines are installed, not started, so this cannot change
        // while the app runs. availableVoices() is a call rather than a
        // property, so a binding would not re-evaluate anyway.
        property bool usable: false
        Component.onCompleted: systemVoice.usable = availableVoices().length > 0

        onStateChanged: {
            // Ready after speaking means the utterance finished.
            if (state === TextToSpeech.Ready && tts.speaking) tts.chunkFinished()
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
            if (reader.probingTts && connected) ttsProbeStart.start()
            if (reader.probingAudio && connected) audioProbeStart.start()
            if (reader.probingAi && connected) aiProbeStart.start()
            if (reader.probingSummary && connected) summaryProbeStart.start()
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

    // TTS probe: start reading and report what the Rust loop actually emits,
    // which is verifiable even with no speech engine installed.
    property bool probingTts: Qt.application.arguments.indexOf("--probe-tts") !== -1
    property int audioEmissions: 0
    property int systemEmissions: 0

    Connections {
        target: tts
        enabled: reader.probingTts && !reader.probingAudio
        function onPlayAudio(path) {
            reader.audioEmissions++
            console.log("PROBE-TTS audio", reader.audioEmissions, "->", path)
            // Stand in for playback finishing, so the loop advances without
            // waiting out the real audio.
            Qt.callLater(function() { tts.chunkFinished() })
        }
        function onSpeakSystem(text) {
            reader.systemEmissions++
            console.log("PROBE-TTS chunk", reader.systemEmissions,
                        "chars=" + text.length,
                        "|", text.substring(0, 64).replace(/\s+/g, " "))
            // Stand in for the speech engine: report the chunk as finished so
            // the loop advances exactly as it would with real audio.
            Qt.callLater(function() { tts.chunkFinished() })
        }
        function onNeedNextPage() { console.log("PROBE-TTS page exhausted -> advancing") }
        function onFinished(reason) {
            console.log("PROBE-TTS finished:", reason,
                        "| audio:", reader.audioEmissions,
                        "| system:", reader.systemEmissions,
                        "| engine:", tts.engine)
            Qt.exit(0)
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
        id: ttsProbeStart
        interval: 1200
        running: false
        onTriggered: {
            reader.fetchPageText(function(text) {
                console.log("PROBE-TTS engine:", tts.engine,
                            "| pageChars:", text.length,
                            "| page:", bridgeObject.pdf_page,
                            "|", text.substring(0, 64).replace(/\s+/g, " "))
                tts.startReading(text, false)
            })
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

    AiController { id: aiController }
    property bool aiOpen: false

    RowLayout {
        anchors.fill: parent
        spacing: 0

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
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

                FlatButton {
                    visible: reader.canReadAloud
                    enabled: reader.canSpeak
                    text: tts.speaking && !tts.continuous ? "■ Stop" : "▶ Read page"
                    tooltip: !reader.canSpeak
                        ? reader.noVoiceHint
                        : reader.textQuality === "poor"
                        ? "Read this page aloud. This book's text extracted poorly, so it may sound wrong."
                        : "Read this page aloud, then stop"
                    onClicked: {
                        if (tts.speaking) tts.stop()
                        else reader.fetchPageText(function(text) {
                            tts.startReading(text, false)
                        })
                    }
                }

                FlatButton {
                    visible: tts.speaking
                    text: tts.paused ? "▶ Resume" : "❚❚ Pause"
                    tooltip: "Pause or resume reading"
                    onClicked: tts.togglePause()
                }

                FlatButton {
                    visible: tts.speaking
                    text: tts.speed.toFixed(2).replace(/0$/, "") + "×"
                    tooltip: "Reading speed"
                    onClicked: {
                        // Cycle through the speeds people actually use.
                        var steps = [1.0, 1.25, 1.5, 1.75, 0.75]
                        var next = steps[(steps.indexOf(tts.speed) + 1) % steps.length]
                        tts.changeSpeed(next === undefined ? 1.0 : next)
                    }
                }

                FlatButton {
                    visible: reader.canReadAloud
                    enabled: reader.canSpeak
                    text: tts.speaking && tts.continuous ? "■ Stop" : "▶▶ Auto read"
                    tooltip: reader.canSpeak ? "Read aloud and keep turning pages"
                                             : reader.noVoiceHint
                    onClicked: {
                        if (tts.speaking) tts.stop()
                        else reader.fetchPageText(function(text) {
                            tts.startReading(text, true)
                        })
                    }
                }

                Label {
                    // A visible warning beats a button that reads nonsense.
                    visible: reader.textQuality === "poor" && !tts.speaking
                    text: "⚠ text extracted poorly"
                    color: Theme.muted
                    font.pixelSize: 11
                }

                Label {
                    text: tts.status !== "" ? tts.status : bridgeObject.chapter
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

                FlatButton {
                    text: "Assistant"
                    // Dead when nothing can answer. The panel says the same
                    // thing inside, but a button that opens onto "no provider"
                    // reads as broken; a dimmed one reads as not set up.
                    enabled: aiController.available_providers !== "none"
                    tooltip: enabled ? "Summaries and questions about this book"
                                     : "No AI provider is reachable. Start Ollama, or set a key in Settings."
                    onClicked: {
                        reader.aiOpen = !reader.aiOpen
                        if (reader.aiOpen) aiPanel.item.refreshIndexState()
                    }
                }
            }
        }
    }

        Loader {
            id: aiPanel
            active: reader.aiOpen
            visible: active
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            sourceComponent: AiPanel {
                ai: aiController
                bookId: reader.bookId
                pageText: reader.currentPageText()
                bookTitle: reader.library ? reader.library.titleFor(reader.bookId) : ""
                chapter: bridgeObject.chapter
                ordinal: -1
                onCloseRequested: reader.aiOpen = false
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
