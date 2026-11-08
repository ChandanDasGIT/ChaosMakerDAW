import QtQuick
import QtQuick.Controls

// The cross (Icons.close) that closes a panel or removes something. At rest a muted cross;
// under the mouse a soft round backdrop and a white cross; pressed, the backdrop darkens a
// little. `destructive` (remove a track, an effect, a pedal) shows the backdrop in the record
// red instead, as a warning.
ToolButton {
    id: button
    property bool destructive: false
    property string tooltip: ""
    property int glyphSize: 16
    // The round backdrop's colour now (tests read it).
    readonly property color backdrop: !(hovered || down) ? "transparent"
                                    : destructive ? (down ? Theme.record : Theme.recordBg)
                                    : (down ? Theme.pressedOverlay : Theme.hoverOverlay)

    text: Icons.close
    hoverEnabled: true
    padding: 0
    implicitWidth: 24
    implicitHeight: 24

    contentItem: Text {
        text: Icons.close
        color: button.hovered || button.down ? Theme.textStrong : Theme.textMuted
        font.pixelSize: button.glyphSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Item {
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height)
            height: width
            radius: width / 2
            color: button.backdrop
            Behavior on color { ColorAnimation { duration: 90 } }
        }
    }
    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.text: tooltip
    ToolTip.delay: 450
}
