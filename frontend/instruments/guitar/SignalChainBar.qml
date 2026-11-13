import QtQuick
import QtQuick.Controls

// The whole guitar chain on one line, always in view: IN, a chip per pedal, OUT (restMap U8 H4).
// Click a chip to bring that pedal into view on the board; drag a chip to move the pedal in
// the chain (the other chips slide aside, one move on release). While a pedal is dragged on
// the board (boardFrom / boardTo), the chips show the same preview.
Item {
    id: root
    property var effectsModel
    property int boardFrom: -1
    property int boardTo: -1
    signal pedalClicked(int index)

    readonly property int count: chips.count
    readonly property real jackWidth: 34
    readonly property real arrowWidth: 16
    readonly property real chipWidth: count === 0 ? 0
        : Math.max(44, Math.min(108, (width - 2 * jackWidth - (count + 1) * arrowWidth) / count))
    readonly property real chipStep: chipWidth + arrowWidth
    function chipX(slot) { return jackWidth + arrowWidth + slot * chipStep }

    // A chip being dragged here, else the board's drag, else nothing.
    property int dragFrom: -1
    property int dragTo: -1
    readonly property int previewFrom: dragFrom >= 0 ? dragFrom : boardFrom
    readonly property int previewTo: dragFrom >= 0 ? dragTo : boardTo
    function visualSlot(index) {
        const from = previewFrom, to = previewTo
        if (from < 0 || to < 0) return index
        if (index === from) return to
        if (from < to && index > from && index <= to) return index - 1
        if (to < from && index >= to && index < from) return index + 1
        return index
    }
    function slotAt(x) { return Math.max(0, Math.min(count - 1, Math.round((x - chipX(0)) / chipStep))) }

    implicitHeight: 30

    component Jack: Rectangle {
        property string label
        width: root.jackWidth; height: 22
        anchors.verticalCenter: parent.verticalCenter
        radius: Theme.controlRadius
        color: Theme.guitarSuite.jack
        border.color: Theme.guitarSuite.cable
        Text { anchors.centerIn: parent; text: parent.label; color: Theme.text; font.pixelSize: Theme.microSize; font.bold: true }
    }
    component Arrow: Text {
        width: root.arrowWidth
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignHCenter
        text: Icons.chainArrow
        color: Theme.guitarSuite.cable
        font.pixelSize: Theme.smallSize
    }

    Jack { objectName: "guitarChainBarIn"; label: "IN"; x: 0 }
    // An arrow before each slot and one before OUT; they stay put while the chips slide.
    Repeater {
        model: root.count + 1
        delegate: Arrow { required property int index; x: root.chipX(index) - root.arrowWidth }
    }
    Jack { objectName: "guitarChainBarOut"; label: "OUT"; x: root.chipX(root.count) }

    Repeater {
        id: chips
        model: root.effectsModel
        delegate: Rectangle {
            id: chip
            required property int index
            required property string pedalName
            required property string pedalType
            required property bool isBypassed
            objectName: "guitarChainChip" + index
            readonly property bool dragging: chipMouse.drag.active
            x: root.chipX(root.visualSlot(index))
            Behavior on x {
                enabled: !chip.dragging
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }
            z: dragging ? 10 : 1
            width: root.chipWidth
            height: 22
            anchors.verticalCenter: parent.verticalCenter
            radius: Theme.controlRadius
            color: chipMouse.containsMouse || dragging ? Theme.chipHover : Theme.chip
            border.color: dragging ? Theme.accent : isBypassed ? "transparent" : Theme.chipBorder
            opacity: isBypassed && !dragging ? 0.5 : 1
            // Amp and cabinet: their colour on the chip's left edge, as on the board.
            Rectangle {
                visible: chip.pedalType === "pedal.amp" || chip.pedalType === "pedal.cabinet"
                x: 1; y: 3; width: 3; height: parent.height - 6; radius: 1
                color: chip.pedalType === "pedal.amp" ? Theme.guitarSuite.ampBand : Theme.guitarSuite.cabinetBand
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: 7; anchors.rightMargin: 5
                text: chip.pedalName
                color: chip.isBypassed ? Theme.textMuted : Theme.text
                font.pixelSize: Theme.captionSize
                font.strikeout: chip.isBypassed
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea {
                id: chipMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                drag.target: chip
                drag.axis: Drag.XAxis
                drag.minimumX: root.chipX(0) - root.arrowWidth
                drag.maximumX: root.chipX(Math.max(0, root.count - 1)) + root.arrowWidth
                drag.threshold: 4
                onPressed: { root.dragFrom = chip.index; root.dragTo = chip.index }
                onPositionChanged: if (drag.active) root.dragTo = root.slotAt(chip.x)
                // After the drag has ended, so the card glides (its animation is off while dragging).
                onReleased: Qt.callLater(chip.finishDrag)
                onCanceled: Qt.callLater(chip.finishDrag)
                // A click without a drag: show the pedal on the board.
                onClicked: root.pedalClicked(chip.index)
                ToolTip.visible: containsMouse && !pressed
                ToolTip.text: chip.pedalName + (chip.isBypassed ? " (bypassed)" : "") + " · click to show it, drag to move it"
                ToolTip.delay: 450
            }
            function finishDrag() {
                const from = root.dragFrom, to = root.dragTo
                root.dragFrom = -1; root.dragTo = -1
                chip.x = Qt.binding(function() { return root.chipX(root.visualSlot(chip.index)) })
                if (from >= 0 && to >= 0 && from !== to) root.effectsModel.movePedal(from, to)
            }
        }
    }
}
