import QtQuick

// The track FX panel's own colours (its frame uses Theme.popupBg / Theme.popupBorder).
// Use it through the Theme singleton: Theme.effects.header, ...
QtObject {
    readonly property color header:          "#2b2f37"
    readonly property color sidebar:         "#1c1f25"   // list of effects on the left
    readonly property color itemHover:       "#2b3038"
    readonly property color itemSelected:    "#344044"
}
