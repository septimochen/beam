import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control
    selectByMouse: true
    color: Theme.text
    placeholderTextColor: Theme.secondary
    padding: 12
    font.pixelSize: 14
    Accessible.name: placeholderText
    background: Rectangle {
        radius: 10
        color: Theme.surface
        border.color: control.activeFocus ? Theme.accent : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
}
