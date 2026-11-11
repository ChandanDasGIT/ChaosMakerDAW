pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel

// The app's own file chooser, inside the main window and in the app's colours (the system one
// can't take them, and as a separate window it left the app without keyboard input, plan 1.11).
// Places on the side (Home, Music, Downloads, the open project's folder, mounted drives), a
// clickable path bar, the folders and matching files of the current folder, the file types,
// and Cancel / the action. Click a folder to open it; Backspace goes up, Enter accepts, Esc
// cancels.
//
// It is used like QtQuick.Dialogs' FileDialog: title, fileMode, nameFilters, defaultSuffix,
// currentFolder, selectedFile(s), open() / reject(), accepted() / rejected().
//   OpenFile:  a click on a file picks it.
//   OpenFiles: click, Ctrl+click and Shift+click choose files; double-click picks one; the
//              button takes all of them.
//   SaveFile:  a file name field; a click on a file takes its name; "Replace" when it exists.
Popup {
    id: root

    enum Mode { OpenFile, OpenFiles, SaveFile }

    property string title: "Choose a file"
    property int fileMode: FilePicker.OpenFile
    property var nameFilters: []         // ["Audio files (*.wav *.mp3)", "All files (*)"]
    property int selectedNameFilterIndex: 0
    property string defaultSuffix: ""    // added to a saved name that has no extension
    property url currentFolder: ""       // where it opens (else: the chosen file's, the last one, ...)
    property url selectedFile: ""        // the result; set before open() to suggest a name (save)
    property var selectedFiles: []       // the result: every chosen file (urls)
    property string acceptLabel: fileMode === FilePicker.SaveFile ? "Save" : "Open"

    signal accepted()
    signal rejected()

    // The folder shown now, and where the last pick was made (the next open starts there).
    property url folder: home
    property url lastFolder: ""
    readonly property url home: Files.toUrl(Files.homeFolder())
    readonly property url projectFolder: Files.folderOf(EngineController.projectFile)
    readonly property bool narrow: width < 480 // places in a row above the list, not beside it
    readonly property bool saveMode: fileMode === FilePicker.SaveFile
    readonly property bool manyMode: fileMode === FilePicker.OpenFiles
    readonly property var places: {
        const all = [
            { icon: Icons.home, name: "Home", url: home },
            { icon: Icons.music, name: "Music", url: StandardPaths.writableLocation(StandardPaths.MusicLocation) },
            { icon: Icons.download, name: "Downloads", url: StandardPaths.writableLocation(StandardPaths.DownloadLocation) }
        ]
        if (projectFolder.toString().length > 0)
            all.push({ icon: Icons.project, name: "Project folder", url: projectFolder })
        return all.filter(p => Files.isFolder(p.url))
    }
    property var drives: []              // mounted drives, read when opened
    property var chosen: []              // OpenFiles: the chosen file urls, as strings
    property int anchorRow: -1           // OpenFiles: where a Shift+click range starts
    property bool accepting: false

    readonly property var patterns: nameFilters.length > 0
        ? Files.filterPatterns(nameFilters[Math.min(Math.max(selectedNameFilterIndex, 0), nameFilters.length - 1)])
        : ["*"]
    // Saving: the name with its extension (defaultSuffix, else the file type's one extension).
    readonly property string suffix: {
        if (defaultSuffix.length > 0) return defaultSuffix
        return patterns.length === 1 && /^\*\.[^*?]+$/.test(patterns[0]) ? patterns[0].substring(2) : ""
    }
    readonly property string saveName: saveMode ? Files.withSuffix(nameField.text.trim(), suffix) : ""
    readonly property bool nameTaken: saveMode && saveName.length > 0 && Files.exists(Files.child(folder, saveName))
    readonly property var folders: listing.item  // the FolderListModel while open

    function goUp() {
        if (!Files.isRoot(folder)) folder = Files.parentOf(folder)
    }
    function isChosen(url) { return chosen.indexOf(url.toString()) >= 0 }
    function finish(files) {
        selectedFiles = files
        selectedFile = files.length > 0 ? files[0] : ""
        lastFolder = folder
        accepting = true
        close()
        accepted()
    }
    // The button / Enter.
    function accept() {
        if (saveMode) {
            if (saveName.length > 0) finish([Files.child(folder, saveName)])
        } else if (manyMode) {
            if (chosen.length > 0) finish(chosen.map(u => Files.toUrl(Files.toPath(u))))
        }
    }
    function reject() { close() }

    // A row was clicked (modifiers: the mouse's).
    function clickRow(row, url, isDir, modifiers) {
        list.currentIndex = row
        if (isDir) {
            folder = url
            return
        }
        if (saveMode) {
            nameField.text = Files.fileName(url)   // save over it
        } else if (!manyMode) {
            finish([url])
        } else if (modifiers & Qt.ShiftModifier && anchorRow >= 0 && folders) {
            const from = Math.min(anchorRow, row), to = Math.max(anchorRow, row)
            const next = (modifiers & Qt.ControlModifier) ? chosen.slice() : []
            for (let i = from; i <= to; ++i) {
                if (folders.get(i, "fileIsDir")) continue
                const u = folders.get(i, "fileUrl").toString()
                if (next.indexOf(u) < 0) next.push(u)
            }
            chosen = next
        } else if (modifiers & Qt.ControlModifier) {
            const next = chosen.slice()
            const at = next.indexOf(url.toString())
            if (at >= 0) next.splice(at, 1); else next.push(url.toString())
            chosen = next
            anchorRow = row
        } else {
            chosen = [url.toString()]
            anchorRow = row
        }
    }
    // Enter on the list: a folder opens, a file is picked (or chosen), else the button.
    function enterRow() {
        const row = list.currentIndex
        if (row >= 0 && folders && row < folders.count) {
            const url = folders.get(row, "fileUrl")
            if (folders.get(row, "fileIsDir")) { folder = url; return }
            if (saveMode) { nameField.text = Files.fileName(url); accept(); return }
            if (!manyMode) { finish([url]); return }
            if (chosen.length === 0) chosen = [url.toString()]
        }
        accept()
    }

    onAboutToShow: {
        accepting = false
        chosen = []
        anchorRow = -1
        drives = Files.drives()
        const fromFile = Files.folderOf(selectedFile)
        const start = [fromFile, currentFolder, lastFolder, projectFolder].find(u => Files.isFolder(u))
        folder = start ? start : home
        nameField.text = saveMode && selectedFile.toString().length > 0 ? Files.fileName(selectedFile) : ""
        filterBox.currentIndex = Math.min(Math.max(selectedNameFilterIndex, 0), Math.max(nameFilters.length - 1, 0))
    }
    onOpened: {
        if (saveMode) { nameField.forceActiveFocus(); nameField.selectAll() }
        else list.forceActiveFocus()
    }
    onClosed: if (!accepting) rejected()

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent ? parent.width - 32 : 720, 720)
    height: Math.min(parent ? parent.height - 32 : 520, 520)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 120; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: 80 }
    }

    background: Rectangle {
        color: Theme.panelBg
        radius: 8
        border.color: Theme.divider
    }

    contentItem: ColumnLayout {
        spacing: 0

        // The folder's listing, only while open (eleven of these are in the app).
        Loader {
            id: listing
            active: root.visible
            sourceComponent: FolderListModel {
                folder: root.folder
                showFiles: true
                nameFilters: root.patterns
                caseSensitive: false
                showDirsFirst: true
                showDotAndDotDot: false
                showHidden: hiddenChip.active
                sortCaseSensitive: false
            }
        }

        // Title
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 10
            spacing: 8

            Text {
                text: Icons.folderOpen
                color: Theme.accent
                font.pixelSize: Theme.bodySize
            }
            Text {
                objectName: "filePickerTitle"
                Layout.fillWidth: true
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.bold: true
                elide: Text.ElideRight
            }
            CloseButton {
                tooltip: "Cancel"
                onClicked: root.reject()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 14
            spacing: 10

            // ---- Places
            ColumnLayout {
                Layout.preferredWidth: 160
                Layout.maximumWidth: 160
                Layout.fillWidth: false
                Layout.fillHeight: true
                visible: !root.narrow
                spacing: 2

                Repeater {
                    model: root.places
                    delegate: Place {
                        required property var modelData
                        glyph: modelData.icon
                        label: modelData.name
                        target: modelData.url
                    }
                }

                Text {
                    Layout.topMargin: 8
                    Layout.leftMargin: 10
                    visible: root.drives.length > 0
                    text: "Drives"
                    color: Theme.accent
                    font.pixelSize: Theme.captionSize
                    font.bold: true
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    Layout.maximumHeight: 4 * 34
                    clip: true
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 2
                    model: root.drives
                    delegate: Place {
                        required property var modelData
                        width: ListView.view.width
                        glyph: Icons.drive
                        label: modelData.name
                        target: modelData.url
                    }
                }

                Item { Layout.fillHeight: true }

                ToggleChip {
                    id: hiddenChip
                    objectName: "filePickerHidden"
                    Layout.leftMargin: 6
                    implicitHeight: 24
                    leftPadding: 8
                    rightPadding: 8
                    text: "Hidden files"
                    hoverHighlight: true
                    tooltip: active ? "Hide hidden files" : "Show hidden files"
                    onClicked: active = !active
                }
            }

            // ---- Current folder
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                // Narrow: the places as a row of buttons.
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    visible: root.narrow
                    orientation: ListView.Horizontal
                    spacing: 6
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.places.concat(root.drives.map(d => ({ icon: Icons.drive, name: d.name, url: d.url })))
                    delegate: PanelButton {
                        required property var modelData
                        text: modelData.icon + "  " + modelData.name
                        checkable: false
                        checked: Files.toPath(root.folder) === Files.toPath(modelData.url)
                        onClicked: root.folder = modelData.url
                    }
                }

                // Path bar: up, then each part of the path (click to go there).
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    PanelButton {
                        objectName: "filePickerUp"
                        text: Icons.folderUp
                        implicitWidth: 30
                        implicitHeight: 28
                        enabled: !Files.isRoot(root.folder)
                        tooltip: "Up one folder (Backspace)"
                        Accessible.name: "Up one folder"
                        onClicked: root.goUp()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 28
                        radius: 4
                        color: Theme.field
                        border.color: Theme.fieldBorder
                        clip: true

                        ListView {
                            id: crumbs
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            orientation: ListView.Horizontal
                            boundsBehavior: Flickable.StopAtBounds
                            model: Files.crumbs(root.folder) // [{ name, url }], from the root
                            onCountChanged: positionViewAtEnd()
                            onWidthChanged: positionViewAtEnd()

                            delegate: Row {
                                id: crumb
                                required property var modelData
                                required property int index
                                readonly property bool last: index === crumbs.count - 1
                                height: crumbs.height

                                Text {
                                    // No separator after the root "/" ("/ home"); one after a drive ("C:", then Music).
                                    visible: crumb.index > 1 || (crumb.index === 1 && crumbs.model[0].name !== "/")
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Icons.submenu
                                    color: Theme.textDisabled
                                    font.pixelSize: Theme.smallSize
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    leftPadding: 5
                                    rightPadding: 5
                                    text: crumb.modelData.name
                                    color: crumb.last ? Theme.text : crumbMouse.containsMouse ? Theme.accent : Theme.textMuted
                                    font.pixelSize: Theme.smallSize
                                    font.bold: crumb.last

                                    MouseArea {
                                        id: crumbMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.folder = crumb.modelData.url
                                    }
                                }
                            }
                        }
                    }

                    ThemedComboBox {
                        id: filterBox
                        objectName: "filePickerFilter"
                        Layout.preferredWidth: 180
                        visible: root.nameFilters.length > 1
                        model: root.nameFilters.map(f => Files.filterLabel(f))
                        onActivated: root.selectedNameFilterIndex = currentIndex
                    }
                }

                ListView {
                    id: list
                    objectName: "filePickerList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 1
                    model: root.folders
                    keyNavigationEnabled: true
                    currentIndex: -1
                    highlightMoveDuration: 0
                    ScrollBar.vertical: ScrollBar {}

                    Keys.onReturnPressed: root.enterRow()
                    Keys.onEnterPressed: root.enterRow()
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Backspace) {
                            root.goUp()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Space && root.manyMode && list.currentIndex >= 0 && root.folders
                                   && !root.folders.get(list.currentIndex, "fileIsDir")) {
                            root.clickRow(list.currentIndex, root.folders.get(list.currentIndex, "fileUrl"), false, Qt.ControlModifier)
                            event.accepted = true
                        }
                    }

                    // A new folder starts at the top, its contents fade in.
                    Connections {
                        target: root
                        function onFolderChanged() {
                            list.currentIndex = -1
                            list.contentY = 0
                            root.anchorRow = -1
                            listFade.restart()
                        }
                    }
                    NumberAnimation {
                        id: listFade
                        target: list
                        property: "opacity"
                        from: 0.3
                        to: 1
                        duration: 120
                    }

                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string fileName
                        required property url fileUrl
                        required property bool fileIsDir
                        readonly property bool picked: !fileIsDir && root.manyMode && root.isChosen(fileUrl)
                        readonly property bool audio: !fileIsDir && Files.matchesFilter(fileName, ["*.wav", "*.flac", "*.mp3"])

                        objectName: "filePickerRow_" + fileName
                        width: ListView.view.width
                        height: 30
                        radius: 4
                        color: picked ? Theme.selectionBg
                             : rowMouse.containsMouse ? Theme.hoverOverlay
                             : ListView.isCurrentItem && list.activeFocus ? Theme.controlBg : "transparent"
                        border.color: ListView.isCurrentItem && list.activeFocus ? Theme.focusRing : "transparent"

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: (mouse) => {
                                list.forceActiveFocus()
                                root.clickRow(row.index, row.fileUrl, row.fileIsDir, mouse.modifiers)
                            }
                            onDoubleClicked: {
                                if (row.fileIsDir) return   // the first click opened it
                                if (root.saveMode) { nameField.text = row.fileName; root.accept() }
                                else if (root.manyMode) root.finish([row.fileUrl])
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            Text {
                                Layout.preferredWidth: 16
                                horizontalAlignment: Text.AlignHCenter
                                text: row.fileIsDir ? Icons.folder : row.audio ? Icons.audioFile : Icons.file
                                color: row.fileIsDir ? Theme.textMuted : Theme.accent
                                font.pixelSize: Theme.smallSize
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.fileName
                                color: row.picked ? Theme.textStrong : Theme.text
                                font.pixelSize: Theme.smallSize
                                elide: Text.ElideRight
                            }
                            Text {
                                text: Icons.submenu
                                color: Theme.textDisabled
                                font.pixelSize: Theme.smallSize
                                visible: row.fileIsDir
                                opacity: rowMouse.containsMouse ? 1 : 0
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !!root.folders && root.folders.status === FolderListModel.Ready && root.folders.count === 0
                        text: "No folders or matching files in here"
                        color: Theme.textDisabled
                        font.pixelSize: Theme.smallSize
                    }
                }
            }
        }

        // Buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.topMargin: 10
            spacing: 8

            Text {
                objectName: "filePickerHint"
                Layout.fillWidth: true
                visible: !root.saveMode
                text: root.manyMode
                      ? (root.chosen.length > 0 ? root.chosen.length + (root.chosen.length === 1 ? " file chosen" : " files chosen")
                                                : "Click files to choose them (Ctrl / Shift for more)")
                      : "Click a file to open it"
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                elide: Text.ElideRight
            }

            // Saving: the file name.
            TextField {
                id: nameField
                objectName: "filePickerName"
                Layout.fillWidth: true
                visible: root.saveMode
                implicitHeight: 28
                leftPadding: 10
                rightPadding: 10
                placeholderText: "File name"
                placeholderTextColor: Theme.textDisabled
                color: Theme.text
                font.pixelSize: Theme.smallSize
                selectByMouse: true
                onAccepted: root.accept()
                Keys.onEscapePressed: root.reject()
                background: Rectangle {
                    radius: 4
                    color: nameField.hovered || nameField.activeFocus ? Theme.fieldHover : Theme.field
                    border.color: nameField.activeFocus ? Theme.accent : Theme.fieldBorder
                }
            }

            PanelButton {
                objectName: "filePickerCancel"
                text: "Cancel"
                implicitHeight: 28
                onClicked: root.reject()
            }

            PanelButton {
                objectName: "filePickerAccept"
                visible: root.fileMode !== FilePicker.OpenFile
                enabled: root.saveMode ? root.saveName.length > 0 : root.chosen.length > 0
                implicitHeight: 28
                checkable: false
                checked: true   // the accent: the main action
                text: root.nameTaken ? "Replace"
                    : root.manyMode && root.chosen.length > 1 ? root.acceptLabel + " " + root.chosen.length + " files"
                    : root.acceptLabel
                onClicked: root.accept()
            }
        }
    }

    // A shortcut in the side bar.
    component Place: Rectangle {
        id: place
        property string glyph
        property string label
        property url target
        readonly property bool current: Files.toPath(root.folder) === Files.toPath(target)

        Layout.fillWidth: true
        implicitHeight: 32
        radius: 4
        color: current ? Theme.accentBg : placeMouse.containsMouse ? Theme.hoverOverlay : "transparent"

        MouseArea {
            id: placeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.folder = place.target
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 8
            spacing: 10

            Text {
                Layout.preferredWidth: 16
                horizontalAlignment: Text.AlignHCenter
                text: place.glyph
                color: place.current ? Theme.accent : Theme.textMuted
                font.pixelSize: Theme.smallSize
            }
            Text {
                Layout.fillWidth: true
                text: place.label
                color: place.current ? Theme.accent : Theme.text
                font.pixelSize: Theme.smallSize
                elide: Text.ElideRight
            }
        }
    }
}
