// Configuracion completa: secciones a la izquierda, formulario generado desde
// el esquema (SettingsModel), barra de Aplicar/Revertir cuando hay cambios.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    property string section: Studio.settings.sections.length > 0 ? Studio.settings.sections[0] : ""

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20; bottomMargin: Studio.settings.dirty ? 84 : 20 }
        spacing: 16
        Behavior on anchors.bottomMargin { NumberAnimation { duration: 180 } }

        Panel {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            title: "Configuracion"
            ListView {
                anchors.fill: parent
                spacing: 4
                model: Studio.settings.sections
                delegate: Rectangle {
                    required property string modelData
                    width: ListView.view.width
                    height: 42
                    radius: 12
                    color: modelData === page.section ? Qt.rgba(1, 0.42, 0.17, 0.25) : (sectMa.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
                    Text { anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                           text: modelData; color: Theme.text; font.pixelSize: 13 }
                    MouseArea { id: sectMa; anchors.fill: parent; hoverEnabled: true; onClicked: page.section = modelData }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: page.section
            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Repeater {
                        model: Studio.settings
                        delegate: FormField {
                            required property string key
                            required property string section
                            required label
                            required kind
                            required value
                            required options
                            required minimum
                            required maximum
                            required suffix
                            required help
                            required modified
                            required property string scope
                            badge: scope
                            visible: section === page.section
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? implicitHeight : 0
                            onEdited: (v) => Studio.settings.setValue(key, v)
                        }
                    }
                    GlassButton {
                        Layout.topMargin: 10
                        text: "Valores por defecto de esta seccion"; compact: true
                        onClicked: Studio.settings.resetSection(page.section)
                    }
                }
            }
        }
    }

    // --- Barra de cambios pendientes ---------------------------------------------
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: page.leftInset + 20; rightMargin: 20; bottomMargin: 16 }
        height: 56
        radius: 18
        visible: opacity > 0
        opacity: Studio.settings.dirty ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        color: Qt.rgba(1, 0.42, 0.17, 0.16)
        border.color: Qt.rgba(1, 0.6, 0.35, 0.6)
        RowLayout {
            anchors { fill: parent; leftMargin: 18; rightMargin: 12 }
            Text { text: "Hay cambios sin aplicar"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
            GlassButton { text: "Revertir"; onClicked: Studio.settings.revert() }
            GlassButton { text: "Aplicar"; active: true; onClicked: Studio.settings.apply() }
        }
    }
}
