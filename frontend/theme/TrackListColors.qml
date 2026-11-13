import QtQuick

// The track list's own colours (shared button and popup colours live in Theme.qml).
// Use it through the Theme singleton: Theme.trackList.rowSelected, ...
QtObject {
    // ---- track rows ----
    readonly property color row:               "#2a2a2a"
    readonly property color rowAlt:            "#272727"   // every other row
    readonly property color rowSelected:       "#2a343d"
    readonly property color rowSelectedBorder: "#3f5a6e"   // its thin edge
    readonly property color nameEditor:        "#292e36"   // inline rename field

    // ---- track colour swatches ----
    readonly property color swatchBorder:      "#777777"   // the track's colour button
    readonly property color swatchChoiceBorder:"#45484e"   // palette entries in the colour popup

    // ---- options in the input and track-type pickers ----
    readonly property color option:            "#2c2e34"
    readonly property color optionHover:       "#343841"
    readonly property color optionBorder:      "#40434b"
    readonly property color backHover:         "#383b42"   // "back" button in the track-type picker
}
