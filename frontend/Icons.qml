pragma Singleton
import QtQuick

// Every icon of the app, by name. These are plain unicode for now, so no font file is needed.
// Use it anywhere like this: Text { text: Icons.play }
// Later you can load an icon font here, as ChaoticFiles does, and no other file has to change.
QtObject {
    readonly property string play:   "▶"
    readonly property string stop:   "■"
    readonly property string record: "●"
}
