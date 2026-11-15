import QtQuick
import QtQuick.Controls

// The app's slider: a rounded track filled up to the value (white, accent while under the mouse
// or dragged) and a handle that grows a little under the mouse and more while dragged.
// Horizontal sliders get a round handle; vertical faders a pill lying across the track.
// The handle's layout box stays 13 px (Qt's own), so swapping a plain Slider for this one
// never changes a layout's size.
Slider {
    id: control
    readonly property bool lit: enabled && (hovered || pressed)
    // Size of the drawn handle now: 0 at rest, 1 under the mouse, 2 while dragged.
    readonly property int handleState: pressed ? 2 : hovered ? 1 : 0
    property real trackThickness: 6
    // A track's own slider (VOL, CLIP) passes its track colour: the fill and the handle's ring
    // take it (a little brighter under the mouse). Transparent = the plain white / accent look.
    property color tint: "transparent"
    readonly property bool tinted: tint.a > 0
    readonly property color litTint: Qt.lighter(tint, 1.3)

    hoverEnabled: true

    background: Item {
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
        implicitWidth: control.horizontal ? 160 : control.trackThickness
        implicitHeight: control.horizontal ? control.trackThickness : 160
        width: control.horizontal ? control.availableWidth : implicitWidth
        height: control.horizontal ? implicitHeight : control.availableHeight

        Rectangle {
            anchors.fill: parent
            radius: control.trackThickness / 2
            color: Theme.controlBorder
        }
        // The filled part: from the left, or up from the bottom on a fader.
        Rectangle {
            objectName: "sliderFill"
            x: 0
            y: control.horizontal ? 0 : control.visualPosition * parent.height
            width: control.horizontal ? control.visualPosition * parent.width : parent.width
            height: control.horizontal ? parent.height : (1 - control.visualPosition) * parent.height
            radius: control.trackThickness / 2
            color: !control.enabled ? Theme.textMuted
                 : control.tinted ? (control.lit ? control.litTint : control.tint)
                 : control.lit ? Theme.accent : Theme.text
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    handle: Item {
        x: control.leftPadding + Math.round(control.horizontal ? control.visualPosition * (control.availableWidth - width)
                                                              : (control.availableWidth - width) / 2)
        y: control.topPadding + Math.round(control.horizontal ? (control.availableHeight - height) / 2
                                                              : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 13
        implicitHeight: 13
        visible: control.enabled

        Rectangle {
            objectName: "sliderKnob"
            anchors.centerIn: parent
            // Round: 10 / 14 / 16. Pill across a fader: 18x8 / 20x9 / 22x10.
            width: control.horizontal ? [10, 14, 16][control.handleState] : [18, 20, 22][control.handleState]
            height: control.horizontal ? width : [8, 9, 10][control.handleState]
            radius: height / 2
            color: control.pressed ? Theme.textStrong : Theme.text
            border.color: control.visualFocus ? Theme.accent
                        : control.tinted ? (control.lit ? control.litTint : control.tint) : "transparent"
            border.width: control.visualFocus || control.tinted ? 2 : 0
            Behavior on width { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
            Behavior on height { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        }
    }
}
