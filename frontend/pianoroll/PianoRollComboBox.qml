import QtQuick
import QtQuick.Controls
import ".."

// The shared drop-down in the piano roll's compact size and blue-grey palette.
ThemedComboBox {
    face: Theme.pianoRoll.combo
    faceHover: Theme.pianoRoll.comboHover
    facePressed: Theme.pianoRoll.comboPressed
    border: Theme.pianoRoll.panelBorder
    textColour: Theme.pianoRoll.text
    arrowColour: Theme.pianoRoll.textMuted
    listBg: Theme.pianoRoll.comboPopup
    listBorder: Theme.pianoRoll.readoutBorder
    itemHighlight: Theme.pianoRoll.comboHover
    cornerRadius: 2
    itemHeight: 22
    implicitHeight: 22
    font.pixelSize: Theme.smallSize
}
