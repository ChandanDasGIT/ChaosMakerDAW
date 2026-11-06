import QtQuick

// The mixer's own colours (its M/S buttons and knob use the shared chip tokens in Theme.qml).
// Use it through the Theme singleton: Theme.mixer.strip, ...
QtObject {
    readonly property color background:   "#1d1d1d"
    readonly property color master:       "#181818"   // master channel
    readonly property color strip:        "#222222"
    readonly property color stripAlt:     "#202020"   // every other strip
    readonly property color resizeHandle: "#676767"   // strip edge while hovered or dragged
    readonly property color knobBorder:   "#4a4a4a"   // pan knob at centre (accent when panned)
}
