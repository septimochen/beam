import Beam
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ScrollView {
    id: page
    required property DesktopController model
    readonly property var snapshot: model.state
    signal manageDevices
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: page.availableWidth
        spacing: 20
        Item {
            Layout.preferredHeight: 4
        }
        RowLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 14
            Image {
                source: "qrc:/beam/icons/beam.png"
                sourceSize.width: 48
                sourceSize.height: 48
                Accessible.ignored: true
            }
            ColumnLayout {
                Label {
                    text: "Beam"
                    font.pixelSize: 30
                    font.bold: true
                    color: Theme.text
                }
                Label {
                    text: "Share directly with your paired devices."
                    color: Theme.secondary
                }
            }
        }
        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            ColumnLayout {
                anchors.fill: parent
                spacing: 14
                Label {
                    text: "Send"
                    font.pixelSize: 18
                    font.bold: true
                    color: Theme.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Device"
                        color: Theme.secondary
                    }
                    ComboBox {
                        id: peers
                        objectName: "peers"
                        Layout.fillWidth: true
                        model: page.snapshot.peers || []
                        currentIndex: Math.max(0, (page.snapshot.peers || []).indexOf(page.model.peer))
                        displayText: count ? currentText : "No paired devices"
                        implicitHeight: 40
                        background: Rectangle {
                            radius: 10
                            color: Theme.background
                            border.color: peers.activeFocus ? Theme.accent : Theme.border
                            border.width: peers.activeFocus ? 2 : 1
                        }
                        contentItem: Text {
                            text: peers.displayText
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: 14
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 12
                            rightPadding: 30
                            elide: Text.ElideRight
                        }
                        indicator: Text {
                            text: "⌄"
                            color: Theme.secondary
                            font.pixelSize: 20
                            x: peers.width - width - 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Accessible.name: "Device"
                        onActivated: page.model.peer = currentText
                    }
                    ActionButton {
                        text: "Manage devices"
                        onClicked: page.manageDevices()
                    }
                    ActionButton {
                        text: "Add device…"
                        visible: page.snapshot.pairingSupported
                        enabled: !page.snapshot.sending && !page.snapshot.checking
                        onClicked: page.model.open_pairing()
                    }
                }
                RowLayout {
                    ActionButton {
                        objectName: "checkConnectivity"
                        text: "Check connectivity"
                        enabled: !!page.model.peer && !page.snapshot.checking && !page.snapshot.pairingOpen
                        onClicked: page.model.check_peer(page.model.peer)
                    }
                    BodyText {
                        Layout.fillWidth: true
                        text: page.snapshot.connectivityStatus
                    }
                }
                RowLayout {
                    ActionButton {
                        text: "File"
                        highlighted: page.model.kind === 0
                        onClicked: page.model.kind = 0
                    }
                    ActionButton {
                        text: "Text"
                        highlighted: page.model.kind === 1
                        onClicked: page.model.kind = 1
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
                RowLayout {
                    ActionButton {
                        text: "Choose file…"
                        onClicked: page.model.choose_file()
                    }
                    ActionButton {
                        objectName: "loadClipboard"
                        text: "Load clipboard text"
                        onClicked: page.model.load_clipboard()
                    }
                }
                BodyText {
                    Layout.fillWidth: true
                    visible: page.model.kind === 0
                    text: page.snapshot.file || "Choose a file or drop it into this window"
                    color: page.snapshot.file ? Theme.text : Theme.secondary
                    Accessible.name: "Selected file: " + page.snapshot.file
                }
                ColumnLayout {
                    visible: page.model.kind === 1
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Text to send"
                            color: Theme.text
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            text: "Copy to clipboard"
                            onClicked: page.model.copy_text(false)
                        }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        TextArea {
                            id: content
                            objectName: "content"
                            text: page.model.content
                            textFormat: TextEdit.PlainText
                            color: Theme.text
                            placeholderText: "Review the text or URL before sending"
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            Accessible.name: "Content to send"
                            onTextChanged: if (activeFocus && text !== page.model.content)
                                page.model.content = text
                            background: Rectangle {
                                radius: 10
                                color: Theme.background
                                border.color: content.activeFocus ? Theme.accent : Theme.border
                            }
                        }
                    }
                }
                BodyText {
                    Layout.fillWidth: true
                    text: "Clipboard text is read only when you click Load. Nothing is sent until you click Send."
                    color: Theme.secondary
                    font.pixelSize: 12
                }
                RowLayout {
                    ActionButton {
                        objectName: "send"
                        text: "Send"
                        highlighted: true
                        enabled: !page.snapshot.sending && !!page.model.peer && !page.snapshot.pairingOpen
                        onClicked: page.model.send_selection()
                    }
                    ActionButton {
                        text: "Cancel send"
                        enabled: page.snapshot.sending
                        onClicked: page.model.cancel_send()
                    }
                }
                TransferStatus {
                    Layout.fillWidth: true
                    title: "Send progress"
                    status: page.snapshot.sendStatus
                    progress: page.snapshot.sendProgress
                    busy: page.snapshot.sendBusy
                }
            }
        }
        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            ColumnLayout {
                anchors.fill: parent
                spacing: 14
                Label {
                    text: "Receive"
                    font.pixelSize: 18
                    font.bold: true
                    color: Theme.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    BodyText {
                        Layout.fillWidth: true
                        text: page.snapshot.output
                        Accessible.name: "Receive folder: " + text
                    }
                    ActionButton {
                        text: "Choose folder…"
                        onClicked: page.model.choose_folder()
                    }
                }
                TransferStatus {
                    Layout.fillWidth: true
                    title: "Receive progress"
                    status: page.snapshot.receiveStatus
                    progress: page.snapshot.receiveProgress
                    busy: page.snapshot.receiveBusy
                }
                RowLayout {
                    visible: page.snapshot.hasReceivedText
                    Layout.fillWidth: true
                    Label {
                        text: "Received text"
                        color: Theme.text
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        objectName: "copyReceived"
                        text: "Copy to clipboard"
                        onClicked: page.model.copy_text(true)
                    }
                }
                ScrollView {
                    visible: page.snapshot.hasReceivedText
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    TextArea {
                        objectName: "receivedText"
                        text: page.snapshot.receivedText
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        Accessible.name: "Received text"
                        background: Rectangle {
                            radius: 10
                            color: Theme.background
                            border.color: Theme.border
                        }
                    }
                }
            }
        }
        BodyText {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: page.snapshot.lastResult
        }
        BodyText {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: page.snapshot.deviceError
            color: Theme.error
            visible: text.length > 0
        }
        Item {
            Layout.preferredHeight: 24
        }
    }
}
