pragma Singleton
import QtQuick

// Every colour and size of the app lives here. Use it anywhere as Theme.windowBg, Theme.accent, ...
QtObject {
    // ---- surfaces ----
    readonly property color windowBg:  "#191919"
    readonly property color panelBg:   "#202020"
    readonly property color divider:   "#333333"

    // ---- text ----
    readonly property color text:      "#ededed"
    readonly property color textMuted: "#a8a8a8"

    // ---- tooltips ----
    readonly property color tooltipBg: "#2b2d32"
    readonly property color tooltipText: "#f3f4f6"

    // ---- states ----
    readonly property color accent:    "#60cdff"
    readonly property color record:    "#e5484d"

    // ---- sizes ----
    readonly property int bodySize:    13
    readonly property int titleSize:   28
    readonly property int stripHeight: 28
    readonly property int margin:      10
}
