import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import com.omabook.app

// One book in the grid: a cover filling the card's width, then title, author,
// and a footer of actions.
Item {
    id: card

    property string bookTitle: ""
    property string bookAuthor: ""
    property real readProgress: 0
    property string bookFormat: ""
    property string cover: ""
    property bool favorite: false
    property bool queued: false
    /// Place in the reading queue, counting from 1. Zero hides the badge —
    /// only the queue itself has an order worth showing.
    property int queuePosition: 0
    /// "good" | "poor" | "none" | "". Only the two bad cases are worth a
    /// badge; a book that reads fine has nothing to say about it (SPEC §7.2).
    property string textQuality: ""
    /// Set by the grid when this card is the one keyboard navigation is on.
    property bool focused: false

    signal opened()
    signal favoriteToggled()
    signal queueToggled()
    signal deleteRequested()

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Theme.radius
        color: hover.hovered ? Theme.surfaceHover : Theme.surface
        border.width: card.focused ? 2 : 1
        border.color: card.focused ? Theme.accent : Theme.cardBorder
        // Clips the sides and foot. It cannot round the cover's top corners on
        // its own: QML's `clip` is a rectangular scissor and ignores `radius`,
        // which is why the cover carries its own mask below.
        clip: true

        HoverHandler { id: hover }
        TapHandler { onTapped: card.opened() }

        Column {
            anchors.fill: parent
            spacing: 0

            // A fixed height, so a short or oddly-proportioned cover cannot
            // pull the title up and break the grid's alignment. Covers vary
            // wildly: tall paperbacks, square textbooks, wide PDF spreads.
            Item {
                id: coverArea
                width: parent.width
                height: Math.round(parent.width * Theme.coverRatio)

                // Placeholder, shown until a cover loads. Per-corner radius so
                // it meets the card's rounded top exactly.
                Rectangle {
                    anchors.fill: parent
                    color: Theme.coverPlaceholder
                    topLeftRadius: Theme.radius - 1
                    topRightRadius: Theme.radius - 1

                    Label {
                        anchors.centerIn: parent
                        visible: cover.status !== Image.Ready
                        text: card.bookFormat.toUpperCase()
                        color: Theme.muted
                        font.pixelSize: 11
                        font.letterSpacing: 2
                    }
                }

                Image {
                    id: cover
                    anchors.fill: parent
                    source: card.cover
                    // Fill the fixed frame; the placeholder behind shows through
                    // only while loading or when there is no cover at all.
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    sourceSize.height: 520
                    // Drawn through the mask below rather than directly.
                    visible: false
                    layer.enabled: true
                }

                // The shape the cover is cut to: square at the foot, rounded at
                // the top to match the card.
                Item {
                    id: coverMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true

                    Rectangle {
                        anchors.fill: parent
                        color: "white"
                        topLeftRadius: Theme.radius - 1
                        topRightRadius: Theme.radius - 1
                    }
                }

                MultiEffect {
                    anchors.fill: parent
                    source: cover
                    visible: cover.status === Image.Ready
                    maskEnabled: true
                    maskSource: coverMask
                    // A hard mask leaves a visibly stepped arc. Spreading the
                    // threshold slightly antialiases the curve.
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                }

                // Place in the queue, floating over the cover's top left.
                // Translucent black rather than a theme colour: it sits on
                // artwork of any brightness and has to stay legible on all of
                // it, and it is small enough to cover no part of the cover
                // that carries meaning.
                Rectangle {
                    visible: card.queuePosition > 0
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 6
                    width: Math.max(20, ordinal.implicitWidth + 12)
                    height: 20
                    radius: height / 2
                    color: Qt.rgba(0, 0, 0, 0.62)

                    Label {
                        id: ordinal
                        anchors.centerIn: parent
                        text: card.queuePosition
                        color: "#ffffff"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }

                // Text quality, floating over the cover's top right. Same
                // translucent-black treatment as the queue badge, opposite
                // corner so the two never overlap.
                Rectangle {
                    visible: card.textQuality === "poor" || card.textQuality === "none"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    width: 20
                    height: 20
                    radius: height / 2
                    color: Qt.rgba(0, 0, 0, 0.62)

                    HoverHandler { id: qualityHover }

                    Label {
                        anchors.centerIn: parent
                        text: "⚠"
                        color: "#ffffff"
                        font.pixelSize: 11
                    }

                    ToolTip.visible: qualityHover.hovered
                    ToolTip.text: card.textQuality === "none"
                        ? "No extractable text — this looks like a scanned PDF"
                        : "This book's text extracted poorly"
                    ToolTip.delay: 500
                }

                // A hairline under the cover, separating it from the metadata.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.cardBorder
                }

                // Reading progress, sitting on the cover's lower edge.
                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: parent.width * Math.min(card.readProgress, 1)
                    height: 2
                    visible: card.readProgress > 0
                    color: Theme.accent
                }
            }

            Column {
                id: meta
                width: parent.width
                padding: 10
                spacing: 3

                Label {
                    text: card.bookTitle
                    width: meta.width - 20
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    font.bold: true
                    color: Theme.text
                }

                Label {
                    text: card.bookAuthor
                    width: meta.width - 20
                    elide: Text.ElideRight
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }
        }

        // Footer actions, pinned to the bottom so they line up across the row
        // regardless of how many lines a title takes.
        Row {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 6
            anchors.bottomMargin: 6
            spacing: 2

            IconButton {
                path: Icons.heart
                active: card.favorite
                tooltip: card.favorite ? "Remove from favorites" : "Add to favorites"
                onClicked: card.favoriteToggled()
            }

            IconButton {
                // A bookmark ribbon: save it for later.
                path: Icons.bookmark
                active: card.queued
                tooltip: card.queued ? "Remove from reading queue" : "Save for later"
                onClicked: card.queueToggled()
            }

            IconButton {
                path: Icons.trash
                tooltip: "Remove from library"
                onClicked: card.deleteRequested()
            }
        }
    }
}
