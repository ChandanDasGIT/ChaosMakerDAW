import QtQuick

// The piano roll's palette: a cooler blue-grey than the rest of the app. Use it through the
// Theme singleton: Theme.pianoRoll.toolButton, Theme.pianoRoll.gridBeat, ...
QtObject {
    // ---- surfaces ----
    readonly property color background:       "#23252a"   // behind everything
    readonly property color menuBar:          "#32353e"   // top menu bar, velocity lane header
    readonly property color menuBarBorder:    "#212328"
    readonly property color toolStrip:        "#2a2c33"   // tool strip, velocity lane label column
    readonly property color panel:            "#2c2e35"   // readout header above the keys, footer
    readonly property color panelBorder:      "#1d1f24"   // borders of strips, panels and boxes
    readonly property color ruler:            "#282a31"   // time ruler, velocity lane
    readonly property color readoutBox:       "#1b1d22"   // position / note readouts, velocity popup
    readonly property color readoutBorder:    "#454a56"
    readonly property color separator:        "#3e424d"   // thin vertical dividers

    // ---- text ----
    readonly property color text:             "#d8dce4"   // icons and main labels
    readonly property color textMuted:        "#8a909d"   // clip name, combo arrows
    readonly property color label:            "#8f95a3"   // footer captions ("Grid:", "Zoom:")
    readonly property color buttonText:       "#c8ccd6"   // footer transport and zoom buttons

    // ---- tool buttons (pointer, pencil, eraser, snap, quantize, undo, redo) ----
    readonly property color toolButton:       "#2f323a"
    readonly property color toolButtonHover:  "#373a44"
    readonly property color toolButtonActive: "#4a505e"
    readonly property color buttonBorder:     "#202227"

    // ---- small footer buttons (transport, zoom) ----
    readonly property color smallButton:      "#353842"
    readonly property color smallButtonHover: "#454955"
    readonly property color transportActive:  "#203a45"   // the play button while playing

    // ---- combo boxes ----
    readonly property color combo:            "#32353e"
    readonly property color comboHover:       "#3c404b"
    readonly property color comboPressed:     "#454955"
    readonly property color comboPopup:       "#26282f"

    // ---- ruler and grid ----
    readonly property color playhead:         "#38b2ac"   // playhead line and the ruler's top edge
    readonly property color rulerTickBar:     "#606675"
    readonly property color rulerTick:        "#3e424e"
    readonly property color rulerLabelMinor:  "#7c8393"
    readonly property color gridBar:          "#4c515e"
    readonly property color gridBeat:         "#363944"
    readonly property color gridSub:          "#2b2d34"
    readonly property color rowWhiteKey:      "#26282f"
    readonly property color rowBlackKey:      "#1d1f24"
    readonly property color rowBorder:        "#202227"
    readonly property color rowOctaveBorder:  "#424652"
    readonly property color clipInside:       "#0a283020" // faint tint over the clip's length
    readonly property color clipOutside:      "#40000000" // darkening after the clip ends
    readonly property color selectionBox:     "#2060cdff" // rubber band fill (border: Theme.accent)

    // ---- notes (Velocity colour mode; Track mode derives from the track colour) ----
    readonly property color noteSoft:         "#24405e"   // velocity 0
    readonly property color noteLoud:         "#38c2b4"   // velocity 127
    readonly property color noteOutline:      "#5bc0de"
    readonly property color noteHighlight:    "#a4e2f5"   // hover outline, edge stripe, draw preview
    readonly property color noteSelected:     "#ffffff"   // selected outline
    readonly property color noteSelectedEdge: "#e6fffa"
    readonly property color noteName:         "#f0f8ff"

    // ---- scroll bars ----
    readonly property color scrollHandle:        "#424652"
    readonly property color scrollHandleHover:   "#565b67"
    readonly property color scrollHandlePressed: "#6b7280"
    readonly property color scrollTrack:         "#1a1c22"

    // ---- velocity lane ----
    readonly property color laneSplitter:       "#2f323a"
    readonly property color laneSplitterActive: "#4a4f5b"
    readonly property color laneSplitterBorder: "#1e2026"
    readonly property color laneScaleText:      "#6b7280"  // 127 / 64 / 0 marks
    readonly property color laneValueText:      "#c2c7d0"
    readonly property color laneGridBar:        "#3b3f4a"
    readonly property color laneGrid:           "#2f333c"
    readonly property color stalkFallback:      "#3883a8"  // when no note colour is available
    readonly property color stalkBorder:        "#1e2229"
    readonly property color stalkSelected:      "#ffffff"

    // ---- piano keys ----
    readonly property color whiteKey:          "#f2f4f8"
    readonly property color whiteKeyHover:     "#ffffff"
    readonly property color whiteKeyPressed:   "#bcc5d3"
    readonly property color whiteKeyActive:    "#dbe0ea"
    readonly property color whiteKeyBorder:    "#8a909d"
    readonly property color whiteKeyLabel:     "#555b68"
    readonly property color whiteKeyLabelC:    "#111317"
    readonly property color blackKey:          "#0f1013"
    readonly property color blackKeyHover:     "#2c2f38"
    readonly property color blackKeyPressed:   "#484d59"
    readonly property color blackKeyActive:    "#42454f"
    readonly property color blackKeyBorder:    "#050608"
    readonly property color blackKeyLabel:     "#a6acb8"
    readonly property color blackKeyShine:     "#2a2d36"
    readonly property color blackKeyShineActive: "#636875"
}
