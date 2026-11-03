// GENERADO por tools/generar/replica-permitidas.py desde la QML del dashboard: no editar a mano.
// Lo que el tablero web puede llamar (tipo «llamar») y escribir («poner»), con la herramienta
// MCP equivalente (perfil del aparato y confirmación de aire). Ver control/ReplicaPublicador.
R"json([
 {
  "o": "Studio",
  "n": "latidoPoliticaUi",
  "tipo": "llamar",
  "mcp": "supervisor_politica"
 },
 {
  "o": "Studio.alarmas",
  "n": "reconocer",
  "tipo": "llamar",
  "mcp": "alert_reconocer"
 },
 {
  "o": "Studio.alarmas",
  "n": "silenciar",
  "tipo": "llamar",
  "mcp": "alert_reconocer"
 },
 {
  "o": "Studio.alarmas.sonido",
  "n": "ponerAjustesUi",
  "tipo": "llamar",
  "mcp": "alert_reconocer"
 },
 {
  "o": "Studio.alarmas.sonido",
  "n": "probar",
  "tipo": "llamar",
  "mcp": "alert_reconocer"
 },
 {
  "o": "Studio.bancas",
  "n": "agregar",
  "tipo": "llamar",
  "mcp": "banca_cola"
 },
 {
  "o": "Studio.bancas",
  "n": "alAire",
  "tipo": "llamar",
  "mcp": "banca_al_aire"
 },
 {
  "o": "Studio.bancas",
  "n": "siguiente",
  "tipo": "llamar",
  "mcp": "banca_cola"
 },
 {
  "o": "Studio.bancas",
  "n": "terminar",
  "tipo": "llamar",
  "mcp": "banca_cola"
 },
 {
  "o": "Studio.camaras",
  "n": "chequearMovimiento",
  "tipo": "llamar",
  "mcp": "ptz_ir_posicion"
 },
 {
  "o": "Studio.camaras",
  "n": "home",
  "tipo": "llamar",
  "mcp": "ptz_ir_posicion"
 },
 {
  "o": "Studio.camaras",
  "n": "irAPosicion",
  "tipo": "llamar",
  "mcp": "ptz_ir_posicion"
 },
 {
  "o": "Studio.camaras",
  "n": "mover",
  "tipo": "llamar",
  "mcp": "ptz_ir_posicion"
 },
 {
  "o": "Studio.camaras",
  "n": "parar",
  "tipo": "llamar",
  "mcp": "ptz_ir_posicion"
 },
 {
  "o": "Studio.carruseles",
  "n": "actual",
  "tipo": "poner",
  "mcp": "carrusel_armar"
 },
 {
  "o": "Studio.carruseles",
  "n": "consulta",
  "tipo": "poner",
  "mcp": "carrusel_armar"
 },
 {
  "o": "Studio.carruseles",
  "n": "ir",
  "tipo": "llamar",
  "mcp": "carrusel_armar"
 },
 {
  "o": "Studio.conference",
  "n": "admit",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.conference",
  "n": "reject",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.conference",
  "n": "request",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.conference",
  "n": "requestBoardLink",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.conference",
  "n": "requestQr",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.conference",
  "n": "speakerNext",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.conference",
  "n": "speakerOnAir",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.conference.waiting",
  "n": "fila",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.dsk",
  "n": "definirUi",
  "tipo": "llamar",
  "mcp": "dsk_mostrar"
 },
 {
  "o": "Studio.dsk",
  "n": "mostrarUi",
  "tipo": "llamar",
  "mcp": "dsk_mostrar"
 },
 {
  "o": "Studio.dsk",
  "n": "plantillaLsaUi",
  "tipo": "llamar",
  "mcp": "dsk_mostrar"
 },
 {
  "o": "Studio.emision",
  "n": "borrarDestinoUi",
  "tipo": "llamar",
  "mcp": "stream_destino_borrar"
 },
 {
  "o": "Studio.emision",
  "n": "detenerDestinoUi",
  "tipo": "llamar",
  "mcp": "stream_stop"
 },
 {
  "o": "Studio.emision",
  "n": "guardarDestinoUi",
  "tipo": "llamar",
  "mcp": "record_ajustes"
 },
 {
  "o": "Studio.emision",
  "n": "iniciarDestinoUi",
  "tipo": "llamar",
  "mcp": "stream_start"
 },
 {
  "o": "Studio.emision",
  "n": "isoInvitadoUi",
  "tipo": "llamar",
  "mcp": "record_ajustes"
 },
 {
  "o": "Studio.emision",
  "n": "isoTodasUi",
  "tipo": "llamar",
  "mcp": "record_ajustes"
 },
 {
  "o": "Studio.emision",
  "n": "limpiaActivarUi",
  "tipo": "llamar",
  "mcp": "salida_limpia"
 },
 {
  "o": "Studio.emision",
  "n": "resetLoudnessUi",
  "tipo": "llamar",
  "mcp": "audio_loudness_reset"
 },
 {
  "o": "Studio.emision",
  "n": "setRetornoInvitadoUi",
  "tipo": "llamar",
  "mcp": "record_ajustes"
 },
 {
  "o": "Studio.emision",
  "n": "startStreaming",
  "tipo": "llamar",
  "mcp": "stream_start"
 },
 {
  "o": "Studio.emision",
  "n": "stopStreaming",
  "tipo": "llamar",
  "mcp": "stream_stop"
 },
 {
  "o": "Studio.emision",
  "n": "toggleRecording",
  "tipo": "llamar",
  "mcp": "record_start"
 },
 {
  "o": "Studio.escenas",
  "n": "armarEscena",
  "tipo": "llamar",
  "mcp": "scene_preview"
 },
 {
  "o": "Studio.escenas",
  "n": "repetirUi",
  "tipo": "llamar",
  "mcp": "preview_al_aire"
 },
 {
  "o": "Studio.escenas",
  "n": "setAfv",
  "tipo": "llamar",
  "mcp": "audio_afv"
 },
 {
  "o": "Studio.escenas",
  "n": "setTransicionEntrada",
  "tipo": "llamar",
  "mcp": "escena_transicion"
 },
 {
  "o": "Studio.escenas",
  "n": "take",
  "tipo": "llamar",
  "mcp": "preview_al_aire"
 },
 {
  "o": "Studio.escenas",
  "n": "takeCon",
  "tipo": "llamar",
  "mcp": "preview_al_aire"
 },
 {
  "o": "Studio.escenas",
  "n": "takeScene",
  "tipo": "llamar",
  "mcp": "scene_switch"
 },
 {
  "o": "Studio.escenas",
  "n": "tbar",
  "tipo": "llamar",
  "mcp": "scene_preview"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "aplicarTiemposGrabados",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "arrancarRevisando",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "decidirPtz",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "descartarTiemposGrabados",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "ensayo",
  "tipo": "poner",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "go",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "grabarTiempos",
  "tipo": "poner",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "parar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "pausar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "reanudar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "reenganchar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.corridaVm",
  "n": "retroceder",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.evento.informeVm",
  "n": "exportarEscaleta",
  "tipo": "llamar",
  "mcp": "guion_bloque"
 },
 {
  "o": "Studio.evento.informeVm",
  "n": "importarEscaleta",
  "tipo": "llamar",
  "mcp": "guion_bloque"
 },
 {
  "o": "Studio.frames",
  "n": "usar",
  "tipo": "llamar",
  "mcp": null
 },
 {
  "o": "Studio.linea",
  "n": "acercar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "adlib",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "alAhora",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "anterior",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "atajo",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "confirmar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "correr",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "descartar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "estirar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "go",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "hold",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "insertar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "irA",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "mover",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "moverPieza",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "reordenar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "repetir",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "salida",
  "tipo": "poner",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "saltar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "siguiente",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea",
  "n": "usar",
  "tipo": "llamar",
  "mcp": null
 },
 {
  "o": "Studio.linea",
  "n": "zoom",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea.productor",
  "n": "aplicar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.linea.productor",
  "n": "descartar",
  "tipo": "llamar",
  "mcp": "linea_go"
 },
 {
  "o": "Studio.matriz",
  "n": "agregarEntradaVirtual",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.matriz",
  "n": "agregarSalidaVirtual",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.matriz",
  "n": "ajustar",
  "tipo": "llamar",
  "mcp": "audio_matriz_cruce"
 },
 {
  "o": "Studio.matriz",
  "n": "alternar",
  "tipo": "llamar",
  "mcp": "audio_matriz_cruce"
 },
 {
  "o": "Studio.matriz",
  "n": "quitarBus",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.matriz",
  "n": "refrescar",
  "tipo": "llamar",
  "mcp": "audio_matriz"
 },
 {
  "o": "Studio.matriz",
  "n": "renombrar",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.matriz",
  "n": "setAjustes",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.matriz",
  "n": "setCruce",
  "tipo": "llamar",
  "mcp": "audio_matriz_cruce"
 },
 {
  "o": "Studio.matriz",
  "n": "setDispositivo",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.matriz",
  "n": "setSalida",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.matriz",
  "n": "setVisibleDispositivo",
  "tipo": "llamar",
  "mcp": "audio_matriz_equipo"
 },
 {
  "o": "Studio.mezcla",
  "n": "aflUi",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mezcla",
  "n": "balistica",
  "tipo": "poner",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mezcla",
  "n": "medirSincroUi",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mezcla",
  "n": "pflUi",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mezcla",
  "n": "retardoUi",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mezcla",
  "n": "salidaEscucha",
  "tipo": "poner",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mezcla",
  "n": "setRetardoUi",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "abrir",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "agregarMicrofono",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "agregarParlante",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "agregarPunto",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "aplicar",
  "tipo": "llamar",
  "mcp": "calibracion_aplicar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "cancelar",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "medir",
  "tipo": "llamar",
  "mcp": "calibracion_medir"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "mover",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "nueva",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "quitar",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "revertir",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "setObjetivo",
  "tipo": "llamar",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mezclador.calibracion",
  "n": "vista",
  "tipo": "poner",
  "mcp": "calibracion_editar"
 },
 {
  "o": "Studio.mixer",
  "n": "canal",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mixer",
  "n": "rowCount",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mixer",
  "n": "setCompacto",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mixer",
  "n": "setGananciaGrupoDb",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mixer",
  "n": "setMuted",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mixer",
  "n": "setMutedGrupo",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.mixer",
  "n": "setVolumeDb",
  "tipo": "llamar",
  "mcp": "audio_set"
 },
 {
  "o": "Studio.niveles",
  "n": "usar",
  "tipo": "llamar",
  "mcp": null
 },
 {
  "o": "Studio.ordenDelDia",
  "n": "siguienteUi",
  "tipo": "llamar",
  "mcp": "orden_del_dia_punto"
 },
 {
  "o": "Studio.pdfEscena",
  "n": "crearEscena",
  "tipo": "llamar",
  "mcp": "pdf_escena_poner"
 },
 {
  "o": "Studio.pdfEscena",
  "n": "elegir",
  "tipo": "llamar",
  "mcp": "pdf_escena_poner"
 },
 {
  "o": "Studio.pdfEscena",
  "n": "elegirArchivo",
  "tipo": "llamar",
  "mcp": "pdf_escena_poner"
 },
 {
  "o": "Studio.pdfEscena",
  "n": "encuadre",
  "tipo": "llamar",
  "mcp": "pdf_escena_poner"
 },
 {
  "o": "Studio.pdfEscena",
  "n": "irA",
  "tipo": "llamar",
  "mcp": "pdf_escena_poner"
 },
 {
  "o": "Studio.pdfEscena",
  "n": "mover",
  "tipo": "llamar",
  "mcp": "pdf_escena_poner"
 },
 {
  "o": "Studio.pdfEscena",
  "n": "paso",
  "tipo": "llamar",
  "mcp": "pdf_escena_poner"
 },
 {
  "o": "Studio.sala",
  "n": "aprobarInfoInvitado",
  "tipo": "llamar",
  "mcp": "participant_admit"
 },
 {
  "o": "Studio.salidas",
  "n": "activa",
  "tipo": "poner",
  "mcp": "salida_editar"
 },
 {
  "o": "Studio.salidas",
  "n": "ftbUi",
  "tipo": "llamar",
  "mcp": "salida_editar"
 },
 {
  "o": "Studio.salidas",
  "n": "grabar",
  "tipo": "llamar",
  "mcp": "salida_editar"
 },
 {
  "o": "Studio.salidas",
  "n": "placaUi",
  "tipo": "llamar",
  "mcp": "salida_editar"
 },
 {
  "o": "Studio.salidas.lista",
  "n": "fila",
  "tipo": "llamar",
  "mcp": "salida_editar"
 },
 {
  "o": "Studio.seguidor",
  "n": "seguir",
  "tipo": "llamar",
  "mcp": "ptz_seguir"
 },
 {
  "o": "Studio.seguirMicrofono",
  "n": "aceptarUi",
  "tipo": "llamar",
  "mcp": "seguir_microfono"
 },
 {
  "o": "Studio.seguirMicrofono",
  "n": "configurarUi",
  "tipo": "llamar",
  "mcp": "seguir_microfono"
 },
 {
  "o": "Studio.seguirMicrofono",
  "n": "descartarUi",
  "tipo": "llamar",
  "mcp": "seguir_microfono"
 },
 {
  "o": "Studio.seguirMicrofono.estado",
  "n": "accepted",
  "tipo": "poner",
  "mcp": "seguir_microfono"
 },
 {
  "o": "Studio.sources",
  "n": "capa",
  "tipo": "llamar",
  "mcp": "source_update"
 },
 {
  "o": "Studio.sources",
  "n": "rowCount",
  "tipo": "llamar",
  "mcp": "source_update"
 },
 {
  "o": "Studio.sources",
  "n": "setVisible",
  "tipo": "llamar",
  "mcp": "source_update"
 },
 {
  "o": "Studio.voz",
  "n": "setAuriculares",
  "tipo": "llamar",
  "mcp": "audio_set"
 }
])json"
