import QtQuick
import QtQuick.Controls
import com.omabook.app

// Search box above the sidebar's MAIN section.
//
// Named SearchBox, not SearchField: Qt Quick Controls 6.11 ships a type of the
// latter name, and an explicit `import QtQuick.Controls` beats same-module
// resolution — so the collision silently resolves to Qt's control and every
// custom signal on it appears not to exist.
Item {
    id: field

    property string placeholder: "Search"
    property alias text: input.text
    /// Whether the input itself holds keyboard focus — plain `field.activeFocus`
    /// stays false here because `field` is not a FocusScope, so single-letter
    /// shortcuts elsewhere have to ask the actual TextInput.
    readonly property alias hasFocus: input.activeFocus
    /// The leading glyph. A magnifier for search, a question mark for Ask.
    property string glyph: "\u2315"
    /// Whether typing submits on its own. True for search, where the cost of a
    /// query is one FTS lookup; false for Ask, where every submission is an
    /// LLM call and only Return should spend one.
    property bool live: true
    /// TextInput.Password for secrets. The key field is this control too.
    property int echoMode: TextInput.Normal
    /// How far the box sits in from this item's edges. Theme.pad in the
    /// sidebar, to line up with the section headings; zero on a page, where it
    /// lines up with the heading and body text above it instead.
    property int inset: Theme.pad

    signal submitted(string query)

    height: 34

    /// Driven by Main.qml's Ctrl+F: jump here and offer the existing text for
    /// replacement, the way a browser's find field does.
    function focusAndSelectAll() {
        input.forceActiveFocus()
        input.selectAll()
    }

    /// Clears the field's remembered focus within its enclosing FocusScope.
    /// A FocusScope hands active focus straight back to whichever descendant
    /// last held it — so once this field has been focused, Sidebar's own
    /// `forceActiveFocus()` (for "s" / row navigation) would otherwise land
    /// back here instead of on the scope itself. Called before that happens.
    function blur() {
        input.focus = false
    }

    Rectangle {
        anchors.fill: parent
        // Defaults to Theme.pad, the same margin the sidebar's section
        // headings use, so the field's edge lines up with "MAIN" rather than
        // sitting further out than everything below it. A page sets its own.
        anchors.leftMargin: field.inset
        anchors.rightMargin: field.inset
        radius: 4
        color: Theme.background
        border.width: 1
        border.color: input.activeFocus ? Theme.accent : Theme.border

        Label {
            id: glass
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: field.glyph
            color: Theme.muted
            font.pixelSize: 15
        }

        TextInput {
            id: input
            anchors.left: glass.right
            anchors.leftMargin: 6
            // With nothing typed there is no clear button, and anchoring to it
            // anyway left 34px of dead space against 8px on the other side.
            anchors.right: clear.visible ? clear.left : parent.right
            anchors.rightMargin: clear.visible ? 6 : 8
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            font.pixelSize: 13
            echoMode: field.echoMode
            selectByMouse: true
            clip: true

            // Debounced: a query per keystroke would re-run FTS on every letter.
            onTextChanged: if (field.live) debounce.restart()
            Keys.onReturnPressed: { debounce.stop(); field.submitted(text) }
            Keys.onEscapePressed: {
                text = ""
                debounce.stop()
                if (field.live) field.submitted("")
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: input.text === "" && !input.activeFocus
                text: field.placeholder
                color: Theme.muted
                font.pixelSize: 13
            }
        }

        IconButton {
            id: clear
            anchors.right: parent.right
            // The button carries 2px of its own, so 6 here puts its glyph the
            // same 8px from the edge as the leading one.
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            visible: input.text !== ""
            glyph: "×"
            tooltip: "Clear"
            onClicked: {
                input.text = ""
                debounce.stop()
                if (field.live) field.submitted("")
            }
        }
    }

    Timer {
        id: debounce
        interval: 220
        onTriggered: field.submitted(input.text)
    }
}
