pragma Singleton
import QtQuick

// Every icon of the app, by name. Use it anywhere like this: Text { text: Icons.play }
// Text icons are written as code points ("\u25B6"), never as literal symbols, and no other QML
// file may contain an icon symbol (a UI test checks this). They are plain Unicode drawn by the
// system fonts for now; switching to an icon font later only changes the values below.
//
// Drawn icons (shapes, not text) live next to this file in frontend/icons/:
//   MetronomeIcon.qml   metronome (transport bar, Metronome & Snap button)
QtObject {
    // ---- transport ----
    readonly property string goToStart:   "|\u25C0"      // |◀
    readonly property string skipForward: "\u25B6|"      // ▶|
    readonly property string record:      "\u25CF"       // ●
    readonly property string play:        "\u25B6"       // ▶
    readonly property string pause:       "\u2161"       // Ⅱ
    readonly property string stop:        "\u25A0"       // ■
    readonly property string loop:        "\u27F3"       // ⟳
    readonly property string punch:       "\u21E5\u2022\u21E4" // ⇥•⇤ punch in / out (restMap G11)

    // ---- menus and common buttons ----
    readonly property string menuArrow:   "\u25BE"       // ▾ opens a menu or list
    readonly property string submenu:     "\u203A"       // › menu item with a submenu
    readonly property string back:        "\u2039"       // ‹ back to the previous page
    readonly property string check:       "\u2713"       // ✓ ticked menu item
    readonly property string close:       "\u00D7"       // × close or remove
    readonly property string dragHandle:  "\u283F"       // ⠿ drag to reorder
    readonly property string plus:        "\u002B"       // + add, increase, zoom in
    readonly property string minus:       "\u2212"       // − decrease, zoom out

    // ---- piano roll tools ----
    readonly property string pointer:     "\u2316"       // ⌖ select
    readonly property string pencil:      "\u270E"       // ✎ draw
    readonly property string eraser:      "\u232B"       // ⌫ erase
    readonly property string snap:        "\uD83E\uDDF2" // magnet (U+1F9F2, written as a surrogate pair)
    readonly property string quantize:    "\u21E5"       // ⇥ (shown as "⇥Q")
    readonly property string undo:        "\u21B6"       // ↶
    readonly property string redo:        "\u21B7"       // ↷

    // ---- file chooser (frontend/dialogs/FilePicker.qml) ----
    readonly property string folder:      "\uD83D\uDCC1" // folder (U+1F4C1)
    readonly property string folderOpen:  "\uD83D\uDCC2" // open folder (U+1F4C2), the chooser's title
    readonly property string file:        "\uD83D\uDCC4" // page (U+1F4C4), a file that is not audio
    readonly property string audioFile:   "\u266B"       // ♫ an audio file
    readonly property string folderUp:    "\u2191"       // ↑ up one folder
    readonly property string home:        "\u2302"       // ⌂ home folder
    readonly property string music:       "\u266A"       // ♪ Music folder
    readonly property string download:    "\u2913"       // ⤓ Downloads folder
    readonly property string project:     "\u25C8"       // ◈ the open project's folder
    readonly property string drive:       "\uD83D\uDCBE" // disk (U+1F4BE), a mounted drive
}
