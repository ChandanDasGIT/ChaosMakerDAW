import QtQuick

// The timeline's own colours. Use it through the Theme singleton: Theme.timeline.lane, ...
QtObject {
    // ---- ruler and grid ----
    readonly property color ruler:              "#252525"
    readonly property color rulerBarLine:       "#4a4a4a"
    readonly property color gridBar:            "#454545"
    readonly property color gridBeat:           "#303030"

    // ---- track lanes ----
    readonly property color lane:               "#202020"
    readonly property color laneAlt:            "#1c1c1c"   // every other lane
    readonly property color laneSelected:       "#232932"
    readonly property color laneBorder:         "#303030"
    readonly property color laneSelectedBorder: "#3a4454"

    // ---- clips ----
    readonly property color clipLabel:          "#f0f0f0"
    readonly property color waveform:           "#d6edf0"
    readonly property color trimHandle:         "#ffffff24" // clip edge handles (accent on hover)

    // ---- selections (border: Theme.accent) ----
    readonly property color selectionBox:       "#3860cdff" // rubber band over clips
    readonly property color timeRange:          "#2858b7b1" // selected time range
}
