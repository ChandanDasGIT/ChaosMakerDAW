import QtQuick
import QtQuick.Controls

// An on / off switch in the app's style: a dark pill with a grey knob, lit in the accent
// with the knob at the right when on. An optional label follows it.
Switch {
    id: root
    property int textSize: Theme.bodySize
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    spacing: 8
    indicator: Rectangle {
        objectName: "switchIndicator"
        x: root.leftPadding
        y: root.topPadding + Math.round((root.availableHeight - height) / 2)
        implicitWidth: 34; implicitHeight: 18
        radius: height / 2
        color: root.checked ? Theme.accentBg : Theme.field
        border.color: root.checked || root.hovered || root.visualFocus ? Theme.accent : Theme.fieldBorder
        Rectangle {
            x: root.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 12; height: 12; radius: width / 2
            color: root.checked ? Theme.accent : Theme.textMuted
            Behavior on x { NumberAnimation { duration: 90 } }
        }
    }
    contentItem: Text {
        text: root.text
        color: Theme.text
        font.pixelSize: root.textSize
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
