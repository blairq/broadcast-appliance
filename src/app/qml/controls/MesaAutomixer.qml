// La mesa del automixer (T32): qué micrófono está en qué mesa, cuánto le da
// el automixer a cada uno AHORA (barra) y los ajustes de la mesa. Todo corre
// en GStreamer (`baautomixer` en la cadena de cada micrófono).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: mesa
    readonly property var vg: Studio.video
    // La mesa en vivo: la arma el grafo de video (en el dueño del motor) y llega con su foto.
    readonly property var datos: vg && vg.automixer.mesas ? vg.automixer : ({ mesas: [], microfonos: [] })
    property string msg: ""
    function mesaDe(nombre) {
        for (const m of datos.mesas)
            for (const x of m.microfonos) if (x.nombre === nombre) return { mesa: m, mic: x }
        return null
    }
    function avisar(err, ok) { msg = err ? err : ok }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: qsTr("Automixer de mesa redonda: los micrófonos de la misma mesa se reparten la ganancia. El que habla pasa al frente, los demás bajan y la suma no crece al abrir más.")
            color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap
        }
        Text {
            visible: mesa.datos.microfonos.length === 0
            text: qsTr("No hay micrófonos por GStreamer todavía (Entradas → Agregar entrada de audio).")
            color: Theme.textDim; font.pixelSize: 13
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Micrófonos")
            ListView {
                anchors.fill: parent
                clip: true
                spacing: 6
                model: mesa.datos.microfonos
                delegate: RowLayout {
                    id: fila
                    required property var modelData
                    readonly property var en: mesa.mesaDe(modelData)
                    width: ListView.view.width
                    spacing: 10
                    Text { text: fila.modelData; color: Theme.text; font.pixelSize: 13; Layout.preferredWidth: 200; elide: Text.ElideRight }
                    TextField {
                        id: grupo
                        Layout.preferredWidth: 130
                        placeholderText: qsTr("sin mesa")
                        text: fila.en ? fila.en.mesa.grupo : ""
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("La mesa (p. ej. «bancas»). Vacío: fuera del automixer.")
                        onEditingFinished: if (text !== (fila.en ? fila.en.mesa.grupo : ""))
                                               mesa.avisar(mesa.vg.automixerPonerUi(fila.modelData, text), text ? qsTr("En la mesa «%1»").arg(text) : qsTr("Fuera de la mesa"))
                    }
                    // La ganancia que aplica ahora (0 dB = pasa entero; -40 dB = abajo de todo).
                    Rectangle {
                        Layout.fillWidth: true
                        height: 12; radius: 6
                        color: Theme.glassBorder
                        visible: !!fila.en
                        Rectangle {
                            height: parent.height; radius: 6
                            color: Theme.accent
                            width: parent.width * Math.max(0, Math.min(1, 1 + ((fila.en && fila.en.mic.ganancia_db !== undefined) ? fila.en.mic.ganancia_db : -60) / 40))
                            Behavior on width { NumberAnimation { duration: 120 } }
                        }
                    }
                    Text {
                        visible: !!fila.en
                        Layout.preferredWidth: 150
                        color: Theme.textDim; font.pixelSize: 11
                        text: !fila.en ? "" : (fila.en.mic.andando === false ? qsTr("sin señal")
                              : (fila.en.mic.ganancia_db !== undefined ? fila.en.mic.ganancia_db.toFixed(1) + " dB" : "—")
                                + (fila.en.mic.nivel_db !== undefined ? qsTr("  · entra %1 dBFS").arg(Math.max(-90, fila.en.mic.nivel_db).toFixed(0)) : ""))
                    }
                }
            }
        }

        Repeater {
            model: mesa.datos.mesas
            delegate: Panel {
                id: panelMesa
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                title: qsTr("Mesa «%1» (%n micrófono)", "", modelData.microfonos.length).arg(modelData.grupo)
                readonly property var aj: modelData.ajustes || ({})
                function valor(k, def) { return aj[k] !== undefined ? aj[k] : def }
                Flow {
                    anchors.fill: parent
                    spacing: 14
                    Repeater {
                        model: [ { k: "umbral_db", t: qsTr("Umbral (dBFS)"), d: -40, a: -90, b: 0 },
                                 { k: "max_abiertos", t: qsTr("Máx. abiertos"), d: 4, a: 1, b: 32 },
                                 { k: "hold_ms", t: qsTr("Hold (ms)"), d: 300, a: 0, b: 5000 },
                                 { k: "piso_hold_db", t: qsTr("Piso del hold (dB)"), d: -20, a: -60, b: 0 },
                                 { k: "ataque_ms", t: qsTr("Ataque (ms)"), d: 5, a: 0, b: 500 },
                                 { k: "liberacion_ms", t: qsTr("Liberación (ms)"), d: 200, a: 0, b: 5000 } ]
                        delegate: Row {
                            required property var modelData
                            spacing: 6
                            Text { text: modelData.t; color: Theme.textDim; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            SpinBox {
                                from: modelData.a; to: modelData.b; editable: true
                                value: panelMesa.valor(modelData.k, modelData.d)
                                onValueModified: {
                                    const c = {}; c[modelData.k] = value
                                    mesa.avisar(mesa.vg.automixerAjustesUi(panelMesa.modelData.grupo, c), qsTr("Ajuste de la mesa guardado"))
                                }
                            }
                        }
                    }
                }
            }
        }
        Text { text: mesa.msg; color: Theme.textDim; font.pixelSize: 11; visible: mesa.msg !== "" }
    }
}
