import QtQuick

// The track FX panel's own colours (its frame uses Theme.popupBg / Theme.popupBorder).
// Use it through the Theme singleton: Theme.effects.header, ...
QtObject {
    readonly property color header:          "#2b2f37"
    readonly property color sidebar:         "#1c1f25"   // list of effects on the left
    readonly property color itemHover:       "#2b3038"
    readonly property color itemSelected:    "#344044"

    // The drum kit page's drawn kit
    readonly property color drumShell:       "#6b2a2f"   // the wrap: dark red lacquer
    readonly property color drumShellEdge:   "#3d1619"
    readonly property color drumHead:        "#e9e4d8"   // coated heads
    readonly property color drumHeadEdge:    "#9b968a"
    readonly property color drumKickHead:    "#3a3a42"   // the kick's black front head
    readonly property color drumBrass:       "#c79a3c"   // cymbals
    readonly property color drumBrassEdge:   "#7d5d1e"
    readonly property color drumHardware:    "#8d9096"   // stands
}
