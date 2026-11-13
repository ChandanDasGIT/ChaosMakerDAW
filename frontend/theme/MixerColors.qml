import QtQuick

// The mixer's own colours (its M/S buttons and knob use the shared chip tokens in Theme.qml).
// Use it through the Theme singleton: Theme.mixer.strip, ...
QtObject {
    readonly property color background:   "#161616"
    readonly property color master:       "#2c2c2c"   // master channel
    readonly property color strip:        "#282828"
    readonly property color stripAlt:     "#252525"   // every other strip
    readonly property color nameBand:     "#333333"   // name band of a strip without a track colour (and the master)
    readonly property color resizeHandle: "#676767"   // strip edge while hovered or dragged
    readonly property color knobBorder:   "#4a4a4a"   // pan knob at centre (accent when panned)
}
