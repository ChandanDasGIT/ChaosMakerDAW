import QtQuick
import QtQuick.Controls

// File > Open Recent: the recent projects and Clear Recent.
ThemedMenu {
    id: root
    objectName: "fileOpenRecentMenu"
    title: "Open Recent"
    width: 300
    readonly property bool empty: recentProjects.empty
    signal openRequested(string path)

    function refresh() {
        recentProjects.refresh()
    }

    RecentProjectsWrap {
        id: recentProjects
        objectName: "recentProjects"
    }

    Instantiator {
        model: recentProjects.paths
        delegate: ThemedMenuItem {
            required property int index
            required property string modelData
            objectName: "recentProject_" + index
            text: recentProjects.labels[index] || ""
            ToolTip.visible: hovered
            ToolTip.text: modelData
            ToolTip.delay: 450
            onTriggered: root.openRequested(modelData)
        }
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }

    ThemedMenuSeparator {}

    ThemedMenuItem {
        objectName: "clearRecentProjects"
        text: "Clear Recent"
        onTriggered: recentProjects.clear()
    }
}
