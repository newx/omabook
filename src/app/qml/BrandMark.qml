import QtQuick

// The wordmark: the Omarchy book glyph, then "OmaBooks" beside it.
//
// The glyph is already #9ece6a, so the word takes the same colour straight from
// the logo rather than approximating it.
Row {
    id: brand

    property int glyphSize: 22
    /// Whether the name follows the glyph. The reader drops it: the glyph
    /// stays put as the app's mark, and the book's own title takes the space
    /// beside it.
    property bool wordmark: true

    spacing: 9

    Image {
        source: "qrc:/brand/mark.png"
        width: brand.glyphSize
        height: brand.glyphSize
        sourceSize.width: brand.glyphSize * 3   // crisp on a scaled display
        sourceSize.height: brand.glyphSize * 3
        smooth: true
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        visible: brand.wordmark
        text: "OmaBooks"
        color: Theme.brand
        font.family: Theme.brandFont
        font.pixelSize: Math.round(brand.glyphSize * 0.86)
        font.weight: Font.Medium
        anchors.verticalCenter: parent.verticalCenter
    }
}
