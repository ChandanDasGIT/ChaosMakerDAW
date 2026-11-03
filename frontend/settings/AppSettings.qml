import QtQuick
import QtQuick.Controls
import ChaosMaker

ToolButton {
    id: root
    objectName: "settingsButton"
    text: "Settings"
    onClicked: settingsMenu.open()
    palette.buttonText: Theme.text

    AppSettingsWrapper {
        id: appSettingsBridge
        onRestoreSessionOnStartupChanged: {
            if (EngineController.restoreSessionOnStartup !== appSettingsBridge.restoreSessionOnStartup) {
                EngineController.restoreSessionOnStartup = appSettingsBridge.restoreSessionOnStartup
            }
        }
    }

    Menu {
        id: settingsMenu
        y: parent.height

        MenuItem {
            objectName: "restoreSessionSetting"
            text: "Restore session on startup"
            checkable: true
            checked: appSettingsBridge.restoreSessionOnStartup
            onTriggered: {
                appSettingsBridge.restoreSessionOnStartup = checked
                EngineController.restoreSessionOnStartup = checked
            }
        }
    }
}
