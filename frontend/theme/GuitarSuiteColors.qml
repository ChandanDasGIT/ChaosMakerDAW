import QtQuick

// The guitar suite's own colours (its buttons use the shared chip tokens in Theme.qml).
// Use it through the Theme singleton: Theme.guitarSuite.card, ...
QtObject {
    // ---- pedal cards ----
    readonly property color card:          "#2c2e34"
    readonly property color cardBorder:    "#454851"   // accent while the card is dragged
    readonly property color ampBand:       "#c8823c"   // the amp's band in the chain
    readonly property color cabinetBand:   "#7b8fa6"   // the cabinet's band

    // ---- the signal chain: cables between pedals, the IN and OUT jacks ----
    readonly property color cable:         "#5f8fa3"
    readonly property color jack:          "#23262c"
    readonly property color jackSocket:    "#0f1013"

    // ---- tuner ----
    readonly property color tunerScale:    "#202227"   // the cents scale
    readonly property color tunerZone:     "#294b39"   // the in-tune zone in its middle
    readonly property color inTune:        "#74d69a"
    readonly property color offTune:       "#efbd70"   // the needle while out of tune
}
