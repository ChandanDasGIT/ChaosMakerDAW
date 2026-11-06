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
}
