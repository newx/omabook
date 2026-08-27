import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// One setting: a label on the left, its controls on the right, and an optional
// line underneath saying what the choice actually does.
//
// Controls are declared as children and reparented into the row, so a setting
// reads as one block at the call site rather than as nested layouts.
Item {
    id: setting

    property string label: ""
    property string hint: ""
    default property alias controls: controlRow.data

    Layout.fillWidth: true
    /// Inset by the same amount on every side. The extra four keeps a row's
    /// text under its heading's, which is inset by as much again.
    readonly property int inset: Theme.pad + 4

    implicitHeight: column.implicitHeight + 2 * setting.inset

    ColumnLayout {
        id: column
        x: setting.inset
        y: setting.inset
        width: setting.width - 2 * setting.inset
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Label {
                Layout.preferredWidth: 150
                text: setting.label
                color: Theme.text
                font.pixelSize: 13
            }

            RowLayout {
                id: controlRow
                Layout.fillWidth: true
                spacing: Theme.gap
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 150 + Theme.gap
            visible: setting.hint !== ""
            text: setting.hint
            color: Theme.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
