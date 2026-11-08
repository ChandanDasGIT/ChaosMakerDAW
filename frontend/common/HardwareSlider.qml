import QtQuick
import QtQuick.Controls

// A slider drawn like a mixing desk fader: a dark slot with a lit bevel, scale ticks beside it
// (longer at the ends and the middle) and a shaded metal cap lying across the slot, with a
// white indicator line across its middle that turns accent while dragged. Under the mouse the
// cap brightens a little. Used on the FX pages, the master fader and the metronome volume; a
// track's own sliders (VOL, CLIP) are ThemedSlider in the track colour.
// The cap's layout box stays 13 px (Qt's own handle), so swapping a plain Slider for this one
// never changes a layout's size; the cap simply overhangs it.
Slider {
    id: control
    property real slotThickness: 4
    property int tickCount: 11

    hoverEnabled: true

    background: Item {
        x: control.leftPadding
        y: control.topPadding
        implicitWidth: control.horizontal ? 160 : control.slotThickness
        implicitHeight: control.horizontal ? control.slotThickness : 160
        width: control.availableWidth
        height: control.availableHeight

        // Scale ticks on both sides of the slot, along the cap's travel.
        Repeater {
            model: control.tickCount * 2
            Rectangle {
                readonly property int step: index % control.tickCount
                readonly property bool major: step === 0 || step === control.tickCount - 1
                                              || step * 2 === control.tickCount - 1
                readonly property bool before: index < control.tickCount
                // Travel of the cap's centre: the handle box's centre from end to end.
                readonly property real along: 6.5 + step / (control.tickCount - 1)
                                              * ((control.horizontal ? parent.width : parent.height) - 13)
                readonly property real across: control.horizontal ? parent.height : parent.width
                readonly property real reach: Math.max(1, Math.min(major ? 4 : 2.5, across / 2 - control.slotThickness / 2 - 2))
                readonly property real gap: control.slotThickness / 2 + 2
                color: Theme.faderTick
                width: control.horizontal ? 1 : reach
                height: control.horizontal ? reach : 1
                x: control.horizontal ? Math.round(along)
                                      : (before ? across / 2 - gap - reach : across / 2 + gap)
                y: control.horizontal ? (before ? across / 2 - gap - reach : across / 2 + gap)
                                      : Math.round(along)
            }
        }
        // The lit bevel: one pixel below / right of the slot.
        Rectangle {
            x: control.horizontal ? 0 : (parent.width - width) / 2 + 1
            y: control.horizontal ? (parent.height - height) / 2 + 1 : 0
            width: control.horizontal ? parent.width : control.slotThickness
            height: control.horizontal ? control.slotThickness : parent.height
            radius: control.slotThickness / 2
            color: Theme.faderSlotEdge
        }
        Rectangle {
            objectName: "faderSlot"
            x: control.horizontal ? 0 : (parent.width - width) / 2
            y: control.horizontal ? (parent.height - height) / 2 : 0
            width: control.horizontal ? parent.width : control.slotThickness
            height: control.horizontal ? control.slotThickness : parent.height
            radius: control.slotThickness / 2
            color: Theme.faderSlot
        }
    }

    handle: Item {
        x: control.leftPadding + Math.round(control.horizontal ? control.visualPosition * (control.availableWidth - width)
                                                              : (control.availableWidth - width) / 2)
        y: control.topPadding + Math.round(control.horizontal ? (control.availableHeight - height) / 2
                                                              : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 13
        implicitHeight: 13
        opacity: control.enabled ? 1 : 0.4

        // Upright on a horizontal slider, lying flat across a vertical fader.
        readonly property real capLength: control.horizontal ? 10 : 24
        readonly property real capDepth: control.horizontal ? 20 : 12

        Rectangle {   // soft shadow under the cap
            anchors.centerIn: cap
            anchors.horizontalCenterOffset: 1
            anchors.verticalCenterOffset: 2
            width: cap.width + 2; height: cap.height + 2
            radius: cap.radius + 1
            color: Theme.faderCapShadow
        }
        Rectangle {
            id: cap
            objectName: "faderCap"
            anchors.centerIn: parent
            width: control.horizontal ? parent.capLength : parent.capDepth * 2
            height: control.horizontal ? parent.capDepth : parent.capLength / 2
            radius: 2
            border.color: Theme.faderCapEdge
            border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.faderCapTop }
                GradientStop { position: 0.55; color: Qt.darker(Theme.faderCapTop, 1.15) }
                GradientStop { position: 1.0; color: Theme.faderCapBottom }
            }
            // Brighter under the mouse.
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: parent.radius
                color: Theme.textStrong
                opacity: control.hovered || control.pressed ? 0.18 : 0
                Behavior on opacity { NumberAnimation { duration: 110 } }
            }
            // Grip grooves either side of the indicator line.
            Repeater {
                model: 2
                Rectangle {
                    color: Theme.faderCapBottom
                    opacity: 0.8
                    width: control.horizontal ? 1 : cap.width - 6
                    height: control.horizontal ? cap.height - 6 : 1
                    x: control.horizontal ? cap.width / 2 + (index ? 2.5 : -3.5) : 3
                    y: control.horizontal ? 3 : cap.height / 2 + (index ? 2.5 : -3.5)
                }
            }
            Rectangle {
                objectName: "faderLine"
                color: control.pressed ? Theme.accent : Theme.faderCapLine
                width: control.horizontal ? 2 : cap.width - 2
                height: control.horizontal ? cap.height - 2 : 2
                anchors.centerIn: parent
                Behavior on color { ColorAnimation { duration: 110 } }
            }
        }
    }
}
