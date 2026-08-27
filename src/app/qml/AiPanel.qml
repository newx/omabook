import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// Summaries and questions about the open book, beside the reader.
Rectangle {
    id: panel

    property var ai: null
    property int bookId: -1
    property string pageText: ""
    property string bookTitle: ""
    property string chapter: ""
    property int ordinal: -1
    property string indexState: "none"

    signal closeRequested()

    color: Theme.surface

    readonly property bool ready: panel.ai !== null && panel.ai !== undefined

    function refreshIndexState() {
        indexState = panel.ready ? panel.ai.indexState(panel.bookId) : "none"
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    // The panel pads its blocks, not itself, so the rules between them reach
    // the panel's own edges. A rule that stops short of the sides reads as a
    // line drawn on the content rather than a division of the panel.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Summarizing: what the page in front of you says.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.pad
            spacing: Theme.gap

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Assistant"
                    font.pixelSize: 14
                    font.bold: true
                    color: Theme.text
                    Layout.fillWidth: true
                }
                IconButton {
                    glyph: "×"
                    tooltip: "Close"
                    onClicked: panel.closeRequested()
                }
            }

            // No provider means every action below would fail; say so once
            // here rather than after each attempt.
            Label {
                Layout.fillWidth: true
                visible: panel.ready && panel.ai.available_providers === "none"
                text: "No AI provider is reachable.\nStart Ollama, or set a key in Settings."
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                FlatButton {
                    text: "Summarize page"
                    enabled: panel.ready && !panel.ai.busy && panel.pageText.length > 40
                    onClicked: panel.ai.summarizePage(panel.pageText, panel.bookTitle, panel.chapter)
                }

                Item { Layout.fillWidth: true }

                BusyIndicator {
                    running: panel.ai && panel.ai.busy
                    visible: running
                    implicitWidth: 18
                    implicitHeight: 18
                }
            }
        }

        // Summarizing and asking are separate tasks that happen to share a
        // panel. Without a rule between them the scope picker read as though
        // it applied to the button above it.
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        // Asking needs the book indexed; the page scope does not.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.pad
            spacing: 6

            // The prefix sits inside the choice rather than beside it: a
            // label reading "Ask about" next to a box reading "this page" is
            // one sentence split across two controls, and the closed box on
            // its own said nothing about what it selected.
            ComboBox {
                id: scopeBox
                Layout.fillWidth: true
                implicitHeight: 28
                model: [
                    { label: "Ask about this page", value: "page" },
                    { label: "Ask about what I've read", value: "so_far" },
                    { label: "Ask about the whole book", value: "book" }
                ]
                textRole: "label"
                valueRole: "value"
                font.pixelSize: 11
            }

            TextField {
                id: question
                Layout.fillWidth: true
                placeholderText: "Ask a question…"
                font.pixelSize: 12
                enabled: panel.ready && !panel.ai.busy
                onAccepted: panel.askNow()
                background: Rectangle {
                    color: Theme.background
                    border.width: 1
                    border.color: question.activeFocus ? Theme.accent : Theme.border
                    radius: 4
                }
            }

            // Anything beyond the current page needs vectors.
            RowLayout {
                Layout.fillWidth: true
                visible: scopeBox.currentValue !== "page" && panel.indexState !== "ready"
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: panel.ai && panel.ai.indexing
                        ? "Indexing " + panel.ai.index_done + "/" + panel.ai.index_total
                        : "This book is not indexed yet."
                    color: Theme.muted
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }

                FlatButton {
                    text: panel.ai && panel.ai.indexing ? "Stop" : "Index"
                    tooltip: "Runs locally. About two minutes for a novel."
                    onClicked: {
                        if (panel.ai.indexing) panel.ai.cancelIndexing()
                        else panel.ai.indexBook(panel.bookId)
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        // What came back.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.pad
            spacing: Theme.gap

            Label {
                Layout.fillWidth: true
                visible: panel.ai && panel.ai.status !== ""
                text: panel.ai ? panel.ai.status : ""
                color: Theme.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    // The block pads itself now, and the scroll bar takes the
                    // twelve on the right.
                    width: panel.width - 2 * Theme.pad - 12
                    spacing: Theme.gap

                    AnswerBox {
                        Layout.fillWidth: true
                        visible: panel.ai && panel.ai.answer !== ""
                        text: panel.ai ? panel.ai.answer : ""
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: panel.ai && panel.ai.sources !== ""
                        text: "From the book:"
                        color: Theme.muted
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: panel.ai && panel.ai.sources !== ""
                        text: panel.ai ? panel.ai.sources : ""
                        wrapMode: Text.WordWrap
                        color: Theme.muted
                        font.pixelSize: 11
                        font.italic: true
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: panel.ai && panel.ai.provider !== "" && panel.ai.answer !== ""
                text: "answered by " + (panel.ai ? panel.ai.provider : "")
                color: Theme.muted
                font.pixelSize: 9
            }
        }
    }

    function askNow() {
        if (!panel.ready || question.text.trim() === "") return
        panel.ai.askBook(panel.bookId, question.text, scopeBox.currentValue,
                         panel.pageText, panel.ordinal)
    }

    Connections {
        target: panel.ai
        function onIndexingChanged() {
            if (panel.ready && !panel.ai.indexing) panel.refreshIndexState()
        }
    }
}
