import QtQuick
import QtQuick.Controls.Basic

TextArea {
    readOnly: true
    selectByMouse: true
    textFormat: TextEdit.PlainText
    wrapMode: TextEdit.Wrap
    color: Theme.text
    font.pixelSize: 13
    padding: 0
    background: null
    Accessible.name: text
}
