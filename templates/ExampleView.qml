import QtQuick

// One line: what this view shows. It draws state and reports clicks. Colours and sizes come from Theme.
Rectangle {
    implicitWidth: 200
    implicitHeight: Theme.stripHeight
    color: Theme.panelBg

    Text {
        anchors.centerIn: parent
        text: ExampleController.value
        color: Theme.text
        font.pixelSize: Theme.bodySize
    }

    MouseArea {
        anchors.fill: parent
        onClicked: ExampleController.reset()
    }
}
