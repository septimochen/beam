import Beam
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: pairing
    objectName: "pairingDialog"
    required property DesktopController model
    readonly property var snapshot: model.state
    readonly property var session: snapshot.pairing || {}
    property string previousState: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 48)
    height: Math.min(720, parent.height - 48)
    modal: true
    title: "Add device"
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    onClosed: if (snapshot.pairingOpen)
        model.close_pairing()
    Connections {
        target: pairing.model
        function onChanged() {
            if (pairing.session.state === "awaiting approval" && pairing.previousState !== "awaiting approval")
                alias.text = pairing.session.alias || "";
            pairing.previousState = pairing.session.state || "";
            if (pairing.snapshot.pairingOpen && !pairing.visible)
                pairing.open();
            if (!pairing.snapshot.pairingOpen && pairing.visible)
                pairing.close();
        }
    }
    onOpened: {
        invitation.text = "";
        alias.text = "";
    }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: pairing.availableWidth
            spacing: 14
            BodyText {
                Layout.fillWidth: true
                text: "Open Add device on both devices on the same local network. Review and approve on both screens."
            }
            BodyText {
                Layout.fillWidth: true
                text: (pairing.session.state || "") + ": " + (pairing.session.message || "")
            }
            BodyText {
                Layout.fillWidth: true
                text: pairing.snapshot.deviceError
                color: Theme.error
            }
            Canvas {
                id: qr
                readonly property var rows: pairing.session.qr || []
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 280
                Layout.preferredHeight: 280
                visible: pairing.session.state === "hosting" && rows.length > 0
                Accessible.name: "Pairing QR code"
                Accessible.role: Accessible.Graphic
                onRowsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.fillStyle = "white";
                    ctx.fillRect(0, 0, width, height);
                    const size = rows.length;
                    const scale = Math.floor(width / (size + 8));
                    const offset = Math.floor((width - size * scale) / 2);
                    ctx.fillStyle = "black";
                    for (let y = 0; y < size; ++y)
                        for (let x = 0; x < size; ++x)
                            if (rows[y][x] === "1")
                                ctx.fillRect(offset + x * scale, offset + y * scale, scale, scale);
                }
            }
            ActionButton {
                text: "Copy invitation"
                enabled: pairing.session.state === "hosting"
                onClicked: pairing.model.pairing_action("copy", "")
            }
            Field {
                id: invitation
                Layout.fillWidth: true
                placeholderText: "Paste a Beam invitation"
                maximumLength: 2048
            }
            ActionButton {
                text: "Use invitation"
                onClicked: pairing.model.pairing_action("join", invitation.text)
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: pairing.session.state === "awaiting approval"
                BodyText {
                    Layout.fillWidth: true
                    text: "Device: " + (pairing.session.remoteName || "")
                    font.bold: true
                }
                Field {
                    id: alias
                    objectName: "pairingAlias"
                    Layout.fillWidth: true
                    placeholderText: "Local alias for this device"
                    maximumLength: 63
                    text: pairing.session.alias || ""
                }
                RowLayout {
                    ActionButton {
                        objectName: "approveDevice"
                        text: "Approve device"
                        highlighted: true
                        onClicked: pairing.model.pairing_action("approve", alias.text)
                    }
                    ActionButton {
                        text: "Decline"
                        onClicked: pairing.model.pairing_action("reject", "")
                    }
                }
            }
            ActionButton {
                text: "New invitation"
                onClicked: pairing.model.pairing_action("host", "")
            }
            ActionButton {
                text: "Forget pending approval"
                visible: pairing.session.state === "incomplete"
                onClicked: pairing.model.pairing_action("forget", "")
            }
        }
    }
}
