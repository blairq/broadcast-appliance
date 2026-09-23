// ============================================================================
// src/app/mcp/McpEtiquetas.cpp — ver McpEtiquetas.hpp
// ============================================================================
#include "mcp/McpEtiquetas.hpp"

#include <algorithm>
#include <map>

namespace ba::app::mcp {
namespace {

struct Fila {
    const char* clave;
    const char* terminos; ///< separados por espacios
};

/// Lo que comparte una familia entera. Se aplica por prefijo del nombre, así
/// que `youtube_live_health` recibe lo de `youtube_` sin repetirlo 15 veces.
const Fila kFamilias[] = {
    {"evento_", "evento guion rundown escaleta programa del dia bloque cue orden del evento produccion"},
    {"scene_", "escena escenas placa composicion layout toma camara aire vivo obs"},
    {"source_", "fuente fuentes capa elemento objeto imagen video texto navegador captura obs"},
    {"audio_", "audio sonido volumen nivel ganancia db decibeles mezcla mixer canal pista"},
    {"capture_", "captura pantallazo screenshot foto imagen mirar ver revisar vista previa como se ve"},
    {"room_", "sala videollamada reunion conferencia webrtc invitados lobby"},
    {"participant_", "invitado participante persona asistente publico gente lobby entrar admitir"},
    {"chat_", "chat mensaje texto escribir conversacion comentario"},
    {"share_", "archivo compartido adjunto subida documento foto pdf"},
    {"speaker_", "orador expositor panelista disertante fila cola turno atril escenario"},
    {"speakers_", "orador expositor panelista fila cola turno backstage"},
    {"ui_", "interfaz pantalla ventana app vista pestaña navegar"},
    {"dashboard_", "tablero dashboard tablet panel control remoto botonera"},
    {"youtube_", "youtube yt google transmitir stream vivo directo plataforma red social"},
    {"twitch_", "twitch transmitir stream vivo directo plataforma red social gaming"},
    {"facebook_", "facebook meta fb transmitir stream vivo directo plataforma red social"},
    {"alert_", "alerta aviso notificacion avisame vigilar monitorear disparador trigger"},
    {"assistant_", "asistente ia llm modelo claude copiloto ayudante"},
    {"account_", "cuenta credenciales login acceso usuario clave conectar plataforma"},
    {"accounts_", "cuenta credenciales login acceso plataformas"},
    {"grafico_", "grafico placa zocalo lower third animacion html overlay superposicion"},
    {"rotulo_", "rotulo zocalo nombre cargo placa identificacion lower third"},
    {"log_", "registro log bitacora diagnostico error historial que paso depurar"},
    {"receta_", "receta automatizacion macro secuencia guardada atajo"},
    {"agente_", "agente automatico rol ia bot trabajador delegar encargar"},
    {"prompter_", "teleprompter prompter apuntador discurso texto orador leer guion"},
    {"pantalla_", "pantalla televisor tv monitor chromecast proyector cartel proyectar castear"},
    {"carrusel_", "carrusel rotacion pasos secuencia pantalla cartelera"},
    {"superficie_", "superficie pagina pantalla web canal control telemetria viva"},
    {"qr_", "qr codigo escanear celular enlace wifi contacto vcard"},
    {"media_", "medio archivo video musica audio cancion tema clip pelicula reproducir"},
    {"stream_", "transmitir emitir salir al aire vivo directo rtmp stream broadcast"},
    {"record_", "grabar grabacion guardar archivo mp4 registro"},
    {"settings_", "configuracion ajuste preferencia opcion parametro"},
    {"system_", "sistema equipo maquina hardware version actualizar"},
    {"operator_", "operador humano persona preguntar avisar consultar cabina"},
    {"ext_", "extension plugin modulo complemento"},
    {"ndi_", "ndi red camara remota fuente por red"},
    {"network_", "red ip conexion internet ancho de banda"},
    {"video_", "video camara webcam dispositivo entrada"},
    {"mixer_", "mezcla audio volumen canal pista mixer"},
    {"transition_", "transicion corte fundido disolvencia cambio de escena"},
    {"guest_", "invitado participante persona sala"},
    {"help", "ayuda como se hace no se que herramienta buscar manual documentacion"},
    {"feeds_", "feed señal fuente video vista"},
    {"monitors", "monitor pantalla fisica display escritorio"},
    {"input_", "entrada dispositivo camara microfono captura"},
    {"output_", "salida ventana proyeccion monitor"},
};

/// Lo propio de cada herramienta: lo que la distingue de sus hermanas.
const Fila kPropias[] = {
    // --- Ayuda y estado ---------------------------------------------------
    {"help", "indice catalogo guia empezar"},
    {"help_search", "buscar herramienta encontrar funcion que hay para"},
    {"status", "estado general resumen como esta todo panorama salud"},
    {"security_status", "seguridad contaminacion sesion permisos aprobacion"},
    {"system_info", "version compilacion build acerca de datos del equipo"},
    {"system_hardware", "hardware placa gpu cpu memoria disco capacidad"},
    {"system_update", "actualizar version nueva upgrade instalar"},
    {"app_quit", "cerrar salir apagar terminar el programa"},
    {"app_save", "guardar persistir escribir configuracion"},
    {"monitors", "monitores conectados resolucion"},

    // --- Escenas y fuentes ------------------------------------------------
    {"scene_list", "listar cuales hay ver escenas"},
    {"scene_switch", "cambiar poner al aire pasar a cortar a programa activa"},
    {"scene_create", "crear nueva agregar escena"},
    {"scene_copy", "copiar duplicar clonar escena"},
    {"scene_delete", "borrar eliminar quitar escena"},
    {"scene_rename", "renombrar cambiar nombre escena"},
    {"scene_move", "mover ordenar reordenar posicion escena"},
    {"scene_folder", "carpeta agrupar organizar escenas"},
    {"scene_protect", "proteger bloquear candado no tocar impedir cambios"},
    {"scene_export", "exportar respaldo backup copiar a archivo"},
    {"scene_define", "definir armar escena completa de una plantilla"},
    {"transition_set", "fundido corte disolvencia stinger duracion transicion"},
    {"source_types", "que tipos de fuente hay disponibles"},
    {"source_list", "listar fuentes de una escena que hay adentro"},
    {"source_add", "agregar poner sumar fuente camara imagen texto navegador"},
    {"source_remove", "quitar sacar borrar fuente"},
    {"source_update", "cambiar editar modificar fuente"},
    {"source_properties", "propiedades opciones parametros de una fuente"},
    {"source_set_property", "cambiar una propiedad valor ajuste de fuente"},
    {"guest_source_add", "poner invitado en escena mostrar participante camara remota"},

    // --- Audio -------------------------------------------------------------
    {"audio_set", "silenciar mutear callar bajar subir volumen ganancia mute solo"},
    {"audio_sincro", "sincronizar labios desfase desfasado desincronizado adelantado atrasado "
                     "delay offset demora retardo la voz no coincide con la imagen"},
    {"audio_sources", "que fuentes de audio hay listar canales"},
    {"audio_devices", "placa de sonido microfono dispositivo entrada salida hardware"},
    {"audio_input_add", "agregar microfono entrada de audio linea"},
    {"audio_rack", "rack cadena procesamiento efectos de un canal"},
    {"audio_plantillas", "plantilla preset cadena tipica voz musica"},
    {"audio_modules", "que efectos hay disponibles procesadores"},
    {"audio_module_add", "agregar efecto procesador al canal"},
    {"audio_module_remove", "quitar sacar efecto procesador"},
    {"audio_module_get", "ver parametros de un efecto"},
    {"audio_module_set", "cambiar parametros de un efecto"},
    {"audio_patch", "ruteo patch enviar a donde va matriz"},
    {"audio_bypass", "puentear desactivar saltear efecto comparar"},
    {"audio_gate", "compuerta gate ruido de fondo silencio respiracion"},
    {"audio_compressor", "compresor compresion dinamica nivelar parejo"},
    {"audio_deesser", "deesser sibilancia ese silbido"},
    {"audio_eq", "ecualizador eq graves medios agudos tono frecuencia"},
    {"audio_multiband", "multibanda compresion por bandas"},
    {"audio_limiter", "limitador limite pico clip saturacion proteger"},
    {"audio_analyze", "analizar espectro medir problema ruido zumbido"},
    {"audio_measure", "medir lufs nivel pico rms loudness normalizar"},
    {"audio_generator", "tono generador prueba ruido rosa senal de test"},
    {"audio_outputs", "salidas de audio a donde sale"},
    {"audio_output_set", "cambiar salida de audio destino"},
    {"audio_show", "mostrar abrir panel de audio"},
    {"audio_editor", "editar audio abrir editor"},
    {"mixer_list", "consola mesa canales faders niveles"},

    // --- Transmision y grabacion -------------------------------------------
    {"stream_start", "empezar arrancar salir al aire comenzar transmision"},
    {"stream_stop", "parar terminar cortar bajar del aire fin de transmision"},
    {"record_start", "empezar a grabar arrancar grabacion rec"},
    {"record_stop", "parar grabacion terminar de grabar"},
    {"grabacion_individual", "grabar por separado pista aislada isos cada camara"},

    // --- Ver ---------------------------------------------------------------
    {"capture_program", "que esta al aire que se ve ahora salida programa"},
    {"capture_source", "ver una fuente revisar antes de poner al aire"},
    {"capture_multiview", "todas las escenas de una grilla multiview"},
    {"capture_feed", "ver una señal entrante feed camara"},
    {"capture_person", "ver a una persona invitado camara de alguien cara"},
    {"capture_people", "ver a todos los invitados grilla de personas"},
    {"capture_sequence", "varias capturas seguidas ver que cambia movimiento"},
    {"capture_ui", "ver la interfaz de la app pantalla del operador"},
    {"feeds_list", "que señales hay entrando fuentes de video"},

    // --- Sala --------------------------------------------------------------
    {"room_status", "quien esta en la sala conectados lista de gente"},
    {"room_set", "configurar sala cupo cerrar abrir limite"},
    {"room_pin", "pin clave codigo de acceso contrasena de la sala"},
    {"room_link", "enlace invitacion link para entrar compartir acceso"},
    {"participant_admit", "dejar entrar aceptar admitir aprobar del lobby"},
    {"participant_reject", "rechazar no dejar entrar negar acceso"},
    {"participant_kick", "echar expulsar sacar desconectar de la sala"},
    {"participant_requeue", "devolver al lobby mandar atras volver a la espera"},
    {"participant_admit_all", "dejar entrar a todos admitir todos vaciar lobby"},
    {"participant_talk", "hablarle privado audio al invitado susurrar"},
    {"participant_request", "pedirle algo pedir camara microfono solicitar"},
    {"participant_profile", "ficha datos nombre cargo informacion de la persona"},
    {"chat_send", "mandar mensaje escribir al chat avisar por chat"},
    {"chat_history", "historial conversacion que dijeron leer chat"},
    {"chat_wait", "esperar mensaje quedarse esperando a que escriban"},
    {"chat_overlay_add", "chat en pantalla mostrar comentarios al aire overlay"},
    {"share_list", "que archivos mandaron subidos adjuntos"},
    {"share_view", "ver archivo mirar adjunto abrir subida"},
    {"share_take", "poner archivo al aire mostrar foto documento subido"},
    {"share_delete", "borrar archivo subido eliminar adjunto"},
    {"guest_chat_pending", "mensajes sin responder pendientes de invitados"},
    {"guest_reply_suggest", "sugerir respuesta redactar contestacion borrador"},
    {"network_status", "red internet conexion ancho de banda tunel calidad"},
    {"gateway_restart", "reiniciar sala gateway reiniciar servidor de la sala"},
    {"operator_feed", "camara del operador cabina mi camara"},

    // --- Oradores ----------------------------------------------------------
    {"speakers_status", "quien esta en la fila estado de oradores"},
    {"speakers_config", "configurar fila de oradores tiempos reglas"},
    {"speaker_on_air", "poner orador al aire darle el aire a quien habla"},
    {"speaker_next", "siguiente orador pasar al que sigue avanzar la fila"},
    {"speaker_move", "mover en la fila reordenar oradores"},
    {"speaker_queue", "fila cola de oradores agregar sacar"},
    {"speaker_presenter", "presentador conductor moderador anfitrion"},
    {"speaker_time", "tiempo cronometro reloj cuanto le queda al orador"},

    // --- Interfaz ----------------------------------------------------------
    {"ui_show_page", "ir a la pestaña abrir seccion navegar mostrar pagina"},
    {"ui_page_window", "abrir en ventana aparte sacar a otra ventana"},
    {"ui_notify", "avisar en pantalla toast mensaje al operador"},
    {"ui_barra", "barra lateral menu navegacion"},
    {"output_window", "ventana de salida proyeccion pantalla completa"},
    {"dashboard_list", "tableros que hay tablet"},
    {"dashboard_card", "boton tarjeta del tablero agregar accion"},
    {"dashboard_background", "fondo del tablero imagen color"},

    // --- Plataformas -------------------------------------------------------
    {"youtube_status", "estado de youtube conectado"},
    {"youtube_setup", "configurar youtube api credenciales"},
    {"youtube_videos", "videos subidos lista del canal"},
    {"youtube_video_upload", "subir video a youtube publicar archivo"},
    {"youtube_video_update", "editar titulo descripcion de un video"},
    {"youtube_video_delete", "borrar video de youtube"},
    {"youtube_live_create", "crear transmision programar vivo en youtube"},
    {"youtube_live_start", "empezar vivo en youtube salir al aire"},
    {"youtube_live_end", "terminar vivo en youtube cortar"},
    {"youtube_live_health", "salud de la transmision calidad problemas youtube"},
    {"youtube_chat", "chat de youtube comentarios del vivo"},
    {"twitch_channel", "canal de twitch titulo categoria juego"},
    {"twitch_live_start", "empezar vivo en twitch"},
    {"twitch_live_stop", "terminar vivo en twitch"},
    {"twitch_stream_status", "estado del vivo en twitch espectadores"},
    {"twitch_chat", "chat de twitch"},
    {"facebook_pages", "paginas de facebook cuales hay"},
    {"facebook_live_create", "crear vivo en facebook"},
    {"facebook_live_publish", "publicar empezar vivo en facebook"},
    {"facebook_live_end", "terminar vivo en facebook"},
    {"facebook_chat", "chat de facebook comentarios"},
    {"accounts_status", "que cuentas hay conectadas plataformas"},
    {"account_setup_guide", "como conectar una cuenta pasos guia"},
    {"account_open", "abrir la pagina de la cuenta en el navegador"},
    {"account_credentials_request", "pedir credenciales al operador clave token"},
    {"account_check", "probar cuenta verificar que ande"},

    // --- Alertas y recetas --------------------------------------------------
    {"alert_types", "que se puede vigilar eventos disponibles"},
    {"alert_create", "avisame cuando crear alerta vigilar"},
    {"alert_list", "que alertas hay configuradas"},
    {"alert_update", "cambiar alerta editar"},
    {"alert_delete", "borrar alerta quitar aviso"},
    {"alert_events", "que paso historial de alertas disparos"},
    {"alert_wait", "esperar a que pase algo quedarse esperando evento"},
    {"receta_listar", "que recetas hay automatizaciones guardadas"},
    {"receta_guardar", "guardar receta automatizar secuencia"},
    {"receta_borrar", "borrar receta"},
    {"receta_correr", "correr receta ejecutar automatizacion"},

    // --- Asistente y agentes -------------------------------------------------
    {"assistant_status", "estado del asistente que modelo motor"},
    {"assistant_config", "configurar asistente modelo motor clave"},
    {"assistant_install", "instalar asistente descargar motor"},
    {"assistant_run", "correr asistente pedirle algo"},
    {"assistant_send", "mandarle texto al asistente escribirle"},
    {"agente_roles", "que roles hay agentes disponibles"},
    {"agente_configurar", "configurar agente motor modelo prender apagar rol"},
    {"agente_encargar", "encargarle algo a un agente delegar pedirle"},
    {"agente_corridas", "que hicieron los agentes historial trabajos"},
    {"agente_corrida", "detalle de una corrida paso a paso que penso"},
    {"agente_detener", "parar agente cancelar trabajo"},

    // --- Entradas ------------------------------------------------------------
    {"video_devices", "camaras webcams capturadoras dispositivos de video"},
    {"video_input_add", "agregar camara webcam capturadora"},
    {"network_input_add", "agregar camara ip rtsp srt stream entrante url"},
    {"ndi_sources", "que fuentes ndi hay en la red"},
    {"ndi_input_add", "agregar fuente ndi camara por red"},
    {"input_status", "estado de las entradas que esta llegando"},
    {"ext_listar", "que extensiones hay plugins disponibles"},
    {"ext_prender", "activar extension plugin prender"},

    // --- Medios ---------------------------------------------------------------
    {"media_list", "que archivos hay musica videos biblioteca"},
    {"media_find", "buscar archivo cancion video por nombre"},
    {"media_view", "ver miniatura previsualizar archivo"},
    {"media_use", "poner musica reproducir cancion video al aire cortina"},

    // --- Operador ------------------------------------------------------------
    {"operator_message", "avisarle al operador decirle mandarle un mensaje"},
    {"operator_ask", "preguntarle al operador consultar pedir permiso confirmar"},
    {"operator_messages", "que dijo el operador leer sus mensajes"},

    // --- Graficos y rotulos ---------------------------------------------------
    {"grafico_listar", "que graficos hay placas disponibles"},
    {"grafico_probar", "probar grafico previsualizar antes de sacarlo"},
    {"grafico_mostrar", "poner grafico al aire sacar placa zocalo"},
    {"grafico_ocultar", "sacar grafico bajar placa esconder"},
    {"grafico_en_escena", "grafico dentro de una escena agregar"},
    {"grafico_repertorio", "catalogo de graficos plantillas disponibles"},
    {"rotulo_vinculos", "que rotulos estan vinculados"},
    {"rotulo_vincular", "vincular rotulo a una persona nombre y cargo"},
    {"rotulo_desvincular", "desvincular rotulo quitar"},

    // --- Registro --------------------------------------------------------------
    {"log_leer", "ver el registro que paso errores diagnosticar buscar en el log"},
    {"log_nivel", "cambiar nivel de registro detalle debug traza"},
    {"log_marcar", "marcar en el registro poner una marca hito"},
    {"logs_tail", "ultimas lineas del log cola seguir el registro"},

    // --- Teleprompter -----------------------------------------------------------
    {"prompter_estado", "estado del teleprompter quien esta conectado"},
    {"prompter_documentos", "que discursos hay textos guardados"},
    {"prompter_leer", "leer un discurso ver el texto"},
    {"prompter_escribir", "escribir discurso guardar texto del orador"},
    {"prompter_cargar", "poner un texto en el teleprompter cargar discurso"},
    {"prompter_control", "arrancar parar velocidad del teleprompter avanzar"},

    // --- Pantallas ---------------------------------------------------------------
    {"pantalla_listar", "que televisores hay pantallas conocidas estado"},
    {"pantalla_buscar", "buscar televisores en la red descubrir chromecast"},
    {"pantalla_mostrar", "mandar a la tv proyectar castear mostrar en pantalla qr mensaje reloj"},
    {"pantalla_fijar", "dejar fijo que siempre muestre lo mismo permanente"},
    {"pantalla_renombrar", "ponerle nombre a un televisor renombrar"},
    {"pantalla_agregar", "agregar televisor a mano por ip"},
    {"pantalla_parar", "dejar de mostrar apagar la proyeccion cortar el casteo"},
    {"carrusel_listar", "que carruseles hay rotaciones"},
    {"carrusel_armar", "armar carrusel rotacion de pantallas pasos"},
    {"superficie_listar", "que paginas estan vivas realmente mostrando telemetria retardo"},
    {"superficie_mostrar", "cambiar lo que muestra sin recastear empujar url recargar"},

    // --- QR -----------------------------------------------------------------------
    {"qr_listar", "que codigos qr hay guardados tipos iconos"},
    {"qr_generar", "generar codigo qr crear sin guardar probar"},
    {"qr_guardar", "guardar codigo qr en la biblioteca"},
    {"qr_borrar", "borrar codigo qr"},
    {"qr_exportar", "exportar qr a archivo png svg imprimir"},

    // --- Evento ---------------------------------------------------------------------
    {"evento_listar", "que eventos hay guiones guardados"},
    {"evento_ver", "ver el guion del evento leer rundown"},
    {"evento_abrir", "abrir evento cargar guion"},
    {"evento_bloque", "bloque del guion seccion agregar mover"},
    {"evento_cue", "cue paso accion del guion agregar editar"},
    {"evento_guardar", "guardar el guion del evento"},
    {"evento_revisar", "revisar guion antes buscar problemas validar"},
    {"evento_ensayo_rapido", "ensayo acelerado probar el guion sin tocar el aire"},
    {"evento_informe", "informe del evento tiempos reales cuanto duro"},
    {"evento_correr", "correr el guion arrancar avanzar pausar el evento"},

    {"settings_list", "que se puede configurar opciones disponibles"},
    {"settings_set", "cambiar una configuracion ajustar opcion"},
};

const std::map<std::string, std::vector<std::string>>& tabla() {
    static const std::map<std::string, std::vector<std::string>> t = [] {
        std::map<std::string, std::vector<std::string>> m;
        const auto partir = [](const char* s) {
            std::vector<std::string> out;
            std::string cur;
            for (const char* p = s; *p != 0; ++p) {
                if (*p == ' ') {
                    if (!cur.empty()) out.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(*p);
                }
            }
            if (!cur.empty()) out.push_back(cur);
            return out;
        };
        for (const Fila& f : kPropias) m[f.clave] = partir(f.terminos);
        return m;
    }();
    return t;
}

} // namespace

std::vector<std::string> etiquetasDe(const std::string& nombre) {
    std::vector<std::string> out;
    for (const Fila& f : kFamilias) {
        const std::string clave = f.clave;
        if (nombre.compare(0, clave.size(), clave) != 0) continue;
        std::string cur;
        for (const char* p = f.terminos; ; ++p) {
            if (*p == ' ' || *p == 0) {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
                if (*p == 0) break;
            } else {
                cur.push_back(*p);
            }
        }
    }
    if (const auto it = tabla().find(nombre); it != tabla().end())
        out.insert(out.end(), it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace ba::app::mcp
