import QtQuick

// Preferences: the audio and MIDI device boxes. Use it through the Theme singleton:
// Theme.settings.deviceBox, ...
QtObject {
    readonly property color deviceBox:          "#292b30"
    readonly property color deviceBoxDisabled:  "#222328"
    readonly property color deviceBoxBorder:    "#444750"   // accent when focused
    readonly property color deviceTextDisabled: "#777b83"
}
