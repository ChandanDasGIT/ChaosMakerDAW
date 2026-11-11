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
                // Latency compensation (restMap G10), owned by EngineController: audio takes are
                // moved earlier by the round trip the device reports plus the extra offset.
                CheckBox {
                    id: latencyCompensationBox
                    objectName: "latencyCompensationSetting"
                    text: "Compensate recording latency"
                    checked: EngineController.recordingLatencyCompensation
                    onToggled: EngineController.recordingLatencyCompensation = checked
                    Binding on checked { value: EngineController.recordingLatencyCompensation }
                    contentItem: Text {
                        text: latencyCompensationBox.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: latencyCompensationBox.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
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
                        font.pixelSize: 12
                        horizontalAlignment: TextInput.AlignHCenter
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: DoubleValidator { bottom: -100; top: 100; decimals: 2; notation: DoubleValidator.StandardNotation }
                        text: String(EngineController.recordingExtraOffsetMs)
                        background: Rectangle { color: Theme.field; border.color: offsetField.activeFocus ? Theme.accent : Theme.fieldBorder; radius: 3 }
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
                RadioButton {
                    id: zoneOctavesRadio
                    objectName: "chordZoneOctaves"
                    text: "Own octave per zone · low C0–C1, middle C4–C5, high C6–C7"
                    checked: appSettingsBridge.chordZoneMapping === "octaves"
                    onToggled: if (checked) appSettingsBridge.chordZoneMapping = "octaves"
                    contentItem: Text {
                        text: zoneOctavesRadio.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: zoneOctavesRadio.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    id: zoneKeysRadio
                    objectName: "chordZoneKeys"
                    text: "Zone keys · F#2 / G#2 / A#2, then C4–C5"
                    checked: appSettingsBridge.chordZoneMapping === "zonekeys"
                    onToggled: if (checked) appSettingsBridge.chordZoneMapping = "zonekeys"
                    contentItem: Text {
                        text: zoneKeysRadio.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: zoneKeysRadio.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
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
                CheckBox {
                    id: snapPlayheadBox
                    objectName: "snapPlayheadSetting"
                    text: "Snap playhead to grid"
                    checked: appSettingsBridge.snapPlayhead
                    onToggled: appSettingsBridge.snapPlayhead = checked
                    contentItem: Text {
                        text: snapPlayheadBox.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: snapPlayheadBox.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                CheckBox {
                    id: snapSelectionBox
                    objectName: "snapSelectionSetting"
                    text: "Snap time selection to grid"
                    checked: appSettingsBridge.snapSelection
                    onToggled: appSettingsBridge.snapSelection = checked
                    contentItem: Text {
                        text: snapSelectionBox.text
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        leftPadding: snapSelectionBox.indicator.width + 6
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
