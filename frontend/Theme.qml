pragma Singleton
import QtQuick

// Every colour and size of the app lives here. Use it anywhere as Theme.windowBg, Theme.accent, ...
QtObject {
    // ---- surfaces ----
    readonly property color windowBg:      "#191919"
    readonly property color panelBg:       "#202020"
    readonly property color panelHeaderBg: "#252525"
    readonly property color cardBg:        "#222222"
    readonly property color cardBgAlt:     "#272727"
    readonly property color divider:       "#333333"
    readonly property color hoverOverlay:  "#1affffff"
    readonly property color pressedOverlay:"#26ffffff"

    // ---- text ----
    readonly property color text:          "#ededed"
    readonly property color textMuted:     "#a8a8a8"

    // ---- tooltips ----
    readonly property color tooltipBg:     "#2b2d32"
    readonly property color tooltipText:   "#f3f4f6"

    // ---- states & accents ----
    readonly property color accent:        "#60cdff"
    readonly property color record:        "#e5484d"
    readonly property color focusRing:     "#60cdff"
    readonly property color selectionBg:   "#24426b"
    // Mute is orange and solo yellow everywhere (red is kept for recording).
    readonly property color muteText:      "#ffc08a"
    readonly property color muteBg:        "#653f28"
    readonly property color muteBorder:    "#c87943"
    readonly property color soloText:      "#ffe08a"
    readonly property color soloBg:        "#665322"
    readonly property color soloBorder:    "#c49d39"

    // ---- level meters (calibrated scale) ----
    readonly property color meterNormal:   "#22c55e"
    readonly property color meterWarning:  "#f59e0b"
    readonly property color meterHot:      "#f97316"
    readonly property color meterClip:     "#ef4444"
    readonly property color meterBackdrop: "#141414"

    // ---- controls & buttons ----
    readonly property color controlBg:       "#2a2a2a"
    readonly property color controlBgHover:  "#353535"
    readonly property color controlBgActive: "#3d3d3d"
    readonly property color controlBorder:   "#404040"

    // ---- curated track color palette ----
    readonly property var trackColors: [
        "#3b82f6", // Blue
        "#10b981", // Emerald
        "#f59e0b", // Amber
        "#ef4444", // Crimson
        "#8b5cf6", // Purple
        "#ec4899", // Pink
        "#06b6d4"  // Cyan
    ]

    // ---- sizes ----
    readonly property int captionSize: 10
    readonly property int smallSize:   11
    readonly property int bodySize:    13
    readonly property int titleSize:   28
    readonly property int stripHeight: 28
    readonly property int margin:      10

    // ---- typography ----
    readonly property string monospaceFontFamily: "Consolas, 'Cascadia Code', 'Fira Code', 'DejaVu Sans Mono', monospace"
}
