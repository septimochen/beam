pragma Singleton
import QtQuick

QtObject {
    readonly property SystemPalette colors: SystemPalette {
        colorGroup: SystemPalette.Active
    }
    readonly property color background: colors.window
    readonly property color surface: colors.base
    readonly property color text: colors.text
    readonly property color secondary: colors.placeholderText
    readonly property color accent: colors.highlight
    readonly property color border: Qt.alpha(colors.mid, 0.45)
    readonly property color error: colors.window.hslLightness < 0.5 ? "#ff9a9a" : "#a52335"
}
