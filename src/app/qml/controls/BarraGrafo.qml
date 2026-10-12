// La barra de arriba de un grafo (audio y video): la MISMA en las dos páginas,
// con los botones agrupados por tipo de operación y siempre en este orden:
//
//   [título] [vistas]  │  [+ Agregar ▾] [acciones de la página]  …  [− 100 % + Ajustar] [Grilla] [Ordenar]  │ [⋯]
//    qué estoy viendo     qué le sumo / qué hago                      cómo lo miro (solo la vista)          lo raro
//
// - «Agregar» es UN botón: abre un menú por secciones (conexiones, procesar,
//   medir…). Antes cada tarjeta posible era un botón suelto en la fila y se
//   mezclaba con todo lo demás.
// - Lo de la vista (zoom, grilla, ordenar) va a la derecha y nunca cambia
//   nada del grafo, salvo «Ordenar», que solo mueve tarjetas.
// - Lo que se usa poco o es peligroso (restablecer, sacar todo) va en «⋯».
//
// Si no entra en un renglón, los grupos saltan al siguiente (Flow); si los
// botones de la página no entran ni así, van al menú «Herramientas ▾».
//
// `lienzo` es el área del grafo: tiene que tener `escala`, `zoom`, `grilla`,
// `acercar(f)` y `ordenar()` (Patchbay los tiene; AudioPage, en su lienzo).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: barra
    property string titulo: ""
    /// [{id, t, icon?, tip?}]: las vistas de la página (Racks | Estudio…).
    property var vistas: []
    property string vista: ""
    signal elegirVista(string id)
    /// Secciones del menú «Agregar»: [{titulo, items: [{t, tip?, color?, f}]}].
    property var agregar: []
    property bool puedeAgregar: true
    property string porQueNoAgregar: ""
    /// Botones propios de la página (paneles): [{t, icon?, tip?, f}].
    property var acciones: []
    /// El menú «⋯»: [{t, tip?, peligro?, f}].
    property var mas: []
    property var lienzo: null

    /// No entra todo en un renglón: lo que se agrega y los paneles bajan al segundo.
    readonly property bool apretado: medidaAcciones.implicitWidth + 160 > width
    readonly property bool dosRenglones: grupoVer.implicitWidth + grupoHacer.implicitWidth + grupoMirar.implicitWidth + 3 * 12 > width
    implicitHeight: grilla.implicitHeight

    component Separador: Rectangle { width: 1; height: 20; color: Qt.rgba(1, 1, 1, 0.12); Layout.alignment: Qt.AlignVCenter }

    // Los tres grupos FLUYEN: si no entran en un renglón, el que no entra baja al
    // siguiente (con el panel de la derecha ancho, la barra nunca se sale de lugar).
    Flow {
        id: grilla
        anchors { left: parent.left; right: parent.right; top: parent.top }
        spacing: 12

    // --- Qué estoy viendo ------------------------------------------------------------
    RowLayout {
        id: grupoVer
        spacing: 6
        Text {
            text: barra.titulo
            visible: text !== ""
            color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
            elide: Text.ElideRight
            Layout.maximumWidth: 220
        }
        Row {
            visible: barra.vistas.length > 1
            spacing: 2
            Repeater {
                model: barra.vistas
                delegate: GlassButton {
                    required property var modelData
                    compact: true; text: modelData.t; icon: modelData.icon || ""
                    active: barra.vista === modelData.id
                    onClicked: barra.elegirVista(modelData.id)
                    ToolTip.visible: hovered && !!modelData.tip; ToolTip.delay: 400; ToolTip.text: modelData.tip || ""
                }
            }
        }
        // Con opacidad (no visible): que no cambie el ancho que decide los renglones.
        Separador { visible: barra.titulo !== "" || barra.vistas.length > 1; opacity: barra.dosRenglones ? 0 : 1 }
    }

    // --- Qué le sumo / qué hago -------------------------------------------------------
    RowLayout {
        id: grupoHacer
        spacing: 6
        GlassButton {
            id: botonAgregar
            visible: barra.agregar.length > 0
            compact: true; text: "+ Agregar"; active: menuAgregar.opened
            enabled: barra.puedeAgregar
            onClicked: menuAgregar.opened ? menuAgregar.close() : menuAgregar.open()
            ToolTip.visible: hovered && !barra.puedeAgregar && barra.porQueNoAgregar !== ""
            ToolTip.text: barra.porQueNoAgregar
            Popup {
                id: menuAgregar
                y: parent.height + 6
                padding: 12
                width: 460
                height: Math.min(secciones.implicitHeight + 2 * padding, 560)
                background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        id: secciones
                        width: parent.width
                        spacing: 12
                        Repeater {
                            model: barra.agregar
                            delegate: ColumnLayout {
                                id: sec
                                required property var modelData
                                visible: (modelData.items || []).length > 0
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    text: sec.modelData.titulo
                                    color: Theme.textFaint; font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.5 }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Repeater {
                                        model: sec.modelData.items || []
                                        delegate: GlassButton {
                                            required property var modelData
                                            compact: true
                                            text: modelData.t
                                            activeColor: modelData.color || Theme.accent
                                            active: !!modelData.color
                                            enabled: modelData.enabled !== false
                                            onClicked: { menuAgregar.close(); modelData.f() }
                                            ToolTip.visible: hovered && !!modelData.tip; ToolTip.delay: 400; ToolTip.text: modelData.tip || ""
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Repeater {
            model: barra.apretado ? [] : barra.acciones
            delegate: GlassButton {
                required property var modelData
                compact: true; text: modelData.t; icon: modelData.icon || ""
                enabled: modelData.enabled !== false
                onClicked: modelData.f()
                ToolTip.visible: hovered && !!modelData.tip; ToolTip.delay: 400; ToolTip.text: modelData.tip || ""
            }
        }
        // No entran (panel de la derecha ancho, ventana chica): juntas en un menú.
        GlassButton {
            visible: barra.apretado && barra.acciones.length > 0
            compact: true; text: "Herramientas ▾"; active: menuHerr.opened
            onClicked: menuHerr.opened ? menuHerr.close() : menuHerr.open()
            Popup {
                id: menuHerr
                y: parent.height + 6
                padding: 8
                background: Rectangle { color: Theme.panel; radius: 8; border.color: Theme.glassBorder }
                ColumnLayout {
                    spacing: 4
                    Repeater {
                        model: barra.acciones
                        delegate: GlassButton {
                            required property var modelData
                            Layout.fillWidth: true
                            compact: true; text: modelData.t; icon: modelData.icon || ""
                            enabled: modelData.enabled !== false
                            onClicked: { menuHerr.close(); modelData.f() }
                            ToolTip.visible: hovered && !!modelData.tip; ToolTip.delay: 400; ToolTip.text: modelData.tip || ""
                        }
                    }
                }
            }
        }

    }

    // Lo que ocupan los botones de la página en fila (medido aparte, sin mostrarse):
    // si no entran ni en el segundo renglón, van al menú «Herramientas».
    Row {
        id: medidaAcciones
        // Transparente y sin recibir clics (invisible, un Row no se mide).
        opacity: 0
        enabled: false
        z: -1
        spacing: 6
        Repeater {
            model: barra.acciones
            delegate: GlassButton { required property var modelData; compact: true; text: modelData.t; icon: modelData.icon || "" }
        }
    }

    // --- Cómo lo miro (no cambia el grafo) --------------------------------------------
    RowLayout {
        id: grupoMirar
        spacing: 6
        Row {
            visible: barra.lienzo !== null
            spacing: 2
            Repeater {
                model: [
                    { t: "−", tip: "Alejar (Cmd + rueda)", f: () => barra.lienzo.acercar(1 / 1.2) },
                    { t: barra.lienzo ? Math.round(barra.lienzo.escala * 100) + " %" : "", tip: "Tamaño real (100 %)", f: () => barra.lienzo.zoom = 1, ancho: 52 },
                    { t: "+", tip: "Acercar (Cmd + rueda)", f: () => barra.lienzo.acercar(1.2) },
                    { t: "Ajustar", tip: "Que entre todo a lo ancho", f: () => barra.lienzo.zoom = barra.lienzo.escalaParaTodo || 0 }
                ]
                delegate: GlassButton {
                    required property var modelData
                    compact: true; text: modelData.t
                    width: modelData.ancho ? modelData.ancho : implicitWidth
                    onClicked: modelData.f()
                    ToolTip.visible: hovered; ToolTip.delay: 500; ToolTip.text: modelData.tip
                }
            }
        }
        GlassButton {
            visible: barra.lienzo !== null
            compact: true; text: "Grilla"; icon: "▦"
            active: !!barra.lienzo && barra.lienzo.grilla
            onClicked: barra.lienzo.grilla = !barra.lienzo.grilla
            ToolTip.visible: hovered; ToolTip.delay: 500
            ToolTip.text: "Dejar la grilla a la vista (al mover una tarjeta aparece igual)"
        }
        GlassButton {
            visible: barra.lienzo !== null
            compact: true; text: "Ordenar"
            onClicked: barra.lienzo.ordenar()
            ToolTip.visible: hovered; ToolTip.delay: 500
            ToolTip.text: "Acomoda las tarjetas en la grilla siguiendo el orden de la cadena (no cambia ninguna conexión)"
        }

        // --- Lo raro ----------------------------------------------------------------------
        Separador { visible: barra.mas.length > 0 }
        GlassButton {
            visible: barra.mas.length > 0
            compact: true; text: "⋯"; active: menuMas.opened
            onClicked: menuMas.opened ? menuMas.close() : menuMas.open()
            ToolTip.visible: hovered && !menuMas.opened; ToolTip.delay: 500; ToolTip.text: "Más"
            Popup {
                id: menuMas
                y: parent.height + 6
                x: parent.width - width
                padding: 8
                background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
                ColumnLayout {
                    spacing: 4
                    Repeater {
                        model: barra.mas
                        delegate: GlassButton {
                            required property var modelData
                            Layout.fillWidth: true
                            compact: true; text: modelData.t
                            activeColor: Theme.live
                            active: !!modelData.peligro
                            onClicked: { menuMas.close(); modelData.f() }
                            ToolTip.visible: hovered && !!modelData.tip; ToolTip.delay: 400; ToolTip.text: modelData.tip || ""
                        }
                    }
                }
            }
        }
    }
    }
}
