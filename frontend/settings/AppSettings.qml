import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../audiodevice"

ToolButton {
    id: root
    objectName: "settingsButton"
    text: "Settings"
    implicitHeight: 32
    checkable: true
    checked: settingsMenu.visible
    onClicked: settingsMenu.visible ? settingsMenu.close() : settingsMenu.open()

    readonly property alias pianoRollPresentation: appSettingsBridge.pianoRollPresentation
    // The app's one AppSettingsWrapper, for settings the window uses directly (layout sizes).
    readonly property alias bridge: appSettingsBridge

    AppSettingsWrapper {
        id: appSettingsBridge
        objectName: "appSettingsBridge"
    }

    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "Settings"
            color: root.down || root.checked ? Theme.accent : Theme.text
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            text: Icons.menuArrow
            color: root.down || root.checked ? Theme.accent : Theme.textMuted
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: 4
        color: root.down || root.checked ? Theme.controlBgActive : root.hovered ? Theme.controlBgHover : "transparent"
        border.color: root.checked ? Theme.divider : "transparent"
    }

    ThemedMenu {
        id: settingsMenu
        y: parent.height + 4
        width: 220

        ThemedMenuItem {
            id: restoreOption
            objectName: "restoreSessionSetting"
            text: "Restore session on startup"
            checkable: true
            // Owned by EngineController, which applies it at startup and when saving the session
            checked: EngineController.restoreSessionOnStartup
            onTriggered: EngineController.restoreSessionOnStartup = checked
            // Keep the tick in step with changes made elsewhere (clicking breaks a plain binding)
            Binding on checked { value: EngineController.restoreSessionOnStartup }
        }

        ThemedMenuItem {
            objectName: "stopAtEndSetting"
            text: "Stop playback at the end"
            checkable: true
            // Owned by EngineController; when off, playback and the metronome run until Stop/Pause.
            checked: EngineController.stopPlaybackAtEnd
            onTriggered: EngineController.stopPlaybackAtEnd = checked
            Binding on checked { value: EngineController.stopPlaybackAtEnd }
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            id: preferencesOption
            objectName: "preferencesSetting"
            text: "Preferences…"
            onTriggered: {
                settingsMenu.close()
                preferencesPopup.open()
            }
        }
    }

    Popup {
        id: preferencesPopup
        objectName: "preferencesPopup"
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 440
        padding: 16
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.panelBg
            border.color: Theme.divider
            border.width: 1
            radius: 8
        }
        contentItem: ColumnLayout {
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "Preferences"
                    color: Theme.text
                    font.pixelSize: Theme.bodySize + 2
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                CloseButton {
                    Accessible.name: "Close preferences"
                    implicitWidth: 28
                    implicitHeight: 28
                    glyphSize: 18
                    onClicked: preferencesPopup.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.divider
            }

            Text {
                text: "Audio & MIDI"
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.bold: true
            }

            DeviceSettings {
                id: deviceSettings
                Layout.fillWidth: true
            }

            Text {
                text: "Recording"
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.bold: true
            }

            ColumnLayout {
                spacing: 8
                Text {
                    text: "New tracks start in this mode; switch a track in its FX panel."
                    color: Theme.textMuted
                    font.pixelSize: Theme.smallSize
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                RadioButton {
                    id: recordDryRadio
                    objectName: "defaultRecordModeDry"
                    text: "Dry · editable FX"
                    checked: appSettingsBridge.defaultRecordMode === "dry"
                    onToggled: if (checked) appSettingsBridge.defaultRecordMode = "dry"
                    contentItem: Text {
                        text: recordDryRadio.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: recordDryRadio.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    id: recordPrintRadio
                    objectName: "defaultRecordModePrint"
                    text: "Print FX"
                    checked: appSettingsBridge.defaultRecordMode === "print"
                    onToggled: if (checked) appSettingsBridge.defaultRecordMode = "print"
                    contentItem: Text {
                        text: recordPrintRadio.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: recordPrintRadio.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Text {
                text: "Piano Roll Display"
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.bold: true
            }

            ColumnLayout {
                spacing: 8
                RadioButton {
                    id: dockRadio
                    text: "Bottom Docked Panel"
                    checked: appSettingsBridge.pianoRollPresentation === "dock"
                    onToggled: if (checked) appSettingsBridge.pianoRollPresentation = "dock"
                    contentItem: Text {
                        text: dockRadio.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: dockRadio.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    id: windowRadio
                    text: "Floating Window"
                    checked: appSettingsBridge.pianoRollPresentation === "window"
                    onToggled: if (checked) appSettingsBridge.pianoRollPresentation = "window"
                    contentItem: Text {
                        text: windowRadio.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: windowRadio.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
}
