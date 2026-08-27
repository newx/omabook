import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// Ask the library a question.
//
// This began as a strip above every page, which was wrong twice over: it sat
// between the reader and the grid on views that had nothing to do with it, and
// asking is a task of its own, not a permanent piece of chrome. It is now a
// sidebar destination.
//
// The answer ranks the grid below rather than replacing it, so the books stay
// the primary result.
Rectangle {
    id: view

    property var ai: null

    readonly property bool ready: view.ai !== null && view.ai !== undefined

    color: Theme.surface

    // Height follows the content, width is handed down by the layout above.
    // The column is positioned rather than anchored to fill, because anchoring
    // it to a parent whose own height comes from `content.implicitHeight`
    // would make the layout pass chase itself.
    implicitHeight: content.implicitHeight + 2 * Theme.pad

    ColumnLayout {
        id: content
        x: Theme.pad
        y: Theme.pad
        width: view.width - 2 * Theme.pad
        spacing: Theme.gap

        // What this page is for. A blank input with a placeholder does not
        // say where an answer comes from, or why a book might not be in one,
        // and both are worth knowing before the first question.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap

                VectorIcon {
                    width: 15
                    height: 15
                    path: Icons.bubble
                    color: Theme.text
                }

                Label {
                    text: "Ask your library"
                    color: Theme.text
                    font.pixelSize: 13
                    font.bold: true
                }

                Item { Layout.fillWidth: true }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 11
                text: "Ask about a subject, or about your library itself. "
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 11
                text: "Only indexed books are used. Use Index library to add the rest."
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            // The same control as the sidebar's search, not a lookalike: one
            // input style for the whole app, and no second definition of it to
            // drift.
            SearchBox {
                id: question
                Layout.fillWidth: true
                placeholder: "Ask your library: \"what maths books do I have?\""
                glyph: "?"
                live: false
                // Flush with the heading and the lines under it. The sidebar's
                // inset exists to match section headings there, and carrying it
                // onto a page just pushes the field out of line.
                inset: 0
                enabled: view.ready && !view.ai.busy
                onSubmitted: view.askNow()
            }

            BusyIndicator {
                running: view.ready && view.ai.busy
                visible: running
                implicitWidth: 18
                implicitHeight: 18
            }

            FlatButton {
                text: "Ask"
                enabled: view.ready && !view.ai.busy && question.text.trim() !== ""
                onClicked: view.askNow()
            }

            FlatButton {
                text: view.ready && view.ai.indexing ? "Stop indexing" : "Index library"
                tooltip: view.ready && view.ai.indexing
                    ? view.ai.status
                    : "Prepare books for questions. Runs locally, about two minutes a book."
                onClicked: {
                    if (!view.ready) return
                    if (view.ai.indexing) view.ai.cancelIndexing()
                    else {
                        // Indexing everything is background work, so it needs
                        // the policy switched on.
                        view.ai.setBackgroundEnabled(true)
                        view.ai.indexLibrary()
                    }
                }
            }
        }

        AnswerBox {
            Layout.fillWidth: true
            visible: view.ready && view.ai.answer !== ""
            text: view.ready ? view.ai.answer : ""
        }

        // Progress and failures are the app talking, not the model answering,
        // so they stay outside the card.
        Label {
            Layout.fillWidth: true
            visible: view.ready && view.ai.answer === "" && view.ai.status !== ""
            text: view.ready ? view.ai.status : ""
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 12
        }
    }

    function askNow() {
        if (!view.ready || question.text.trim() === "") return
        view.ai.askLibrary(question.text)
    }

    /// Put a question as though it had been typed here.
    function ask(text) {
        question.text = text
        view.askNow()
    }

    // The only separation from the grid below: one hairline.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.border
    }
}
