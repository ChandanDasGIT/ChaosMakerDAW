import QtQuick

// The guitar suite's own colours (its buttons use the shared chip tokens in Theme.qml).
// Use it through the Theme singleton: Theme.guitarSuite.card, ...
QtObject {
    // ---- pedal cards ----
    readonly property color card:          "#2c2e34"
    readonly property color cardBorder:    "#454851"   // accent while the card is dragged
    readonly property color sliderTrack:   "#3a3d45"   // parameter sliders (fill: Theme.accent)
    readonly property color handleBorder:  "#1e2025"   // slider handle (accent when focused)

    // ---- tuner ----
    readonly property color tunerScale:    "#202227"   // the cents scale
    readonly property color tunerZone:     "#294b39"   // the in-tune zone in its middle
    readonly property color inTune:        "#74d69a"
    readonly property color offTune:       "#efbd70"   // the needle while out of tune
}
