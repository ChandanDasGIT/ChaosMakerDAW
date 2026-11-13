import QtQuick
import QtQuick.Controls

// A tool from the Tools menu as a window of its own (restMap U7), like the FX panel and the
// on-screen piano: it floats over the app, the user moves it where they want it (onto another
// screen too), and the app behind stays usable. It is as wide as `width` says and as tall as
// what is put in it. open() shows it (centred on the app the first time) and gives it the
// keyboard; close(), the window's own close or Escape hide it, and the app's window takes the
// keyboard back. While `busy` (an analysis is running) it does not close.
Window {
    id: root
    default property alias content: body.data
    property bool busy: false
    property int padding: Theme.spaceLg
    signal aboutToShow()                                    // before each showing
    signal opened()                                         // after it, with the keyboard here
    signal closed()

    property bool placed: false                             // centred once; then where the user left it

    function open() {
        if (!visible) aboutToShow()
        if (!placed) {
            const host = transientParent
            x = host ? host.x + (host.width - width) / 2 : (Screen.desktopAvailableWidth - width) / 2
            y = host ? host.y + (host.height - height) / 2 : (Screen.desktopAvailableHeight - height) / 2
            placed = true
        }
        show()
        raise()
        requestActivate()
        opened()
    }
    onClosing: close => { if (root.busy) close.accepted = false }
    onVisibleChanged: {
        if (visible) return
        // The window it floats over takes the keyboard back (shortcuts, KEYS, the transport).
        if (transientParent) transientParent.requestActivate()
        closed()
    }

    flags: Qt.Tool | Qt.WindowStaysOnTopHint
    // As tall as what is in it, and not resizable: the height follows the content.
    readonly property int fitHeight: Math.ceil(body.childrenRect.height) + 2 * padding
    width: 560
    height: fitHeight
    minimumWidth: width
    maximumWidth: width
    minimumHeight: fitHeight
    maximumHeight: fitHeight
    color: Theme.popupBg
    // A window of its own does not inherit the main window's palette: without these the
    // tooltips fall back to the system's black on yellow.
    palette.toolTipBase: Theme.tooltipBg
    palette.toolTipText: Theme.tooltipText

    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: !root.busy
        onActivated: root.close()
    }

    Item {
        id: body
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
