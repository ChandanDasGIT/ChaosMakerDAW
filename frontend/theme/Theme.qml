pragma Singleton
import QtQuick

// Every colour and size of the app lives here, in frontend/theme/. Use it anywhere as Theme.windowBg,
// Theme.accent, ...; area palettes are sub-objects: Theme.pianoRoll.toolButton, ...
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
    readonly property color textStrong:    "#ffffff"   // active/hover text on dark or red buttons
    readonly property color textMuted:     "#a8a8a8"

    // ---- tooltips ----
    readonly property color tooltipBg:     "#2b2d32"
    readonly property color tooltipText:   "#f3f4f6"

    // ---- states & accents ----
    readonly property color accent:        "#60cdff"
    readonly property color record:        "#e5484d"
    readonly property color focusRing:     "#60cdff"
    readonly property color selectionBg:   "#24426b"
    readonly property color accentBg:      "#203a45"   // background of a lit (accent) toggle
    readonly property color recordBg:      "#a9363c"   // armed record / remove-on-hover background
    readonly property color recordActiveBg:"#663238"   // record button while recording, recording region
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
    readonly property color meterClipOff:  "#393b42"   // clip light while not clipped

    // ---- controls & buttons ----
    readonly property color controlBg:       "#2a2a2a"
    readonly property color controlBgHover:  "#353535"
    readonly property color controlBgActive: "#3d3d3d"
    readonly property color controlBorder:   "#404040"
    // Small toggle chips (track card IN/MON/FX/REC/M/S, mixer M/S, pedal and EQ buttons)
    readonly property color chip:            "#303030"
    readonly property color chipHover:       "#383838"
    readonly property color chipBorder:      "#424242"
    readonly property color chipBorderHover: "#626671"

    // ---- fields (drop-down boxes) ----
    readonly property color field:           "#292b30"
    readonly property color fieldHover:      "#30333a"
    readonly property color fieldDisabled:   "#222328"
    readonly property color fieldBorder:     "#444750"   // accent when focused
    readonly property color textDisabled:    "#777b83"

    // ---- popups ----
    readonly property color popupBg:         "#25272c"
    readonly property color popupBorder:     "#4a4d56"

    // ---- track colour palette (the track colour picker) ----
    readonly property var trackColors: [
        "#58b7b1", "#6fa8dc", "#b18bd9", "#de849c", "#e2a565", "#d8c16b",
        "#86bd83", "#9babb7", "#d97c73", "#72b9ce", "#8c9bdd", "#b49a87"
    ]

    // ---- area palettes (one file each in frontend/theme/) ----
    readonly property PianoRollColors pianoRoll: PianoRollColors {}
    readonly property TrackListColors trackList: TrackListColors {}
    readonly property TimelineColors timeline: TimelineColors {}
    readonly property MixerColors mixer: MixerColors {}
    readonly property TransportColors transport: TransportColors {}
    readonly property GuitarSuiteColors guitarSuite: GuitarSuiteColors {}
    readonly property EffectsColors effects: EffectsColors {}

    // ---- sizes ----
    readonly property int microSize:   9    // the smallest text allowed (tiny labels, chips)
    readonly property int captionSize: 10
    readonly property int smallSize:   11
    readonly property int bodySize:    13
    readonly property int titleSize:   28
    readonly property int stripHeight: 28
    readonly property int margin:      10

    // ---- typography ----
    readonly property string monospaceFontFamily: "Consolas, 'Cascadia Code', 'Fira Code', 'DejaVu Sans Mono', monospace"
}
