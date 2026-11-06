import QtQuick

// The transport bar's own colours: its background, the transport buttons and the BPM and
// time signature fields. Use it through the Theme singleton: Theme.transport.button, ...
QtObject {
    readonly property color bar:              "#242424"

    // ---- transport buttons (lit: Theme.accentBg / Theme.recordActiveBg) ----
    readonly property color button:           "#2b2d32"
    readonly property color buttonHover:      "#363941"
    readonly property color buttonPressed:    "#454851"
    readonly property color buttonBorder:     "#41444c"
    readonly property color buttonBorderHover:"#5a5e68"

    // ---- BPM and time signature fields ----
    readonly property color field:            "#191a1e"
    readonly property color fieldBorder:      "#3a3d45"
    readonly property color fieldFocus:       "#282b32"   // a focused number inside the field
    readonly property color stepHover:        "#2d3038"   // BPM - / + buttons
    readonly property color stepPressed:      "#414650"
    readonly property color separatorText:    "#7e838d"   // the "/" in the time signature
}
