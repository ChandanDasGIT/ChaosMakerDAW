import QtQuick

// Chaos EQ display colours (plan 6.2). Use it through the Theme singleton: Theme.chaosEq.grid, ...
QtObject {
    readonly property color background:   "#15171b"   // behind the curve
    readonly property color grid:         "#24282f"
    readonly property color gridStrong:   "#323843"   // 0 dB line and decade lines
    readonly property color gridLabel:    "#7d828c"
    readonly property color spectrumFill: "#22404d"
    readonly property color spectrumLine: "#3f6f84"
    readonly property color curve:        "#60cdff"   // the summed response
    readonly property color bypassed:     "#6b6f78"
    readonly property color nodeText:     "#101215"
    readonly property color inspectorBg:  "#1d2026"
    // Band node colours, by slot (repeats after eight).
    readonly property var bandColors: ["#ff7a7a", "#ffb35c", "#f2dc5d", "#7ddc7a",
                                       "#5fd8c8", "#6aa8ff", "#a98bff", "#f07ad6"]
}
