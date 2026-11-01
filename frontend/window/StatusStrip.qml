import QtQuick

// The strip along the bottom. It reads EngineController, which proves QML, wrapper and backend are connected.
Rectangle {
    height: Theme.stripHeight
    color: Theme.panelBg

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.margin
        text: EngineController.transportMessage.length ? EngineController.transportMessage
              : "Engine " + EngineController.version + "  ·  " + EngineController.status
        anchors.right: parent.right
        anchors.rightMargin: Theme.margin
        elide: Text.ElideMiddle
        color: Theme.textMuted
        font.pixelSize: Theme.bodySize
    }
}
