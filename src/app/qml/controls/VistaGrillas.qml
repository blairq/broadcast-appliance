// Quién está en cada «Grilla de personas», en vivo (escenas/ControlGrillas,
// propiedades `grillas` y `gente`). La usan las Propiedades de la fuente (una
// grilla, con todo), la Sala y la Asamblea (todas, como resumen).
//
// Tocando una cara: fijarla en esa grilla (va siempre, primero, aunque los
// filtros digan otra cosa), soltarla, sacarla (de esa grilla y de su grupo) o
// moverla a otra. Los sacados se muestran abajo para devolverlos. Lo mismo por
// MCP: grilla_personas_fijar / _sacar / _mover / _devolver.
//
// La gente entra, sale y habla todo el tiempo: las listas se actualizan EN SU
// LUGAR, por clave (ListModel con clave), para que no salten mientras se las mira.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: vg
    spacing: 10
    /// "" = todas las grillas; si no, solo esa (la de las Propiedades).
    property string fuente: ""
    /// Resumen: sin los sacados ni «Fijar a alguien».
    property bool compacta: false

    readonly property var ctl: Studio.grillas
    readonly property var todas: ctl ? ctl.grillas : []
    readonly property var gente: ctl ? ctl.gente : []
    readonly property var mias: vg.fuente !== "" ? vg.todas.filter(g => g.fuente === vg.fuente) : vg.todas
    /// Hay algo que mostrar (para que quien la usa la esconda si no).
    readonly property bool hay: vg.mias.length > 0

    function avisar(e) { if (e !== "") Studio.notify(e, "error") }

    // Sincroniza un ListModel con una lista de objetos, por `clave` (o la que se diga), en su lugar.
    function sincronizar(modelo, lista, clave) {
        for (let i = modelo.count - 1; i >= 0; --i)
            if (!lista.some(x => x[clave] === modelo.get(i)[clave])) modelo.remove(i)
        for (let j = 0; j < lista.length; ++j) {
            let k = -1
            for (let i = 0; i < modelo.count; ++i) if (modelo.get(i)[clave] === lista[j][clave]) { k = i; break }
            if (k < 0) modelo.insert(Math.min(j, modelo.count), lista[j])
            else {
                modelo.set(k, lista[j])
                if (k !== j && j < modelo.count) modelo.move(k, j, 1)
            }
        }
    }

    // Una fila por grilla: solo lo que no cambia seguido (las caras las lleva cada fila).
    ListModel { id: filas }
    function sincronizarFilas() {
        vg.sincronizar(filas, vg.mias.map(g => ({ fuente: g.fuente })), "fuente")
    }
    onMiasChanged: sincronizarFilas()
    Component.onCompleted: sincronizarFilas()

    Text {
        Layout.fillWidth: true
        visible: !vg.hay
        wrapMode: Text.WordWrap
        color: Theme.textFaint; font.pixelSize: 12
        text: vg.ctl ? "No hay grillas de personas. Se agregan en Escenas → Agregar → Grilla de personas."
                     : "La grilla de personas todavía no arrancó."
    }

    Repeater {
        model: filas
        delegate: ColumnLayout {
            id: fila
            required property string fuente
            Layout.fillWidth: true
            spacing: 6
            readonly property var datos: vg.todas.find(g => g.fuente === fila.fuente) || ({ personas: [], fuera: [], mas: 0 })
            readonly property var personas: fila.datos.personas || []
            readonly property var fuera: fila.datos.fuera || []

            ListModel { id: caras }
            ListModel { id: sacados }
            function sincronizarCaras() {
                vg.sincronizar(caras, fila.personas.map(p => ({ clave: p.clave, nombre: p.nombre || p.clave,
                                                                fijo: !!p.fijo, hablando: !!p.hablando })), "clave")
                vg.sincronizar(sacados, fila.fuera.map(p => ({ clave: p.clave, nombre: p.nombre || p.clave })), "clave")
            }
            onDatosChanged: sincronizarCaras()
            Component.onCompleted: sincronizarCaras()

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    implicitWidth: 8; implicitHeight: 8; radius: 4
                    color: fila.datos.al_aire ? Theme.live : Theme.textFaint
                    ToolTip.visible: puntoMa.containsMouse; ToolTip.delay: 300
                    ToolTip.text: fila.datos.al_aire ? "Se ve en el programa" : "No está al aire"
                    MouseArea { id: puntoMa; anchors.fill: parent; hoverEnabled: true }
                }
                Text {
                    // Con una sola grilla (las Propiedades) el nombre ya está en el título.
                    visible: vg.fuente === ""
                    text: fila.fuente
                    color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                    elide: Text.ElideRight
                    Layout.maximumWidth: 220
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.textDim; font.pixelSize: 12
                    text: (fila.datos.al_aire ? "al aire · " : "") + caras.count + (caras.count === 1 ? " persona" : " personas")
                          + (fila.datos.mas > 0 ? " · +" + fila.datos.mas + " afuera" : "")
                          + (fila.datos.grupo ? " · grupo «" + fila.datos.grupo + "»" : "")
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !!fila.datos.aviso
                wrapMode: Text.WordWrap
                color: Theme.amber; font.pixelSize: 11
                text: fila.datos.aviso || ""
            }
            Text {
                Layout.fillWidth: true
                visible: caras.count === 0
                wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 12
                text: "Nadie por ahora: entran los admitidos de la Conferencia y los integrantes habilitados de la Asamblea, según los ajustes."
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: caras
                    delegate: Rectangle {
                        id: cara
                        required property string clave
                        required property string nombre
                        required property bool fijo
                        required property bool hablando
                        implicitWidth: filaCara.implicitWidth + 16
                        implicitHeight: 26
                        radius: 13
                        color: caraMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: cara.hablando ? 2 : 1
                        border.color: cara.hablando ? Theme.amber : Qt.rgba(1, 1, 1, 0.12)
                        RowLayout {
                            id: filaCara
                            anchors.centerIn: parent
                            spacing: 4
                            Text { visible: cara.fijo; text: "📌"; font.pixelSize: 10 }
                            Text { text: cara.nombre; color: Theme.text; font.pixelSize: 12
                                   elide: Text.ElideRight; Layout.maximumWidth: 180 }
                        }
                        MouseArea {
                            id: caraMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: menuCara.abrir(fila.fuente, cara.clave, cara.nombre, cara.fijo)
                        }
                    }
                }
            }
            // Los sacados a mano que siguen conectados: para devolverlos.
            Flow {
                Layout.fillWidth: true
                visible: !vg.compacta && sacados.count > 0
                spacing: 6
                Text { text: "Sacados:"; color: Theme.textFaint; font.pixelSize: 11; height: 24; verticalAlignment: Text.AlignVCenter }
                Repeater {
                    model: sacados
                    delegate: GlassButton {
                        required property string clave
                        required property string nombre
                        compact: true
                        text: nombre + "  ↺"
                        ToolTip.visible: hovered; ToolTip.delay: 300
                        ToolTip.text: "Devolver a «" + nombre + "» a esta grilla (vuelve a lo automático)"
                        onClicked: vg.avisar(vg.ctl.devolverQml(fila.fuente, clave))
                    }
                }
            }
            // Fijar a alguien que no está (otra grilla, el «+N», un filtro lo dejó afuera).
            RowLayout {
                Layout.fillWidth: true
                visible: !vg.compacta
                spacing: 8
                ComboBox {
                    id: sumar
                    Layout.fillWidth: true
                    // La lista se rehace cada vez que alguien entra o habla: lo elegido se
                    // recuerda por CLAVE, no por posición (que cambia debajo del dedo).
                    property string elegido: ""
                    readonly property var opciones: vg.gente.filter(p => !fila.personas.some(x => x.clave === p.clave))
                                                    .map(p => ({ clave: p.clave, texto: (p.nombre || p.clave)
                                                                 + (p.origen === "asamblea" ? " (asamblea)" : "")
                                                                 + (p.al_aire ? " · al aire" : "") }))
                    readonly property var opcionElegida: opciones.find(o => o.clave === elegido)
                    model: opciones
                    textRole: "texto"
                    currentIndex: -1
                    enabled: opciones.length > 0
                    onActivated: (i) => elegido = opciones[i] ? opciones[i].clave : ""
                    displayText: opciones.length === 0 ? "Están todos los conectados"
                                 : opcionElegida ? opcionElegida.texto : "Fijar a alguien acá…"
                }
                GlassButton {
                    compact: true; text: "Fijar"; active: true
                    enabled: !!sumar.opcionElegida
                    onClicked: {
                        vg.avisar(vg.ctl.fijarQml(fila.fuente, sumar.elegido))
                        sumar.elegido = ""
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.06); visible: vg.fuente === "" }
        }
    }

    // El menú de una cara (uno solo para todas).
    Menu {
        id: menuCara
        property string fuente: ""
        property string clave: ""
        property string nombre: ""
        property bool fijo: false
        readonly property var otras: vg.todas.map(g => g.fuente).filter(f => f !== menuCara.fuente)
        function abrir(f, c, n, fi) { fuente = f; clave = c; nombre = n; fijo = fi; popup() }
        MenuItem { enabled: false; text: menuCara.nombre }
        MenuSeparator {}
        MenuItem {
            text: menuCara.fijo ? "Soltar (vuelve a lo automático)" : "Fijar en esta grilla"
            onTriggered: vg.avisar(menuCara.fijo ? vg.ctl.devolverQml(menuCara.fuente, menuCara.clave)
                                                 : vg.ctl.fijarQml(menuCara.fuente, menuCara.clave))
        }
        MenuItem {
            text: "Sacar de esta grilla"
            onTriggered: vg.avisar(vg.ctl.sacarQml(menuCara.fuente, menuCara.clave))
        }
        Menu {
            id: menuMover
            title: "Mover a"
            enabled: menuCara.otras.length > 0
            ItemsDeMenu {
                menu: menuMover
                model: menuCara.otras
                delegate: MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: vg.avisar(vg.ctl.moverQml(menuCara.clave, menuCara.fuente, modelData))
                }
            }
        }
    }
}
