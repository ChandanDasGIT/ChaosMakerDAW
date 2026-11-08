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

    // The electronic drum page's drawn drum machine
    readonly property color machineBody:     "#2e3138"   // the case
    readonly property color machineEdge:     "#16181c"
    readonly property color machineTrim:     "#c8553d"   // the stripe across the top
    readonly property color machineLabel:    "#d9d4c7"   // printed text on the case
    readonly property color machineLcd:      "#9fb86a"   // the display, backlit green
    readonly property color machineLcdText:  "#1f2a12"
    readonly property color machineLed:      "#ff5a3c"   // a lit style button
    readonly property color machineLedOff:   "#4a2b26"
    readonly property color machineKnob:     "#1b1c20"
    readonly property color padKick:         "#b8463a"   // pads by family: kick and toms
    readonly property color padSnare:        "#c9913a"   // snare, clap, side stick
    readonly property color padHat:          "#3f8f9b"   // hi-hats
    readonly property color padCymbal:       "#7a62a8"   // crash and ride
}
