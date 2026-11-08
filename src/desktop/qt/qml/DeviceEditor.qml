import Beam
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: editor
    required property DesktopController model
    readonly property var snapshot: model.state
    property string lastReplacement: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(580, parent.width - 48)
    height: Math.min(720, parent.height - 48)
    modal: true
    title: "Edit device"
    standardButtons: Dialog.Cancel
    onOpened: {
        const device = snapshot.editor;
        name.text = device.name;
        endpoint.text = device.endpoint;
        fallback.text = device.fallback;
        replace.checked = false;
        certificate.text = "";
        fingerprint.text = "";
    }
    Connections {
        target: editor.model
        function onChanged() {
            if (editor.snapshot.replacementCertificate !== editor.lastReplacement) {
                editor.lastReplacement = editor.snapshot.replacementCertificate || "";
                certificate.text = editor.lastReplacement;
            }
        }
    }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: editor.availableWidth
            spacing: 14
            Field {
                id: name
                objectName: "deviceName"
                Layout.fillWidth: true
                placeholderText: "Device name"
                maximumLength: 63
            }
            BodyText {
                Layout.fillWidth: true
                text: "1–63 lowercase letters, digits, - or _; start with a letter."
                color: Theme.secondary
            }
            Field {
                id: endpoint
                objectName: "deviceEndpoint"
                Layout.fillWidth: true
                placeholderText: "IP address & port, e.g. 192.168.1.20:4269"
                maximumLength: 256
            }
            Field {
                id: fallback
                objectName: "deviceFallback"
                Layout.fillWidth: true
                placeholderText: "Fallback address (optional)"
                maximumLength: 256
            }
            BodyText {
                Layout.fillWidth: true
                text: "Leave an address empty to remove it. Saving stops active transfers and checks."
                color: Theme.secondary
            }
            Label {
                text: "Current SHA-256"
                color: Theme.text
            }
            BodyText {
                Layout.fillWidth: true
                text: (editor.snapshot.editor || {}).fingerprint || ""
            }
            Label {
                text: "Public certificate"
                color: Theme.text
            }
            BodyText {
                Layout.fillWidth: true
                text: (editor.snapshot.editor || {}).certificate || ""
            }
            CheckBox {
                id: replace
                text: "Replace certificate"
                Accessible.name: text
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: replace.checked
                BodyText {
                    Layout.fillWidth: true
                    text: "This changes which identity you trust. Verify the full new fingerprint on the other device."
                }
                ActionButton {
                    text: "Choose public certificate…"
                    onClicked: editor.model.choose_certificate()
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    TextArea {
                        id: certificate
                        textFormat: TextEdit.PlainText
                        color: Theme.text
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        placeholderText: "Replacement public certificate PEM"
                        Accessible.name: placeholderText
                    }
                }
                Field {
                    id: fingerprint
                    Layout.fillWidth: true
                    placeholderText: "Independently verified SHA-256 (all 64 characters)"
                    maximumLength: 64
                }
            }
            BodyText {
                Layout.fillWidth: true
                text: editor.snapshot.deviceError
                color: Theme.error
            }
            ActionButton {
                text: replace.checked ? "Save & trust certificate" : "Save changes"
                highlighted: true
                onClicked: {
                    if (editor.model.save_device({
                        name: name.text,
                        endpoint: endpoint.text,
                        fallback: fallback.text,
                        replace: replace.checked,
                        certificate: certificate.text,
                        fingerprint: fingerprint.text
                    }))
                        editor.close();
                }
            }
        }
    }
}
