pragma Singleton
import QtQuick

// One place for every value the UI shares.
//
// Deliberately flat: no gradients, no shadows, no elevation. Separation comes
// from a single hairline border and a barely-lifted surface, which is what
// keeps the app looking like the rest of an Omarchy desktop.
QtObject {
    id: theme

    // Set from Main.qml once ThemeModel exists. Defaults are Omarchy's own
    // "solitude" dark theme, matching OmarchyTheme's C++ defaults, so this
    // file still reads right when previewed standalone.
    property bool dark: true
    property color accent: "#798186"

    // The raw Omarchy palette. Named apart from the tokens below because
    // Omarchy's own names cannot be trusted at face value: `lighter_background`
    // is measurably *darker* than `background` in both a light and a dark
    // real-world theme, so it goes unused. What holds in both is the ramp
    // darker_background < dark_background < background, and every token below
    // is built on exactly that ramp and nothing else.
    property color paletteBackground: "#101315"
    property color paletteDarkBackground: "#0c0e10"
    property color paletteDarkerBackground: "#080a0b"
    property color paletteSelection: "#343d41"
    property color paletteForeground: "#cacccc"
    // `muted` needs no ramp position -- it maps straight across.
    property color muted: "#4b4e55"

    // Deepest of the ramp: the page sits below every card.
    readonly property color background: paletteDarkerBackground
    readonly property color text:       paletteForeground

    // Selection is used for both: a border is a thin selection-coloured line,
    // and `selected` is the same colour filling a whole row/card.
    readonly property color border:     paletteSelection
    readonly property color selected:   paletteSelection
    // Quieter than `border`: sits between the page and a card, one step short
    // of the ramp's top.
    readonly property color cardBorder: paletteDarkBackground
    readonly property color coverPlaceholder: paletteDarkBackground

    // Top of the ramp, so cards read as lifted off the page in dark themes
    // and as bright panels in light ones.
    readonly property color surface:    paletteBackground
    // Must differ visibly from `surface`; nudged toward `text` rather than
    // toward black or white, so hover reads the same direction the theme
    // already leans.
    readonly property color surfaceHover: dark ? Qt.lighter(surface, 200) : Qt.darker(surface, 110)


    /// Cover height as a share of card width. Roughly the proportions of a
    /// trade paperback, which suits most covers without cropping heavily.
    readonly property real coverRatio: 1.42

    /// The brand green, taken from the logo itself.
    readonly property color brand: "#9ece6a"

    /// omarchy.org sets `--font-family: 'JetBrains Mono', monospace` and uses
    /// nothing else, so the wordmark follows it. Installed here as a Nerd Font
    /// variant, hence the fallback chain rather than one hard-coded name.
    readonly property string brandFont: {
        var installed = Qt.fontFamilies()
        var wanted = ["JetBrains Mono", "JetBrainsMono Nerd Font",
                      "JetBrainsMono NF", "JetBrains Mono NL"]
        for (var i = 0; i < wanted.length; i++)
            if (installed.indexOf(wanted[i]) !== -1)
                return wanted[i]
        return "monospace"
    }

    readonly property int gap: 8
    /// Space between grid cards. Generous on purpose: the covers carry the
    /// visual weight, so the grid needs air to stay calm.
    readonly property int gridGap: 18
    readonly property int pad: 14
    /// Card and control corner radius. Six read as square at this display
    /// scale — the arc came out only two or three physical pixels — so it is
    /// a little larger now, but deliberately tight.
    readonly property int radius: 8
    /// A tighter corner, for blocks that sit inside a page rather than float
    /// on it.
    readonly property int radiusSm: 4
    readonly property int headerHeight: 60
    readonly property int sidebarWidth: 248
}
