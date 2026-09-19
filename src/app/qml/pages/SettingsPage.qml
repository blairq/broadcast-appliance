// Configuracion completa: secciones a la izquierda, formulario generado desde
// el esquema (SettingsModel), barra de Aplicar/Revertir cuando hay cambios.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    property string section: Studio.settings.sections.length > 0 ? Studio.settings.sections[0] : ""
    readonly property string systemSection: "Sistema y datos"
    readonly property var sys: Studio.system
    property string updateError: ""

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
                model: Studio.settings.sections.concat([page.systemSection])
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
                visible: page.section !== page.systemSection
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
                        visible: page.section !== page.systemSection
                        Layout.topMargin: 10
                        text: "Valores por defecto de esta seccion"; compact: true
                        onClicked: Studio.settings.resetSection(page.section)
                    }
                }
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.systemSection
                sourceComponent: systemComp
            }
        }
    }

    // --- Sistema y datos -------------------------------------------------------------
    Component.onCompleted: sys.refresh()
    FolderDialog {
        id: updateDlg
        title: "Carpeta de la version nueva"
        onAccepted: {
            const err = sys.checkUpdate(selectedFolder)
            page.updateError = err
            if (err === "") { updConfirm.folder = selectedFolder; updConfirm.open() }
        }
    }
    Dialog {
        id: updConfirm
        property url folder
        anchors.centerIn: parent
        modal: true
        title: "Actualizar el sistema"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: 460
            wrapMode: Text.WordWrap
            text: "La app se cierra, se reemplaza el programa por el de\n" + updConfirm.folder.toString().replace("file://", "") +
                  "\ny vuelve a abrir sola. Configuracion, credenciales, grabaciones y archivos NO se tocan (antes se respaldan). Si algo sale mal, podes volver a esta version."
        }
        onAccepted: page.updateError = sys.startUpdate(folder)
    }
    Component {
        id: systemComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 16
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 13
                       text: "Todo el sistema vive en una sola carpeta (sirve desde un pendrive): " + sys.dataRoot }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Repeater {
                        model: [
                            { n: "Config", d: "Configuracion (escenas, ajustes, tablero)" },
                            { n: "Credenciales", d: "Tokens y claves, cifrados (AES-256-GCM). Una actualizacion nunca los toca." },
                            { n: "Recordings", d: "Grabaciones" },
                            { n: "Shares", d: "Archivos que comparten los invitados" },
                            { n: "Respaldos", d: "Copias de Config/ y Credenciales/ antes de cada actualizacion (quedan 5)" },
                            { n: "Logs", d: "Registros para diagnostico" }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            GlassButton { compact: true; Layout.preferredWidth: 150; text: modelData.n + "/"; icon: "▸"; onClicked: sys.openFolder(modelData.n) }
                            Text { text: modelData.d; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Credenciales guardadas (" + sys.credentials.length + ")"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: sys.credentials
                        delegate: StatusPill { required property string modelData; text: modelData; dot: Theme.ok }
                    }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                       text: "Sin contraseña: el cifrado evita que se lean por accidente (un pendrive FAT/exFAT no tiene permisos), pero quien se lleve el pendrive entero tiene la clave del equipo (Credenciales/clave-equipo). Para mudar el equipo a otro pendrive, copia la carpeta Credenciales/ completa." }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Actualizar"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                       text: "Descomprimi la version nueva en cualquier carpeta (fuera de esta) y elegila. Se reemplaza solo el programa; Config/, Credenciales/, Recordings/, Shares/ y Logs/ quedan como estan." }
                RowLayout {
                    GlassButton { text: "Actualizar desde una carpeta…"; icon: "⇪"; active: true; onClicked: updateDlg.open() }
                    GlassButton { text: "Volver a la version anterior"; visible: sys.canRollback; onClicked: page.updateError = sys.startRollback() }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.live; font.pixelSize: 12; text: page.updateError; visible: text !== "" }
                Text { visible: sys.backups.length > 0; text: "Respaldos"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Repeater {
                    model: sys.backups
                    delegate: RowLayout {
                        required property var modelData
                        Text { text: modelData.name; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                        GlassButton { compact: true; text: "Abrir"; onClicked: sys.openFolder("Respaldos/" + modelData.name) }
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
