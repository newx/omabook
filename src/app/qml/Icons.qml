pragma Singleton
import QtQuick

// SVG path data for the app's icons, all drawn in a 24x24 box.
//
// Vectors rather than emoji glyphs: emoji render in the font's own colours and
// weights, which ignores the theme and varies between systems. These take
// Theme colours and stay crisp at any size.
//
// One drawing style throughout: a single stroked outline, no fills of their
// own, no detail that turns to mush at 15px — which is the size the grid's
// card actions and the sidebar rows both use.
QtObject {
    readonly property int box: 24

    // Save for later. A bookmark ribbon with a notched foot — the shape the
    // reading queue uses everywhere.
    readonly property string bookmark:
        "M7 2 H17 A2 2 0 0 1 19 4 V21 L12 16.4 L5 21 V4 A2 2 0 0 1 7 2 Z"

    readonly property string heart:
        "M12 20.6 C12 20.6 3.6 15.3 3.6 9.4 A4.6 4.6 0 0 1 12 6.7 " +
        "A4.6 4.6 0 0 1 20.4 9.4 C20.4 15.3 12 20.6 12 20.6 Z"

    readonly property string trash:
        "M4 6 H20 M9 6 V4 A1 1 0 0 1 10 3 H14 A1 1 0 0 1 15 4 V6 " +
        "M6 6 V20 A1 1 0 0 0 7 21 H17 A1 1 0 0 0 18 20 V6 M10 10 V17 M14 10 V17"

    // The whole library: the grid of covers itself, in miniature.
    readonly property string grid:
        "M4 4 H10 V10 H4 Z M14 4 H20 V10 H14 Z M4 14 H10 V20 H4 Z M14 14 H20 V20 H14 Z"

    // Reading now: an open book, spine down the middle.
    readonly property string bookOpen:
        "M12 6.5 C9.5 5 6.6 4.7 3.8 5.4 V17.6 C6.6 16.9 9.5 17.2 12 18.7 " +
        "C14.5 17.2 17.4 16.9 20.2 17.6 V5.4 C17.4 4.7 14.5 5 12 6.5 Z M12 6.5 V18.7"

    // Finished. A bare tick: a tick inside a circle is mush at this size.
    readonly property string check: "M4.6 12.4 L9.6 17.4 L19.4 6.6"

    // Highlights and notes: a pencil, with the line where its ferrule sits.
    readonly property string pencil:
        "M4 20 L4.9 15.9 L16.1 4.7 A2.2 2.2 0 0 1 19.3 7.9 L8.1 19.1 Z M14.6 6.2 L17.8 9.4"

    // Ask: a speech bubble with a tail at the lower left.
    readonly property string bubble:
        "M5 4.5 H19 A2 2 0 0 1 21 6.5 V14.5 A2 2 0 0 1 19 16.5 H10.5 L6.5 20 V16.5 " +
        "H5 A2 2 0 0 1 3 14.5 V6.5 A2 2 0 0 1 5 4.5 Z"

    // Settings: two sliders. A cog's teeth disappear at 15px; these do not.
    readonly property string sliders:
        "M4 8 H10 M14.4 8 H20 M10 8 A2.2 2.2 0 0 1 14.4 8 A2.2 2.2 0 0 1 10 8 Z " +
        "M4 16 H13 M17.4 16 H20 M13 16 A2.2 2.2 0 0 1 17.4 16 A2.2 2.2 0 0 1 13 16 Z"

    readonly property string plus: "M12 5 V19 M5 12 H19"
}
