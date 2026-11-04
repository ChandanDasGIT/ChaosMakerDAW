import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ToolButton {
    id: root
    objectName: "settingsButton"
    text: "Settings"
    implicitHeight: 32
    checkable: true
    checked: settingsMenu.visible
    onClicked: settingsMenu.visible ? settingsMenu.close() : settingsMenu.open()

    AppSettingsWrapper {
        id: appSettingsBridge
        onRestoreSessionOnStartupChanged: {
            if (EngineController.restoreSessionOnStartup !== appSettingsBridge.restoreSessionOnStartup) {
                EngineController.restoreSessionOnStartup = appSettingsBridge.restoreSessionOnStartup
            }
        }
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
            checked: appSettingsBridge.restoreSessionOnStartup
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
            onTriggered: {
                appSettingsBridge.restoreSessionOnStartup = checked
                EngineController.restoreSessionOnStartup = checked
            }
        }
    }
}
