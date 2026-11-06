// Pruebas → Capacidad: ¿cuántos invitados aguanta? Este equipo, otro
// equipo y la conexión a internet. Solo presentación: todo sale de Studio.capacidad
// (ui/CapacidadViewModel), el mismo modelo que usan las herramientas capacidad_* del MCP.
// Ver docs/funciones/CAPACIDAD.md.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

ScrollView {
    id: panel
    clip: true
    contentWidth: availableWidth
    readonly property var cv: Studio.capacidad
    readonly property var res: cv.resultado
    readonly property var resumen: res.resultado || ({})
    readonly property var vivo: cv.vivo
    readonly property var prog: cv.progreso
    readonly property var rec: cv.recibir
    readonly property var net: cv.internet
    readonly property bool rampa: cv.estado === "local" || cv.estado === "remoto"
    property bool verCodigo: false
    property bool copiado: false

    function fecha(ms) { return ms ? new Date(ms).toLocaleString(Qt.locale(), Locale.ShortFormat) : "" }
    function num(x, dec) { return x === undefined || x === null || x < 0 ? "–" : Number(x).toFixed(dec || 0) }
    function confirmar(titulo, texto, acepta, accion) {
        ApplicationWindow.window.confirm({ title: titulo, text: texto, accept: acepta, reject: qsTr("Cancelar"), action: accion })
    }
    function avisar(err) { if (err) Studio.notify(err, "error") }

    // Un gráfico chico de una medida por escalón: una barra por escalón (roja si se pasó)
    // y la línea del umbral.
    component Mini: ColumnLayout {
        id: mini
        property string titulo: ""
        property var valores: []      // [{n, v}]
        property real tope: 100
        property real umbral: -1
        property bool malArriba: true // pasarse es estar arriba del umbral (si no, abajo)
        property string unidad: ""
        Layout.fillWidth: true
        Layout.preferredWidth: 150
        spacing: 4
        Text { text: mini.titulo; color: Theme.textDim; font.pixelSize: 11 }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            Rectangle { anchors.fill: parent; radius: 4; color: Qt.rgba(1, 1, 1, 0.03) }
            Row {
                anchors { fill: parent; margins: 4 }
                spacing: 4
                Repeater {
                    model: mini.valores
                    delegate: Item {
                        required property var modelData
                        readonly property bool conocido: modelData.v !== undefined && modelData.v !== null && modelData.v >= 0
                        readonly property bool mal: conocido && mini.umbral >= 0 &&
                                                    (mini.malArriba ? modelData.v > mini.umbral : modelData.v < mini.umbral)
                        width: Math.max(6, (parent.width - 4 * (mini.valores.length - 1)) / Math.max(1, mini.valores.length))
                        height: parent.height
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: parent.conocido ? Math.max(2, Math.min(1, modelData.v / mini.tope) * parent.height) : 2
                            radius: 2
                            color: !parent.conocido ? Theme.textFaint : parent.mal ? Theme.live : Theme.accent
                            opacity: parent.conocido ? 0.9 : 0.4
                        }
                        ToolTip.visible: barMa.containsMouse
                        ToolTip.text: qsTr("%1 invitados: %2").arg(modelData.n).arg(conocido ? panel.num(modelData.v, 0) + " " + mini.unidad : "–")
                        MouseArea { id: barMa; anchors.fill: parent; hoverEnabled: true }
                    }
                }
            }
            Rectangle {
                visible: mini.umbral >= 0 && mini.umbral <= mini.tope
                x: 2; width: parent.width - 4; height: 1
                y: 4 + (parent.height - 8) * (1 - mini.umbral / mini.tope)
                color: Theme.amber
                opacity: 0.8
            }
        }
        Row {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: mini.valores
                delegate: Text {
                    required property var modelData
                    width: Math.max(6, (mini.width - 4 * (mini.valores.length - 1)) / Math.max(1, mini.valores.length))
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData.n
                    color: Theme.textFaint
                    font.pixelSize: 9
                }
            }
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: 14

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 13
            text: qsTr("¿Cuántos invitados aguanta? Entran invitados de prueba por escalones (10, 20, 40, 60, 80…, cada uno 2 minutos como mucho) hasta que la máquina se degrada: la CPU sostenida pasa del 85 %, queda poca memoria, la grilla baja de 25 cuadros por segundo, el audio se atrasa más de 50 ms o la interfaz se traba. Los de prueba no tocan el padrón ni la votación, y el evento abierto no se modifica.")
        }

        // Por qué no se puede ahora (algo al aire, gente conectada, otra prueba).
        Rectangle {
            Layout.fillWidth: true
            visible: panel.cv.riesgo !== "" && !panel.cv.ocupado
            implicitHeight: riesgoTxt.implicitHeight + 16
            radius: Theme.radiusSmall
            color: Qt.rgba(0.96, 0.72, 0.24, 0.12)
            border.color: Theme.amber
            Text {
                id: riesgoTxt
                anchors { fill: parent; margins: 8 }
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 12
                text: qsTr("Ahora no se puede probar: %1").arg(panel.cv.riesgo)
            }
        }

        // --- En curso ------------------------------------------------------------------
        Seccion {
            visible: panel.cv.ocupado && panel.cv.estado !== "recibiendo"
            titulo: panel.cv.estado === "internet" ? qsTr("Midiendo la conexión")
                    : panel.cv.estado === "remoto" ? qsTr("Probando %1").arg(panel.prog.maquina || "")
                    : qsTr("Probando este equipo")
            subtitulo: panel.prog.texto || ""
            resaltado: Theme.accent
            extra: GlassButton {
                text: qsTr("Cancelar")
                compact: true
                onClicked: panel.cv.cancelar()
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: panel.rampa
                from: 0; to: 1
                value: panel.prog.avance || 0
            }
            Text {
                Layout.fillWidth: true
                visible: panel.rampa && (panel.prog.escalones || []).length > 0
                color: Theme.textFaint
                font.pixelSize: 11
                text: qsTr("Escalón %1 de %2 · %3 s").arg((panel.prog.indice || 0) + 1).arg((panel.prog.escalones || []).length).arg(panel.prog.segundosEscalon || 0)
            }
            Flow {
                Layout.fillWidth: true
                visible: panel.rampa
                spacing: 6
                StatusPill { text: qsTr("%1/%2 con video").arg(panel.num(panel.vivo.conVideo)).arg(panel.prog.escalon || 0); dot: Theme.ok }
                StatusPill { text: qsTr("CPU %1 %").arg(panel.num(panel.vivo.cpuMaquina)); dot: (panel.vivo.cpuMaquina || 0) > 85 ? Theme.live : Theme.ok }
                StatusPill {
                    visible: (panel.vivo.cpuGenerador || -1) >= 0
                    text: qsTr("hinchada %1 %").arg(panel.num(panel.vivo.cpuGenerador)); dot: Theme.textDim
                }
                StatusPill {
                    text: qsTr("memoria libre %1 GB").arg(panel.num((panel.vivo.memLibreMb || -1000) / 1000, 1))
                    dot: (panel.vivo.memLibreMb || 99999) < 4500 ? Theme.live : Theme.ok
                }
                StatusPill { visible: (panel.vivo.cps || -1) >= 0; text: qsTr("grilla %1 cps").arg(panel.num(panel.vivo.cps)); dot: (panel.vivo.cps || 30) < 25 ? Theme.live : Theme.ok }
                StatusPill { visible: (panel.vivo.audioMs || -1) >= 0; text: qsTr("audio %1 ms").arg(panel.num(panel.vivo.audioMs)); dot: (panel.vivo.audioMs || 0) > 50 ? Theme.live : Theme.ok }
            }
            // Los dos lados en vivo: la máquina medida y la que genera.
            GraficasPrueba {
                Layout.fillWidth: true
                visible: panel.rampa && panel.cv.serie.length > 1
                serie: panel.cv.serie
            }
        }

        // --- Este equipo -------------------------------------------------------------------
        Seccion {
            titulo: qsTr("Este equipo")
            subtitulo: qsTr("¿Cuántos invitados aguanta esta máquina? El generador de prueba (hinchada) corre acá mismo: su CPU se mide aparte y se descuenta.")
            // Vale para este equipo y para probar otro (abajo).
            OpcionDecodificar {
                id: decodificar
                Layout.fillWidth: true
                enabled: !panel.cv.ocupado
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Hasta"); color: Theme.textDim; font.pixelSize: 12 }
                SpinBox {
                    id: topeLocal
                    from: 10; to: panel.cv.topeMax; stepSize: 10; value: 80; editable: true
                }
                Text { text: qsTr("invitados"); color: Theme.textDim; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                GlassButton {
                    text: qsTr("Probar este equipo…")
                    active: true
                    enabled: !panel.cv.ocupado && panel.cv.riesgo === "" && panel.cv.hinchadaDisponible
                    onClicked: panel.confirmar(qsTr("¿Probar este equipo?"),
                                               qsTr("Van a entrar hasta %1 invitados de prueba por escalones: la máquina se va a cargar durante unos minutos. No lo hagas con un evento en curso. Se puede cancelar en cualquier momento.").arg(topeLocal.value),
                                               qsTr("Probar"),
                                               function() { panel.avisar(panel.cv.probarEsteEquipo(topeLocal.value, decodificar.modo, decodificar.pct)) })
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !panel.cv.hinchadaDisponible
                wrapMode: Text.WordWrap
                color: Theme.amber
                font.pixelSize: 11
                text: qsTr("El generador de prueba (hinchada) no viene en este programa: esta máquina se puede probar desde otro equipo (abajo, «Que otro equipo pruebe este»).")
            }
        }

        // --- El resultado ------------------------------------------------------------------
        Seccion {
            visible: (panel.res.frase || "") !== ""
            titulo: panel.res.tipo === "remoto" ? qsTr("Resultado: %1").arg(panel.res.maquina || "") : qsTr("Resultado")
            subtitulo: panel.fecha(panel.res.fecha)
            resaltado: panel.resumen.degradoEn > 0 || panel.res.error ? Theme.amber : Theme.ok
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                font { pixelSize: 15; weight: Font.DemiBold }
                text: panel.res.frase || ""
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.cv.pasos.length > 0
                spacing: 12
                Mini {
                    titulo: panel.res.tipo === "local" ? qsTr("CPU sin el generador (%)") : qsTr("CPU (%)")
                    tope: 100; umbral: 85; unidad: "%"
                    valores: panel.cv.pasos.map(p => ({ n: p.n, v: p.cpu }))
                }
                Mini {
                    titulo: qsTr("Memoria disponible (GB)")
                    tope: Math.max(8, ...panel.cv.pasos.map(p => (p.memTotalMb || 0) / 1000))
                    umbral: 4.5; malArriba: false; unidad: "GB"
                    valores: panel.cv.pasos.map(p => ({ n: p.n, v: p.memMinMb >= 0 ? p.memMinMb / 1000 : -1 }))
                }
                Mini {
                    titulo: qsTr("Grilla (cuadros por segundo)")
                    tope: 32; umbral: 25; malArriba: false; unidad: "cps"
                    valores: panel.cv.pasos.map(p => ({ n: p.n, v: p.cps }))
                }
                Mini {
                    titulo: qsTr("Audio de los invitados (ms)")
                    tope: 100; umbral: 50; unidad: "ms"
                    valores: panel.cv.pasos.map(p => ({ n: p.n, v: p.audioMs }))
                }
            }
            // Escalón por escalón, lo que se pasó.
            Repeater {
                model: panel.cv.pasos
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: modelData.degradado ? Theme.amber : Theme.textDim
                    text: modelData.degradado
                          ? qsTr("%1 invitados: %2").arg(modelData.n).arg((modelData.motivos || []).join(" · "))
                          : qsTr("%1 invitados: bien (CPU %2 %, %3/%1 con video)").arg(modelData.n).arg(panel.num(modelData.cpu)).arg(panel.num(modelData.conVideo))
                }
            }
        }

        // --- Un sistema remoto ----------------------------------------------------------------
        Seccion {
            titulo: qsTr("Un sistema remoto")
            subtitulo: qsTr("Probar otro BroadcastAppliance desde acá (otra PC): en el otro, «Que otro equipo pruebe este» muestra un código; pegalo acá. Los invitados de prueba salen de esta máquina y el otro informa su CPU, su memoria y su red; esta le manda la suya (y su ficha) para su informe. Varias PC pueden generar contra la misma: cada una pega el mismo código.")
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                GlassButton { compact: true; text: qsTr("Rampa (cuántos aguanta)"); active: !salaRemota.checked; onClicked: salaRemota.checked = false }
                GlassButton { compact: true; text: qsTr("Prueba de sala (N durante unos minutos)"); active: salaRemota.checked; onClicked: salaRemota.checked = true }
                CheckBox { id: salaRemota; visible: false }
                Text { visible: salaRemota.checked; text: qsTr("minutos"); color: Theme.textDim; font.pixelSize: 12 }
                SpinBox { id: minutosRemota; visible: salaRemota.checked; from: 1; to: 15; value: 3; editable: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: codigo
                    Layout.fillWidth: true
                    placeholderText: qsTr("Código del otro equipo (BACAP1.…)")
                    echoMode: TextInput.PasswordEchoOnEdit
                    enabled: !panel.cv.ocupado
                }
                Text { text: salaRemota.checked ? qsTr("Invitados") : qsTr("Hasta"); color: Theme.textDim; font.pixelSize: 12 }
                SpinBox { id: topeRemoto; from: 1; to: panel.cv.topeMax; stepSize: 10; value: 80; editable: true }
                GlassButton {
                    text: qsTr("Probar el otro equipo…")
                    enabled: !panel.cv.ocupado && codigo.text.trim() !== "" && panel.cv.hinchadaDisponible
                    onClicked: panel.confirmar(qsTr("¿Probar el otro equipo?"),
                                               salaRemota.checked
                                               ? qsTr("Esta máquina va a generar %1 invitados de prueba contra el otro equipo durante %2 minutos. Que el otro no tenga un evento en curso.").arg(topeRemoto.value).arg(minutosRemota.value)
                                               : qsTr("Esta máquina va a generar hasta %1 invitados de prueba contra el otro equipo. Que el otro no tenga un evento en curso.").arg(topeRemoto.value),
                                               qsTr("Probar"),
                                               function() {
                                                   const e = panel.cv.probarRemoto(codigo.text, topeRemoto.value, decodificar.modo, decodificar.pct,
                                                                                   salaRemota.checked ? minutosRemota.value : 0)
                                                   panel.avisar(e)
                                                   if (!e) codigo.text = ""
                                               })
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
            Text { text: qsTr("Que otro equipo pruebe este"); color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.cv.estado !== "recibiendo"
                spacing: 8
                Text { text: qsTr("Hasta"); color: Theme.textDim; font.pixelSize: 12 }
                SpinBox { id: cupoRecibir; from: 10; to: panel.cv.topeMax; stepSize: 10; value: 80; editable: true }
                Text { text: qsTr("invitados, durante"); color: Theme.textDim; font.pixelSize: 12 }
                SpinBox { id: minutosRecibir; from: 5; to: 60; stepSize: 5; value: 20; editable: true }
                Text { text: qsTr("minutos"); color: Theme.textDim; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                GlassButton {
                    text: qsTr("Recibir una prueba…")
                    enabled: !panel.cv.ocupado && panel.cv.riesgo === ""
                    onClicked: panel.confirmar(qsTr("¿Dejar que otro equipo pruebe este?"),
                                               qsTr("Se abre una credencial de prueba (vence sola) y entran hasta %1 invitados de prueba desde el otro equipo. La guardia corta si esta máquina se ahoga.").arg(cupoRecibir.value),
                                               qsTr("Recibir"),
                                               function() { panel.verCodigo = false; panel.copiado = false; panel.avisar(panel.cv.empezarARecibir(cupoRecibir.value, minutosRecibir.value)) })
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.cv.estado === "recibiendo"
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: !panel.rec.codigo && !panel.rec.codigoInternet ? qsTr("Abriendo…")
                          : qsTr("Recibiendo hasta las %1: %2 invitados de prueba adentro (%3 con video).")
                                .arg(new Date(panel.rec.venceMs || 0).toLocaleTimeString(Qt.locale(), Locale.ShortFormat))
                                .arg(panel.rec.participantes || 0).arg(panel.rec.conVideo || 0)
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!panel.rec.codigo || !!panel.rec.codigoInternet
                    wrapMode: Text.WrapAnywhere
                    color: Theme.textDim
                    font { family: Theme.mono; pixelSize: 11 }
                    text: panel.verCodigo ? (panel.rec.codigo || panel.rec.codigoInternet)
                                          : qsTr("Código: •••••••• (%1)").arg(panel.rec.url || panel.rec.urlInternet || "")
                }
                RowLayout {
                    spacing: 8
                    GlassButton {
                        compact: true
                        visible: !!panel.rec.codigo
                        text: panel.copiado ? qsTr("Copiado ✓") : qsTr("Copiar el código (red local)")
                        onClicked: { Studio.copiar(panel.rec.codigo); panel.copiado = true }
                    }
                    GlassButton {
                        compact: true
                        visible: !!panel.rec.codigoInternet
                        text: qsTr("Copiar el código (por internet)")
                        onClicked: { Studio.copiar(panel.rec.codigoInternet); panel.copiado = true }
                    }
                    GlassButton {
                        compact: true
                        text: panel.verCodigo ? qsTr("Ocultar") : qsTr("Mostrar")
                        onClicked: panel.verCodigo = !panel.verCodigo
                    }
                    GlassButton {
                        compact: true
                        text: qsTr("Dejar de recibir")
                        onClicked: panel.cv.dejarDeRecibir()
                    }
                }
                // Las que generan contra esta (lo que mandan por el emparejamiento).
                Repeater {
                    model: panel.cv.generadores
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: (modelData.cpuMaquina || 0) > 90 ? Theme.live : Theme.textDim
                        font.pixelSize: 11
                        text: qsTr("Genera %1: CPU %2 % (hinchada %3 %), red sale %4 Mbps")
                              .arg(modelData.maquina).arg(panel.num(modelData.cpuMaquina)).arg(panel.num(modelData.cpuProbador))
                              .arg(panel.num(modelData.redSaleMbps, 1))
                    }
                }
                GraficasPrueba {
                    Layout.fillWidth: true
                    visible: panel.cv.serie.length > 1
                    serie: panel.cv.serie
                }
            }
        }

        // --- Conexión a internet ----------------------------------------------------------------
        Seccion {
            titulo: qsTr("Conexión a internet")
            subtitulo: qsTr("Lo que cuenta es el camino de los invitados: pinggy y el TURN de la conferencia (y el túnel WireGuard, si está). Dice cuántos invitados entran con esta conexión: como mucho 4 en alta definición y el resto en baja.")
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 12
                text: (panel.cv.pinggy.texto || qsTr("Pinggy: sin datos")) + (panel.cv.hayTunel ? " · " + qsTr("hay túnel WireGuard") : "")
            }
            RowLayout {
                spacing: 8
                GlassButton {
                    text: qsTr("Medir la conexión")
                    enabled: !panel.cv.ocupado
                    onClicked: panel.avisar(panel.cv.medirInternet(false))
                }
                GlassButton {
                    visible: panel.cv.pinggy.estado !== "listo"
                    text: qsTr("Prender pinggy y medir…")
                    enabled: !panel.cv.ocupado && panel.cv.riesgo === ""
                    onClicked: panel.confirmar(qsTr("¿Prender pinggy para medir?"),
                                               qsTr("Prender pinggy reinicia la conferencia (unos segundos). Al terminar la medición se apaga solo."),
                                               qsTr("Prender y medir"),
                                               function() { panel.avisar(panel.cv.medirInternet(true)) })
                }
            }
            Text {
                Layout.fillWidth: true
                visible: (panel.net.frase || "") !== ""
                wrapMode: Text.WordWrap
                color: Theme.text
                font { pixelSize: 14; weight: Font.DemiBold }
                text: panel.net.frase || ""
            }
            Repeater {
                model: [{ k: "pinggy", t: qsTr("Por pinggy") }, { k: "wireguard", t: qsTr("Por el túnel WireGuard") }]
                delegate: Text {
                    required property var modelData
                    readonly property var c: panel.net[modelData.k] || ({})
                    readonly property var m: c.medicion || ({})
                    Layout.fillWidth: true
                    visible: c.medicion !== undefined || c.nota !== undefined
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: 11
                    text: modelData.t + ": " + (c.nota ? c.nota
                          : modelData.k === "pinggy"
                            ? qsTr("%1 Mbps ida y vuelta, demora %2 ms (%3 ms cargado), pérdida %4 % → entran %5")
                                  .arg(panel.num((m.kbps || 0) / 1000, 1)).arg(panel.num(m.rttMs)).arg(panel.num(m.rttCargaMs))
                                  .arg(panel.num((m.perdida || 0) * 100, 1)).arg(panel.num((c.cuenta || {}).entranConMargen))
                            : qsTr("subida %1 Mbps, bajada %2 Mbps → entran %3")
                                  .arg(panel.num(((m.subida || {}).kbpsSinPerdida || 0) / 1000, 1))
                                  .arg(panel.num(((m.bajada || {}).kbpsSinPerdida || 0) / 1000, 1))
                                  .arg(panel.num((c.cuenta || {}).entranConMargen)))
                }
            }
        }

        // --- Historial ----------------------------------------------------------------------
        Seccion {
            titulo: qsTr("Historial")
            subtitulo: qsTr("Cada prueba queda guardada con la fecha y la máquina. La revisión previa del evento muestra la última.")
            extra: RowLayout {
                spacing: 6
                GlassButton { compact: true; text: qsTr("Exportar el último…"); onClicked: { exportDlg.csv = false; exportDlg.open() } }
                GlassButton { compact: true; text: qsTr("Exportar el historial (CSV)…"); onClicked: { exportDlg.csv = true; exportDlg.open() } }
            }
            Text {
                visible: panel.cv.historial.length === 0
                color: Theme.textFaint
                font.pixelSize: 12
                text: qsTr("Todavía no hay pruebas guardadas.")
            }
            Repeater {
                model: panel.cv.historial
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: panel.fecha(modelData.fecha); color: Theme.textFaint; font { family: Theme.mono; pixelSize: 11 } Layout.preferredWidth: 130 }
                    Text {
                        text: modelData.tipo === "internet" ? qsTr("conexión") : modelData.tipo === "remoto" ? qsTr("remoto") : modelData.tipo === "recibida" ? qsTr("recibida") : qsTr("este equipo")
                        color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 80
                    }
                    Text { text: modelData.maquina || ""; color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 140 }
                    Text { text: modelData.frase || ""; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
        }
    }

    FileDialog {
        id: exportDlg
        property bool csv: false
        title: csv ? qsTr("Exportar el historial de capacidad") : qsTr("Exportar el último resultado de capacidad")
        fileMode: FileDialog.SaveFile
        defaultSuffix: csv ? "csv" : "json"
        nameFilters: csv ? [qsTr("CSV (*.csv)")] : [qsTr("JSON (*.json)")]
        onAccepted: panel.avisar(panel.cv.exportar(selectedFile.toString()))
    }
}
