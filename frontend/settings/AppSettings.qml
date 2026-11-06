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
            text: "▾"
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

    Menu {
        id: settingsMenu
        y: parent.height + 4
        width: 220
        padding: 4
        background: Rectangle {
            color: Theme.panelBg
            border.color: Theme.divider
            radius: 6
        }

        MenuItem {
            id: restoreOption
            objectName: "restoreSessionSetting"
            text: "Restore session on startup"
            checkable: true
            // Owned by EngineController, which applies it at startup and when saving the session
            checked: EngineController.restoreSessionOnStartup
            implicitHeight: 30
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text {
                text: restoreOption.text
                color: restoreOption.highlighted ? Theme.accent : Theme.text
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
                leftPadding: restoreOption.indicator ? restoreOption.indicator.width + 4 : 0
            }
            background: Rectangle {
                color: restoreOption.highlighted ? Theme.tooltipBg : "transparent"
                radius: 4
            }
            onTriggered: EngineController.restoreSessionOnStartup = checked
            // Keep the tick in step with changes made elsewhere (clicking breaks a plain binding)
            Binding on checked { value: EngineController.restoreSessionOnStartup }
        }

        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.divider
            }
        }

        MenuItem {
            id: preferencesOption
            objectName: "preferencesSetting"
            text: "Preferences…"
            implicitHeight: 30
            leftPadding: Theme.margin
            rightPadding: Theme.margin
            contentItem: Text {
                text: preferencesOption.text
                color: preferencesOption.highlighted ? Theme.accent : Theme.text
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: preferencesOption.highlighted ? Theme.tooltipBg : "transparent"
                radius: 4
            }
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
                Button {
                    text: "✕"
                    flat: true
                    implicitWidth: 28
                    implicitHeight: 28
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
