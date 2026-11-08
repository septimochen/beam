import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    padding: 10
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 13
    hoverEnabled: true
    Accessible.name: text
    contentItem: Text {
        text: control.text
        font: control.font
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.highlighted ? control.palette.highlightedText : Theme.text
        opacity: control.enabled ? 1 : 0.45
    }
    background: Rectangle {
        radius: 9
        color: control.highlighted ? Theme.accent : control.down ? control.palette.midlight : control.hovered ? control.palette.alternateBase : Theme.surface
        opacity: control.enabled ? 1 : 0.5
        border.color: control.activeFocus ? Theme.accent : control.highlighted ? "transparent" : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
}
