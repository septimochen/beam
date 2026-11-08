import Beam
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    required property DesktopController model
    readonly property var snapshot: model.state
    title: "Beam"
    width: 780
    height: 820
    minimumWidth: 680
    minimumHeight: 640
    color: Theme.background
    font.pixelSize: 14

    header: ToolBar {
        padding: 16
        background: Rectangle {
            color: Theme.background
        }
        RowLayout {
            anchors.centerIn: parent
            spacing: 4
            ActionButton {
                text: "Share"
                highlighted: pages.currentIndex === 0
                onClicked: root.model.show_share()
            }
            ActionButton {
                objectName: "manageDevices"
                text: "Devices"
                highlighted: pages.currentIndex === 1
                onClicked: root.model.open_devices()
            }
        }
        implicitHeight: 72
    }
    StackLayout {
        id: pages
        anchors.fill: parent
        currentIndex: root.snapshot.page
        SharePage {
            model: root.model
            onManageDevices: root.model.open_devices()
        }
        DevicesPage {
            model: root.model
        }
    }
    PairingDialog {
        model: root.model
    }
    Window {
        id: panel
        objectName: "transferPanel"
        transientParent: null
        title: "Beam transfers"
        width: 400
        height: 400
        minimumWidth: 360
        minimumHeight: 380
        color: Theme.background
        // An independent compositor-managed window, with no tray-position assumptions.
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            ColumnLayout {
                width: panel.width - 40
                x: 20
                y: 20
                spacing: 20
                Label {
                    text: "Transfers"
                    font.pixelSize: 22
                    font.bold: true
                    color: Theme.text
                }
                TransferStatus {
                    Layout.fillWidth: true
                    objectName: "panelSend"
                    title: "Send"
                    status: root.snapshot.sendStatus
                    progress: root.snapshot.sendProgress
                    busy: root.snapshot.sendBusy
                }
                TransferStatus {
                    Layout.fillWidth: true
                    objectName: "panelReceive"
                    title: "Receive"
                    status: root.snapshot.receiveStatus
                    progress: root.snapshot.receiveProgress
                    busy: root.snapshot.receiveBusy
                }
                RowLayout {
                    ActionButton {
                        text: "Cancel send"
                        enabled: root.snapshot.sending
                        onClicked: root.model.cancel_send()
                    }
                    ActionButton {
                        text: "Open Beam"
                        highlighted: true
                        onClicked: root.model.show_window()
                    }
                }
                Item {
                    Layout.preferredHeight: 20
                }
            }
        }
    }
}
