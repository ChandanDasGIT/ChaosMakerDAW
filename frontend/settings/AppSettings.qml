import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../audiodevice"

ToolButton {
    id: root
    objectName: "settingsButton"
    text: "Settings"
    implicitHeight: 30
    checked: settingsMenu.visible
    onClicked: settingsMenu.visible ? settingsMenu.close() : settingsMenu.open()

    readonly property alias pianoRollPresentation: appSettingsBridge.pianoRollPresentation
    // The app's one AppSettingsWrapper, for settings the window uses directly (layout sizes).
    readonly property alias bridge: appSettingsBridge
    // Opens Preferences at the top (Audio & MIDI), e.g. from the top bar's device line.
    function openPreferences() {
        settingsMenu.close()
        preferencesScroll.contentY = 0
        preferencesPopup.open()
    }

    AppSettingsWrapper {
        id: appSettingsBridge
        objectName: "appSettingsBridge"
    }

    leftPadding: 10

    rightPadding: 10

    contentItem: RowLayout {
        spacing: 4
        Text {
            text: "Settings"
            color: root.down || root.checked ? Theme.accent : Theme.text
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: Theme.controlRadius
        color: root.down || root.checked ? Theme.controlBgActive : root.hovered ? Theme.controlBgHover : "transparent"
        border.color: root.checked ? Theme.divider : "transparent"
    }

    ThemedMenu {
        id: settingsMenu
        objectName: "settingsMenu"
        y: parent.height + 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent   // the button toggles it
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

        ThemedMenuItem {
            objectName: "shortcutsSetting"
            text: "Keyboard shortcuts…"
            onTriggered: {
                settingsMenu.close()
                shortcutEditor.open()
            }
        }
    }

    ShortcutEditor {
        id: shortcutEditor
    }

    Popup {
        id: preferencesPopup
        objectName: "preferencesPopup"
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        // Two columns when the window allows, one otherwise.
        width: Math.min(860, parent.width - 32)
        // Never taller than the window: the sections scroll instead.
        height: Math.min(implicitHeight, parent.height - 32)
        padding: 16
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.panelBg
            border.color: Theme.divider
            border.width: 1
            radius: Theme.panelRadius
        }
        contentItem: ColumnLayout {
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "Preferences"
                    color: Theme.text
                    font.pixelSize: Theme.headingSize
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

            // The sections scroll when the window is too short for all of them.
            Flickable {
                id: preferencesScroll
                objectName: "preferencesScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: preferencesBody.implicitHeight
                contentHeight: preferencesBody.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                readonly property bool overflowing: contentHeight > height + 1
                ScrollBar.vertical: ScrollBar { id: preferencesScrollBar; policy: preferencesScroll.overflowing ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

                GridLayout {
                    id: preferencesBody
                    width: preferencesScroll.width - (preferencesScroll.overflowing ? preferencesScrollBar.width + 6 : 0)
                    columns: width > 700 ? 2 : 1
                    columnSpacing: 28
                    rowSpacing: 12
                    // Left: audio and recording. Right: chords, timeline, piano roll, shortcuts.
                    ColumnLayout {
                        objectName: "preferencesLeftColumn"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: 12

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
                            ThemedRadioButton {
                                id: recordDryRadio
                                objectName: "defaultRecordModeDry"
                                text: "Dry · editable FX"
                                checked: appSettingsBridge.defaultRecordMode === "dry"
                                onToggled: if (checked) appSettingsBridge.defaultRecordMode = "dry"
                            }
                            ThemedRadioButton {
                                id: recordPrintRadio
                                objectName: "defaultRecordModePrint"
                                text: "Print FX"
                                checked: appSettingsBridge.defaultRecordMode === "print"
                                onToggled: if (checked) appSettingsBridge.defaultRecordMode = "print"
                            }
                            // Latency compensation (restMap G10), owned by EngineController: audio takes are
                            // moved earlier by the round trip the device reports plus the extra offset.
                            ThemedCheckBox {
                                id: latencyCompensationBox
                                objectName: "latencyCompensationSetting"
                                text: "Compensate recording latency"
                                checked: EngineController.recordingLatencyCompensation
                                onToggled: EngineController.recordingLatencyCompensation = checked
                                Binding on checked { value: EngineController.recordingLatencyCompensation }
                            }
                            RowLayout {
                                spacing: 8
                                enabled: EngineController.recordingLatencyCompensation
                                opacity: enabled ? 1 : 0.45
                                Text {
                                    text: "Extra offset (ms)"
                                    color: Theme.text
                                    font.pixelSize: Theme.bodySize
                                }
                                TextField {
                                    id: offsetField
                                    objectName: "recordingOffsetField"
                                    Layout.preferredWidth: 70
                                    selectByMouse: true
                                    color: Theme.text
                                    font.pixelSize: Theme.bodySize
                                    horizontalAlignment: TextInput.AlignHCenter
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    validator: DoubleValidator { bottom: -100; top: 100; decimals: 2; notation: DoubleValidator.StandardNotation }
                                    text: String(EngineController.recordingExtraOffsetMs)
                                    background: Rectangle { color: Theme.field; border.color: offsetField.activeFocus ? Theme.accent : Theme.fieldBorder; radius: Theme.controlRadius }
                                    onEditingFinished: {
                                        const ms = Number(text.replace(",", "."))
                                        if (isFinite(ms)) EngineController.recordingExtraOffsetMs = ms
                                        text = String(EngineController.recordingExtraOffsetMs)
                                    }
                                    ToolTip.visible: hovered; ToolTip.delay: 450
                                    ToolTip.text: "-100 to 100 ms, added to what the device reports (measure it with tools/latencytest)"
                                }
                            }
                            Text {
                                id: reportedLatency
                                objectName: "reportedLatencyText"
                                property real ms: EngineController.reportedRoundTripMs()
                                text: ms > 0 ? "The device reports " + ms.toFixed(1) + " ms round trip; takes move earlier by that plus the extra offset."
                                             : "The device reports no round-trip latency right now; only the extra offset applies."
                                color: Theme.textMuted
                                font.pixelSize: Theme.smallSize
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                Connections {
                                    target: preferencesPopup
                                    function onAboutToShow() { reportedLatency.ms = EngineController.reportedRoundTripMs() }
                                }
                            }
                        }

                    }
                    ColumnLayout {
                        objectName: "preferencesRightColumn"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: 12
                        Text {
                            text: "Chords Track Zones"
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.bold: true
                        }

                        ColumnLayout {
                            spacing: 8
                            Text {
                                text: "How a strip's low / middle / high zone is written to MIDI (KEYS, recording). Clips play either way."
                                color: Theme.textMuted
                                font.pixelSize: Theme.smallSize
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            ThemedRadioButton {
                                id: zoneOctavesRadio
                                objectName: "chordZoneOctaves"
                                Layout.fillWidth: true
                                wrap: true
                                text: "Own octave per zone · low C0–C1, middle C4–C5, high C6–C7"
                                checked: appSettingsBridge.chordZoneMapping === "octaves"
                                onToggled: if (checked) appSettingsBridge.chordZoneMapping = "octaves"
                            }
                            ThemedRadioButton {
                                id: zoneKeysRadio
                                objectName: "chordZoneKeys"
                                Layout.fillWidth: true
                                wrap: true
                                text: "Zone keys · F#2 / G#2 / A#2, then C4–C5"
                                checked: appSettingsBridge.chordZoneMapping === "zonekeys"
                                onToggled: if (checked) appSettingsBridge.chordZoneMapping = "zonekeys"
                            }
                        }

                        Text {
                            text: "Timeline"
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.bold: true
                        }

                        ColumnLayout {
                            spacing: 8
                            Text {
                                text: "Off: the playhead and the time selection go exactly where you click and drag. On: they land on the snap grid. Clips snap either way."
                                color: Theme.textMuted
                                font.pixelSize: Theme.smallSize
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            ThemedCheckBox {
                                id: snapPlayheadBox
                                objectName: "snapPlayheadSetting"
                                text: "Snap playhead to grid"
                                checked: appSettingsBridge.snapPlayhead
                                onToggled: appSettingsBridge.snapPlayhead = checked
                            }
                            ThemedCheckBox {
                                id: snapSelectionBox
                                objectName: "snapSelectionSetting"
                                text: "Snap time selection to grid"
                                checked: appSettingsBridge.snapSelection
                                onToggled: appSettingsBridge.snapSelection = checked
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
                            ThemedRadioButton {
                                id: dockRadio
                                text: "Bottom Docked Panel"
                                checked: appSettingsBridge.pianoRollPresentation === "dock"
                                onToggled: if (checked) appSettingsBridge.pianoRollPresentation = "dock"
                            }
                            ThemedRadioButton {
                                id: windowRadio
                                text: "Floating Window"
                                checked: appSettingsBridge.pianoRollPresentation === "window"
                                onToggled: if (checked) appSettingsBridge.pianoRollPresentation = "window"
                            }
                        }

                        // Keyboard shortcuts (restMap G16): the editor opens over Preferences.
                        Text {
                            text: "Keyboard Shortcuts"
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.bold: true
                        }

                        RowLayout {
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: "Change the key of any action, or put them all back."
                                color: Theme.textMuted
                                font.pixelSize: Theme.smallSize
                                wrapMode: Text.WordWrap
                            }
                            PanelButton {
                                objectName: "editShortcutsButton"
                                text: "Edit shortcuts…"
                                onClicked: shortcutEditor.open()
                            }
                        }
                    }
                }
            }
        }
    }
}
