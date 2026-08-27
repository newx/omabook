import QtQuick
import QtQuick.Controls

// Where an answer from the model lands.
//
// A card rather than prose set loose in the page: an answer is quoted from
// somewhere else, and giving it its own ground keeps it from reading as
// something the app is asserting. The ground is a step off the page in
// whichever direction the theme runs — see Theme.answerSurface.
Item {
    id: box

    property string text: ""
    /// Equal on all four sides. Text against a corner reads as cramped.
    property int padding: Theme.pad

    implicitHeight: body.implicitHeight + 2 * box.padding

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.answerSurface

        Label {
            id: body
            x: box.padding
            y: box.padding
            width: parent.width - 2 * box.padding
            text: box.text
            wrapMode: Text.WordWrap
            color: Theme.answerText
            font.pixelSize: 12
            // The model returns prose, not markup; rendering it as rich text
            // would let a stray angle bracket eat the answer.
            textFormat: Text.PlainText
        }
    }
}
