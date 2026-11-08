pragma ComponentBehavior: Bound
import Beam
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ScrollView {
    id: page
    required property DesktopController model
    readonly property var snapshot: model.state
    property string removing: ""
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: page.availableWidth
        spacing: 18
        Item {
            Layout.preferredHeight: 4
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            ColumnLayout {
                Layout.fillWidth: true
                Label {
                    text: "Your devices"
                    font.pixelSize: 30
                    font.bold: true
                    color: Theme.text
                }
                Label {
                    text: "A trusted connection, wherever you work."
                    color: Theme.secondary
                }
            }
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                text: "+ Add device"
                highlighted: true
                visible: page.snapshot.pairingSupported
                enabled: !page.snapshot.sending && !page.snapshot.checking
                onClicked: page.model.open_pairing()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Label {
                text: (page.snapshot.devices || []).length + " paired"
                color: Theme.secondary
            }
            Item {
                Layout.fillWidth: true
            }
            ActionButton {
                text: "My identity"
                onClicked: {
                    page.model.show_identity();
                    identity.open();
                }
            }
            ActionButton {
                objectName: "refreshPeers"
                text: "Refresh"
                onClicked: page.model.refresh_peers()
            }
        }
        Field {
            id: search
            objectName: "deviceSearch"
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            placeholderText: "Search devices or addresses"
        }
        BodyText {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            visible: text.length > 0
            text: page.snapshot.deviceError
            color: Theme.error
        }
        BodyText {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            visible: text.length > 0
            text: page.snapshot.deviceStatus
            color: Theme.secondary
        }
        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            visible: !devices.count
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label {
                    text: "Make your first connection"
                    font.pixelSize: 20
                    font.bold: true
                    color: Theme.text
                }
                BodyText {
                    Layout.fillWidth: true
                    text: "Add a device with a QR code or invitation, or restore a trusted backup."
                    color: Theme.secondary
                }
                ActionButton {
                    text: "Add device"
                    visible: page.snapshot.pairingSupported
                    onClicked: page.model.open_pairing()
                }
            }
        }
        Repeater {
            id: devices
            model: page.snapshot.devices || []
            delegate: Card {
                id: device
                required property var modelData
                readonly property bool matches: [modelData.name, modelData.endpoint, modelData.fallback].join(" ").toLowerCase().includes(search.text.toLowerCase())
                visible: matches
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: device.modelData.name
                            textFormat: Text.PlainText
                            font.pixelSize: 19
                            font.bold: true
                            color: Theme.text
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            text: "Edit"
                            onClicked: {
                                page.model.begin_edit(device.modelData.name);
                                if (!page.snapshot.deviceError)
                                    editor.open();
                            }
                        }
                    }
                    BodyText {
                        Layout.fillWidth: true
                        text: device.modelData.endpoint || "No direct address saved"
                        color: Theme.secondary
                    }
                    BodyText {
                        Layout.fillWidth: true
                        visible: !!device.modelData.fallback
                        text: "Fallback · " + device.modelData.fallback
                        color: Theme.secondary
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Theme.border
                    }
                    BodyText {
                        Layout.fillWidth: true
                        text: "Certificate paired"
                        color: Theme.secondary
                    }
                    BodyText {
                        Layout.fillWidth: true
                        text: page.snapshot.deviceChecks[device.modelData.name] || "Connection not checked"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ActionButton {
                            text: "Check connection"
                            enabled: !page.snapshot.checking && !page.snapshot.pairingOpen
                            onClicked: page.model.check_peer(device.modelData.name)
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            objectName: "unpairDevice-" + device.modelData.name
                            text: "Unpair device…"
                            onClicked: {
                                page.removing = device.modelData.name;
                                unpair.open();
                            }
                        }
                    }
                }
            }
        }
        Label {
            Layout.leftMargin: 24
            text: "No matching devices. Try another name or address."
            color: Theme.secondary
            visible: devices.count > 0 && !Array.from({
                length: devices.count
            }, (_, i) => devices.itemAt(i)).some(item => item && item.matches)
        }
        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label {
                    text: "Backup & restore"
                    font.pixelSize: 18
                    font.bold: true
                    color: Theme.text
                }
                BodyText {
                    Layout.fillWidth: true
                    text: "Keep a copy of your devices, addresses, and public certificates. Your private key stays on this device."
                    color: Theme.secondary
                }
                RowLayout {
                    ActionButton {
                        objectName: "exportDevices"
                        text: "Export backup…"
                        onClicked: page.model.export_devices()
                    }
                    ActionButton {
                        objectName: "importDevices"
                        text: "Import backup…"
                        onClicked: {
                            page.model.preview_import();
                            if (page.snapshot.importPreview.summary)
                                restore.open();
                        }
                    }
                }
            }
        }
        Item {
            Layout.preferredHeight: 24
        }
    }
    DeviceEditor {
        id: editor
        model: page.model
    }
    Dialog {
        id: unpair
        objectName: "unpairConfirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(500, parent.width - 48)
        modal: true
        title: "Unpair " + page.removing + "?"
        standardButtons: Dialog.Cancel
        ColumnLayout {
            width: parent.width
            spacing: 16
            BodyText {
                Layout.fillWidth: true
                text: "Active transfers and checks will stop. This removes local trust. Pair again to reconnect."
            }
            ActionButton {
                objectName: "confirmUnpair"
                text: "Unpair device"
                onClicked: {
                    page.model.unpair_device(page.removing);
                    unpair.close();
                }
            }
        }
    }
    Dialog {
        id: identity
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(580, parent.width - 48)
        height: Math.min(530, parent.height - 48)
        modal: true
        title: "This device’s identity"
        standardButtons: Dialog.Close
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            ColumnLayout {
                width: identity.availableWidth
                spacing: 16
                BodyText {
                    Layout.fillWidth: true
                    text: "Compare all 64 SHA-256 characters when manually pairing. Share only your public certificate."
                }
                BodyText {
                    Layout.fillWidth: true
                    text: (page.snapshot.identity || {}).fingerprint || ""
                }
                BodyText {
                    Layout.fillWidth: true
                    text: (page.snapshot.identity || {}).certificate || ""
                }
                ActionButton {
                    text: "Copy public certificate"
                    onClicked: page.model.copy_certificate()
                }
            }
        }
    }
    Dialog {
        id: restore
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(600, parent.width - 48)
        height: Math.min(600, parent.height - 48)
        modal: true
        title: "Restore your devices"
        standardButtons: Dialog.Cancel
        onClosed: page.model.discard_import()
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            ColumnLayout {
                width: restore.availableWidth
                spacing: 16
                BodyText {
                    Layout.fillWidth: true
                    text: (page.snapshot.importPreview || {}).summary || ""
                }
                BodyText {
                    Layout.fillWidth: true
                    text: "Only restore a backup you trust. Existing devices keep their current settings."
                }
                BodyText {
                    Layout.fillWidth: true
                    visible: !!(page.snapshot.importPreview || {}).differentIdentity
                    text: "This backup came from another identity. Pair this device on the other devices before transferring."
                }
                BodyText {
                    Layout.fillWidth: true
                    text: (page.snapshot.importPreview || {}).details || ""
                }
                BodyText {
                    Layout.fillWidth: true
                    text: page.snapshot.deviceError
                    color: Theme.error
                }
                ActionButton {
                    text: "Restore devices"
                    highlighted: true
                    onClicked: if (page.model.restore_devices())
                        restore.close()
                }
            }
        }
    }
}
