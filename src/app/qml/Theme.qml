pragma Singleton
import QtQuick

// One place for every value the UI shares.
//
// Deliberately flat: no gradients, no shadows, no elevation. Separation comes
// from a single hairline border and a barely-lifted surface, which is what
// keeps the app looking like the rest of an Omarchy desktop.
QtObject {
    id: theme

    // Set from Main.qml once the Rust ThemeModel exists.
    property bool dark: true
    property color accent: "#798186"

    // A near-black ground in dark mode, matching the terminal, rather than the
    // theme's slightly-blue background.
    readonly property color background: dark ? "#000000" : "#ffffff"
    readonly property color text:       dark ? "#e6e6e6" : "#141414"
    readonly property color muted:      dark ? "#8a8a8a" : "#5f5f5f"

    // Borders are a visible light grey, not a whisper — they are the only
    // separation this design uses.
    readonly property color border:     dark ? "#3a3a3a" : "#d0d0d0"
    // Grid cards sit quieter than structural borders: thinner in feel, and
    // closer to the background — but still far enough from it to draw an edge.
    // The light value used to be #e6e6e6, which vanished against a white page
    // and left every pale cover floating without a card around it. Both values
    // now sit about the same distance from their own ground.
    readonly property color cardBorder: dark ? "#242424" : "#d9d9d9"
    readonly property color coverPlaceholder: dark ? "#0a0a0a" : "#efefef"
    readonly property color surface:    dark ? "#0d0d0d" : "#f6f6f6"
    readonly property color surfaceHover: dark ? "#171717" : "#ededed"
    readonly property color selected:   dark ? "#1f1f1f" : "#e4e4e4"

    // An answer from the model gets its own ground: it is quoted material, and
    // a card lifted off the page keeps it from reading as something the app is
    // asserting. Lifted in the theme's own direction, though — a light card in
    // a dark window was a hole punched in the page.
    readonly property color answerSurface: dark ? "#1c1c1c" : "#eaeaea"
    readonly property color answerText:    dark ? "#e6e6e6" : "#141414"

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
