import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common"

// Tap tempo (restMap G17), opened from Options > Tap tempo…: a big pad to click in time (or
// Space / T while open), the BPM it gives, and a way back to the BPM from before. The taps
// themselves are counted by the transport bar's tempo field; this popup asks for them.
Popup {
    id: root
    property real bpm: 120                 // the project's BPM now
    property int tapCount: 0               // taps in the current count
    signal tapRequested()
    signal bpmEdited(real value)           // "Back to …"

    property real startBpm: 120            // the BPM when the popup opened
    property bool paused: true             // no tap for over 2 s: the next one starts a new count

    function tap() {
        root.paused = false
        pauseTimer.restart()
        flash.restart()
        root.tapRequested()
    }

    objectName: "tapTempoDialog"
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 320
    padding: 18
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: { root.startBpm = root.bpm; root.paused = true }
    onOpened: keys.forceActiveFocus()
    background: Rectangle { radius: 10; color: Theme.popupBg; border.color: Theme.popupBorder }

    Timer { id: pauseTimer; interval: 2000; onTriggered: root.paused = true }

    contentItem: Item {
        id: keys
        implicitHeight: column.implicitHeight
        focus: true
        // Space and T tap here: taken before the window's shortcuts (Space would play).
        Keys.onShortcutOverride: event => {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_T) event.accepted = true
        }
        Keys.onPressed: event => {
            if ((event.key === Qt.Key_Space || event.key === Qt.Key_T) && !event.isAutoRepeat) {
                root.tap()
                event.accepted = true
            }
        }

        ColumnLayout {
            id: column
            width: parent.width
            spacing: 12

            Text {
                text: "Tap tempo"
                color: Theme.text; font.pixelSize: 16; font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "Click the pad in time with the music, or press Space or T."
                color: Theme.textMuted; font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Rectangle {
                id: pad
                objectName: "tapTempoPad"
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                radius: 10
                color: Theme.cardBgAlt
                border.color: padArea.containsMouse ? Theme.accent : Theme.divider
                border.width: 1
                Rectangle {                                   // the flash on each tap
                    id: flashLayer
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.accent
                    opacity: 0
                }
                SequentialAnimation {
                    id: flash
                    NumberAnimation { target: flashLayer; property: "opacity"; to: 0.45; duration: 30 }
                    NumberAnimation { target: flashLayer; property: "opacity"; to: 0; duration: 220 }
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 2
                    Text {
                        objectName: "tapTempoBpm"
                        Layout.alignment: Qt.AlignHCenter
                        text: Math.round(root.bpm)
                        color: Theme.text; font.pixelSize: 44; font.bold: true
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "BPM"
                        color: Theme.textMuted; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
                    }
                    Text {
                        objectName: "tapTempoStatus"
                        Layout.alignment: Qt.AlignHCenter
                        text: root.paused ? (root.tapCount > 1 ? "Tap to start a new count" : "Tap to start")
                            : root.tapCount <= 1 ? "Keep tapping…"
                            : root.tapCount + " taps"
                        color: root.paused ? Theme.textMuted : Theme.accent
                        font.pixelSize: 12
                    }
                }
                MouseArea {
                    id: padArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: root.tap()                    // on press, for the timing
                }
                Accessible.role: Accessible.Button
                Accessible.name: "Tap"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                PanelButton {
                    objectName: "tapTempoRestore"
                    text: "Back to " + Math.round(root.startBpm)
                    enabled: Math.abs(root.bpm - root.startBpm) > 1e-6
                    onClicked: { root.bpmEdited(root.startBpm); root.paused = true; keys.forceActiveFocus() }
                }
                Item { Layout.fillWidth: true }
                PanelButton {
                    objectName: "tapTempoClose"
                    text: "Close"
                    onClicked: root.close()
                }
            }
        }
    }
}
