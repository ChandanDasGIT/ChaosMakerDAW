import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The strip along the bottom: the engine message on the left, the audio device readouts
// (sample rate, buffer/latency, xruns) on the right. All text comes from StatusStripWrap.
Rectangle {
    id: root
    height: Theme.stripHeight
    color: Theme.panelBg

    component Readout: Text {
        property string tip
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        font.family: Theme.monospaceFontFamily
        Layout.alignment: Qt.AlignVCenter
        HoverHandler { id: readoutHover }
        ToolTip.visible: readoutHover.hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 450
    }
    component Separator: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 12
        Layout.alignment: Qt.AlignVCenter
        color: Theme.divider
    }

    StatusStripWrap {
        id: strip
        objectName: "statusStripWrap"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.margin
        anchors.rightMargin: Theme.margin
        spacing: 10

        Text {
            objectName: "statusMessage"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            // One line: a message with line breaks (a device error) would run out of the strip.
            text: strip.message
            maximumLineCount: 1
            elide: Text.ElideMiddle
            color: Theme.textMuted
            font.pixelSize: Theme.bodySize
        }
        Readout {
            objectName: "statusSampleRate"
            text: strip.sampleRateText
            tip: "Sample rate"
        }
        Separator {}
        Readout {
            objectName: "statusBuffer"
            text: strip.bufferText
            tip: strip.bufferTooltip
        }
        Separator {}
        Readout {
            id: xrunReadout
            objectName: "statusXruns"
            text: strip.xrunText
            tip: strip.xrunTooltip
            // Orange once a dropout happened; click to start counting again.
            color: strip.xrunCount > 0 ? Theme.meterWarning : Theme.textMuted
            font.bold: strip.xrunCount > 0
            TapHandler {
                enabled: strip.xrunCount > 0
                onTapped: strip.resetXruns()
            }
        }
    }
}
