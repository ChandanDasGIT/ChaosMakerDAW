import QtQuick

// The timeline's own colours. Use it through the Theme singleton: Theme.timeline.lane, ...
QtObject {
    // ---- ruler and grid ----
    readonly property color ruler:              "#272727"
    readonly property color rulerBarLine:       "#555555"
    readonly property color rulerLabel:         "#d0d0d0"   // bar numbers
    readonly property color gridBar:            "#4a4a4a"
    readonly property color gridBeat:           "#303030"

    // ---- track lanes ----
    readonly property color lane:               "#1c1c1c"
    readonly property color laneAlt:            "#191919"   // every other lane
    readonly property color laneSelected:       "#232932"
    readonly property color laneBorder:         "#262626"
    readonly property color laneSelectedBorder: "#3a4454"

    // ---- clips ----
    readonly property color clipLabel:          "#f0f0f0"
    readonly property color clipNeutral:        "#868b94"   // a clip on a track without a colour (shaded like a track colour)
    readonly property color waveform:           "#d6edf0"
    readonly property color trimHandle:         "#24ffffff" // clip edge handles: faint white (accent on hover)

    // ---- selections (border: Theme.accent) ----
    readonly property color selectionBox:       "#3860cdff" // rubber band over clips
    readonly property color timeRange:          "#2858b7b1" // selected time range

    // ---- volume envelope (restMap G6) ----
    readonly property color envelopeLine:       "#7fd36b"
    readonly property color envelopeFill:       "#307fd36b" // shaded area under the line
    readonly property color envelopeBypassed:   "#6a6a6a"   // line and points while bypassed
    readonly property color envelopeLaneTop:    "#3a3a3a"   // the lane's top edge
    readonly property color envelopePointSelected: "#ffffff"
}
