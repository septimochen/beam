import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: transfer
    required property string title
    required property string status
    required property real progress
    required property bool busy
    spacing: 10
    Label {
        text: transfer.title
        textFormat: Text.PlainText
        color: Theme.text
        font.pixelSize: 16
        font.bold: true
    }
    ProgressBar {
        id: bar
        Layout.fillWidth: true
        value: transfer.progress
        indeterminate: transfer.busy
        Accessible.name: transfer.title + " progress"
        background: Rectangle {
            implicitHeight: 6
            radius: 3
            color: Theme.border
        }
        contentItem: Item {
            implicitHeight: 6
            clip: true
            Rectangle {
                width: bar.indeterminate ? parent.width * 0.25 : parent.width * bar.visualPosition
                height: parent.height
                radius: 3
                color: Theme.accent
                x: bar.indeterminate ? (parent.width - width) * pulse.phase : 0
                QtObject {
                    id: pulse
                    property real phase: 0
                }
                SequentialAnimation {
                    running: bar.indeterminate && bar.visible
                    loops: Animation.Infinite
                    NumberAnimation {
                        target: pulse
                        property: "phase"
                        from: 0
                        to: 1
                        duration: 900
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        target: pulse
                        property: "phase"
                        from: 1
                        to: 0
                        duration: 900
                        easing.type: Easing.InOutSine
                    }
                }
            }
        }
    }
    BodyText {
        Layout.fillWidth: true
        text: transfer.status
        color: Theme.secondary
    }
}
