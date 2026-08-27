import QtQuick
import QtQuick.Shapes
import com.omabook.app

// One icon, stroked when inactive and filled when active.
Item {
    id: icon

    property string path: ""
    property color color: Theme.muted
    property bool filled: false
    property real strokeWidth: 1.6
    /// The coordinate system `path` is drawn in.
    property int box: Icons.box

    implicitWidth: 16
    implicitHeight: 16

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        // Antialiased edges at small sizes, which matters most here.
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.strokeWidth * (icon.width / icon.box)
            fillColor: icon.filled ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            // Scale the 24-unit path into whatever size this icon is given.
            scale: Qt.size(icon.width / icon.box, icon.height / icon.box)

            PathSvg { path: icon.path }
        }
    }
}
