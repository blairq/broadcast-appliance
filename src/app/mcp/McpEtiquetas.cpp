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
    {"audio_", "audio sonido volumen nivel ganancia db decibeles mezcla mixer canal pista "
               "se escucha se oye suena"},
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
    {"rostro_", "rostro cara persona quien es reconocer identificar nombre cargo libreta "
                "caras conocidas quien llego quien esta"},
    {"ventana_", "ventana monitor pantalla escritorio mover acomodar ordenar posicion tamaño "
                 "multimonitor dos monitores segundo monitor puesto de trabajo"},
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
    {"system_hardware", "llenando llena disco almacenamiento espacio queda lugar se esta llenando el disco disco lleno espacio libre en disco cuanto "
                        "queda de disco memoria temperatura hardware placa gpu cpu capacidad"},
    {"system_update", "actualizar version nueva upgrade instalar"},
    {"app_quit", "cerrar salir apagar terminar el programa"},
    {"app_save", "guardar persistir escribir configuracion"},
    {"monitors", "monitores conectados resolucion"},

    // --- Escenas y fuentes ------------------------------------------------
    {"scene_list", "cuantas escenas hay listado de escenas listar cuales hay ver escenas"},
    // «Cortá a la cámara 2» es CAMBIAR DE ESCENA: en una cabina las escenas se
    // llaman por su cámara. Sin la palabra «camara» aca, esa frase se iba a las
    // doce herramientas que sí la tienen (capture_feed, ndi_sources...) y el
    // cambio de escena no aparecia por ningun lado.
    {"scene_switch", "corta cortala corte pasa poné pone cambiar poner al aire pasar a cortar a programa activa corte directo urgencia "
                     "cortar a camara pasar a camara ir a camara poner la camara "
                     "pone al aire la pone al aire poner al aire la escena arranca con"},
    {"scene_preview", "dejar preparada dejala lista preparar la proxima armar la que sigue armar preparar el proximo plano bus de preview previo antes de sacarlo "
                      "cuear siguiente escena mesa switcher"},
    {"scene_take", "sacar al aire lo armado sacar la armada tomar el take ya take sacar al aire la armada cortar ahora disparar el plano preparado mesa switcher"},
    {"scene_create", "crear nueva agregar escena"},
    {"scene_copy", "copiar duplicar clonar escena"},
    {"scene_delete", "borrar eliminar quitar escena"},
    {"scene_rename", "renombrar cambiar nombre escena"},
    {"scene_move", "mover ordenar reordenar posicion escena"},
    {"scene_folder", "carpeta agrupar organizar escenas"},
    {"scene_protect", "proteger bloquear candado no tocar impedir cambios"},
    {"scene_export", "exportar respaldo backup copiar a archivo"},
    {"scene_define", "definir armar escena completa de una plantilla"},
    {"transition_set", "que fundido esta puesto que transicion hay puesta cual es el fundido fundido corte disolvencia stinger duracion transicion"},
    {"source_types", "que tipos de fuente hay disponibles"},
    {"source_list", "que hay adentro de la escena que tiene la escena que capas tiene contenido de la escena listar fuentes de una escena que hay adentro"},
    {"source_add", "agrega la camara agrega a esta escena sumar una camara agregar a la escena sumar a la escena meter en la escena poner en la escena agregar poner sumar fuente camara imagen texto navegador"},
    {"source_remove", "quitar sacar borrar fuente"},
    {"source_update", "cambiar editar modificar fuente"},
    {"source_properties", "propiedades opciones parametros de una fuente"},
    {"source_set_property", "cambiar una propiedad valor ajuste de fuente"},
    {"guest_source_add", "poner invitado en escena mostrar participante camara remota"},

    // --- Audio -------------------------------------------------------------
    {"audio_set", "acople acoplando pitido cortar el audio bajalo ya urgente ""silenciar mutear callar bajar subir volumen ganancia mute solo "
                  // Como se queja alguien en cabina: no dice «audio_set», dice
                  // que no se escucha. Sin estas palabras, «el invitado no se
                  // escucha» no traia NADA de audio entre los primeros.
                  "no se escucha no se oye sin sonido no tengo sonido no suena "
                  "esta mudo quedo mudo no le sale audio mas fuerte mas bajo"},
    {"audio_sincro", "desfasado de la imagen no coincide con la imagen labios imagen adelantada sincronizar labios desfase desfasado desincronizado adelantado atrasado "
                     "delay offset demora retardo la voz no coincide con la imagen"},
    {"audio_sources", "que fuentes de audio hay listar canales"},
    {"audio_escuchar", "acople de donde viene el pitido aislar canal ""pfl solo escuchar previo monitorear probar un microfono antes de abrirlo "
                       "auriculares cascos oir sin sacar al aire"},
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
    {"audio_gate", "el microfono queda abierto se escucha todo lo de alrededor micro abierto compuerta gate ruido de fondo silencio respiracion"},
    {"audio_compressor", "compresor compresion dinamica nivelar parejo"},
    {"audio_deesser", "deesser sibilancia ese silbido"},
    {"audio_eq", "ecualizador eq graves medios agudos tono frecuencia"},
    {"audio_multiband", "multibanda compresion por bandas"},
    {"audio_limiter", "limitador limite pico clip saturacion proteger"},
    {"audio_analyze", "analizar espectro medir problema ruido zumbido"},
    // ACOPLE, PITIDO, ZUMBIDO: lo que se grita en cabina. Ninguna de estas
    // palabras estaba, y «hay acople» no traia NADA de audio. Es el caso que
    // menos tiempo da para pensar, asi que es el que mejor tiene que andar.
    {"audio_measure", "no tengo sonido sin sonido no sale audio quedamos sin audio pitido chillido silbido agudo insoportable suena un pitido no se escucha nada al aire quedamos sin audio acople acoplando acoplamiento microfonia feedback pitido chillido "
                      "zumbido ruido de fondo satura saturando satura el audio distorsiona "
                      "medir niveles pico ""medir lufs nivel pico rms loudness normalizar"},
    {"audio_generator", "tono generador prueba ruido rosa senal de test"},
    {"audio_outputs", "salidas de audio a donde sale"},
    {"audio_output_set", "cambiar salida de audio destino"},
    {"audio_show", "mostrar abrir panel de audio"},
    {"audio_editor", "editar audio abrir editor"},
    {"mixer_list", "consola mesa canales faders niveles"},

    // --- Transmision y grabacion -------------------------------------------
    {"stream_start", "salimos al aire salgamos al aire arrancamos la transmision empezar a transmitir empezar arrancar salir al aire comenzar transmision"},
    {"stream_stop", "parar terminar cortar bajar del aire bajanos del aire bajalo del aire "
                    "sacanos del aire cortar la transmision fin de transmision"},
    {"record_start", "empezar a grabar arrancar grabacion rec"},
    {"record_stop", "para frena detene la grabacion cortar de grabar parar grabacion para la grabacion parar de grabar frena la grabacion "
                    "cortar la grabacion terminar de grabar"},
    {"grabacion_individual", "grabar por separado pista aislada isos cada camara"},

    // --- Ver ---------------------------------------------------------------
    {"capture_program", "quedamos sin imagen no hay imagen pantalla en blanco se fue la imagen que esta al aire que se ve ahora salida programa"},
    {"capture_source", "ver una fuente revisar antes de poner al aire"},
    {"capture_multiview", "todas las escenas de una grilla multiview"},
    {"capture_feed", "se congelo la camara del invitado su camara quedo trabada imagen congelada ver una señal entrante feed camara"},
    {"capture_person", "ver a una persona invitado camara de alguien cara"},
    {"capture_people", "ver a todos los invitados grilla de personas"},
    {"capture_sequence", "varias capturas seguidas ver que cambia movimiento"},
    {"capture_ui", "ver la interfaz de la app pantalla del operador"},
    {"feeds_list", "que camaras hay conectadas que señales llegan que señales hay entrando fuentes de video"},

    // --- Sala --------------------------------------------------------------
    {"room_status", "se cayo un invitado se desconecto se fue quien quedo quien esta en la sala conectados lista de gente cuanta gente "
                    "cuantos hay cuantos son quienes estan esperando en el lobby "
                    "estado de la sala"},
    {"room_set", "configurar sala cupo cerrar abrir limite"},
    {"room_pin", "pin clave codigo de acceso contrasena de la sala"},
    {"room_link", "enlace invitacion link para entrar compartir acceso"},
    {"participant_admit", "hace pasar hacelo pasar que pase dejalo entrar adentro dejar entrar aceptar admitir aprobar del lobby"},
    {"participant_reject", "rechazar no dejar entrar negar acceso"},
    {"participant_kick", "echar expulsar sacar desconectar de la sala"},
    {"participant_requeue", "devolver al lobby mandar atras volver a la espera"},
    {"participant_admit_all", "dejar entrar a todos admitir todos vaciar lobby"},
    {"participant_talk", "hablarle privado audio al invitado susurrar"},
    {"participant_request", "pedile que prenda encienda apague que active su camara su microfono pedirle algo pedir camara microfono solicitar"},
    {"participant_profile", "ficha datos nombre cargo informacion de la persona"},
    {"chat_send", "mandale un mensaje escribile decile por chat avisale mandar mensaje escribir al chat avisar por chat"},
    {"chat_history", "que me dijo que dijo que escribio leer lo que mandaron que contesto historial conversacion que dijeron leer chat"},
    {"chat_wait", "esperar mensaje quedarse esperando a que escriban"},
    {"chat_overlay_add", "chat en pantalla mostrar comentarios al aire overlay"},
    {"share_list", "que archivos mandaron subidos adjuntos"},
    {"share_view", "ver archivo mirar adjunto abrir subida"},
    {"share_take", "poner archivo al aire mostrar foto documento subido"},
    {"share_delete", "borrar archivo subido eliminar adjunto"},
    {"guest_chat_pending", "mensajes sin responder pendientes de invitados"},
    {"guest_reply_suggest", "sugerir respuesta redactar contestacion borrador"},
    {"network_status", "se esta cortando se corta entrecortado se cae la transmision va lento red internet conexion ancho de banda tunel calidad"},
    {"gateway_restart", "reiniciar sala gateway reiniciar servidor de la sala"},
    {"operator_feed", "camara del operador cabina mi camara"},

    // --- Oradores ----------------------------------------------------------
    {"speakers_status", "quien esta en la fila estado de oradores"},
    {"speakers_config", "configurar fila de oradores tiempos reglas"},
    {"speaker_on_air", "poner orador al aire darle el aire a quien habla"},
    {"speaker_next", "pasa al que sigue el siguiente orador proximo orador turno siguiente siguiente orador pasar al que sigue avanzar la fila"},
    {"speaker_move", "mover en la fila reordenar oradores"},
    {"speaker_queue", "fila cola de oradores agregar sacar"},
    {"speaker_presenter", "presentador conductor moderador anfitrion"},
    {"speaker_time", "tiempo cronometro reloj cuanto le queda al orador"},

    // --- Interfaz ----------------------------------------------------------
    {"ui_show_page", "ir a la pestaña abrir seccion navegar mostrar pagina personas rostros"},
    {"ui_page_window", "abrir en ventana aparte sacar a otra ventana"},
    {"media_list", "que archivos hay musica videos imagenes presentaciones que tengo en medios"},
    {"media_find", "buscar un archivo de medios encontrar el logo donde esta ese video"},
    {"ui_panel_window", "sacar el padron o las preguntas o la urna a su ventana panel en otro "
                        "monitor ventana dedicada para moderar"},
    {"ui_notify", "avisar en pantalla toast mensaje al operador"},
    {"ui_barra", "barra lateral menu navegacion"},
    {"output_window", "ventana de salida proyeccion pantalla completa"},
    {"dashboard_list", "tableros que hay tablet"},
    {"dashboard_card", "boton tarjeta del tablero agregar accion direccion al aire armada take "
                       "tiempos del guion desvio"},
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
    // SIN LOS SINTOMAS. Tenia «se cayo la transmision» y «disco lleno» como
    // ejemplos de lo que se puede vigilar, y con eso ganaba las preguntas de
    // cuando esas cosas ESTAN PASANDO —que son de `status` y `system_hardware`,
    // no de leer un catalogo de tipos—.
    {"alert_types", "que se puede vigilar tipos de vigilancia eventos disponibles parametros"},
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
    {"agente_configurar", "configurar agente motor modelo prender apagar rol darle consola a un rol "
                          "permitirle disco e internet herramientas propias del agente"},
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
    {"media_use", "musica cancion tema pista de audio video clip poner musica poner musica de fondo musica de fondo poner una cancion un video poner musica reproducir cancion video al aire cortina"},

    // --- Operador ------------------------------------------------------------
    {"operator_message", "avisarle al operador decirle mandarle un mensaje"},
    {"operator_ask", "preguntale al de cabina consultale al operador pedile permiso al humano preguntarle al operador consultar pedir permiso confirmar"},
    {"operator_messages", "que dijo el operador leer sus mensajes"},

    // --- Graficos y rotulos ---------------------------------------------------
    {"grafico_listar", "que graficos hay placas disponibles"},
    {"grafico_probar", "probar grafico previsualizar antes de sacarlo"},
    {"grafico_mostrar", "poner grafico al aire sacar placa zocalo"},
    {"grafico_ocultar", "sacar grafico bajar placa esconder"},
    {"grafico_en_escena", "grafico dentro de una escena agregar"},
    {"grafico_repertorio", "catalogo de graficos plantillas disponibles"},
    {"rotulo_vinculos", "que rotulos estan vinculados"},
    {"rotulo_vincular", "vincular rotulo a una persona nombre y cargo "
                        "poner el nombre en pantalla mostrar quien habla "
                        "identificar a quien esta al aire placa con el nombre"},
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
    {"pantalla_mostrar", "mandar a la tv proyectar castear mostrar en pantalla qr mensaje reloj "
                         "tally luz roja camara al aire"},
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
    {"evento_ver", "que sigue en el guion que viene ahora proximo bloque proximo cue ver el guion del "
                   "evento leer rundown cuando es el evento que dia a que hora arranca"},
    {"evento_abrir", "abrir evento cargar guion"},
    {"evento_bloque", "bloque del guion seccion agregar mover"},
    {"evento_cue", "cue paso accion del guion agregar editar"},
    {"evento_guardar", "guardar el guion del evento ponerle fecha hora y huso horario cambiarle el titulo"},
    {"evento_revisar", "revisar guion antes buscar problemas validar"},
    {"evento_ensayo_rapido", "ensayo acelerado probar el guion sin tocar el aire"},
    // «Informe» y «acta» son dos cosas y el que pide una no quiere la otra: el
    // informe es de tiempos (para el operador) y el acta es de gente y
    // decisiones (para el que organizo).
    {"evento_informe", "informe del evento tiempos reales cuanto duro desvios de cada bloque"},
    // Sin «quorum» a secas: preguntar si HAY quorum es algo que se pregunta
    // ANTES de votar y lo contesta el padron. El acta lo lleva adentro, pero
    // nadie pide un acta para saber si puede empezar.
    {"evento_acta", "acta de la reunion quienes asistieron que se decidio que se voto "
                    "minuta del evento documento de la asamblea"},
    {"evento_armar", "armar un evento entero crear el evento con sus bloques y su gente "
                     "organizar una asamblea preparar una reunion de cero"},
    {"evento_capitulos", "capitulos de la grabacion marcas de tiempo para la descripcion del video "
                         "cortar el video por bloque timestamps de youtube"},
    // --- El padron: la gente del evento -----------------------------------
    // «Quienes estan» es LA pregunta de un evento hibrido, y las palabras con
    // que se la hace no se parecen al nombre de la herramienta: se pregunta por
    // los que llegaron, los que faltan y el quorum, nunca por «el padron».
    //
    // Ojo con «invitado»: en la sala un invitado es el que entra por webrtc
    // (participant_*), y en un evento es el de la lista. Va en las dos, y lo
    // que desempata son las palabras de al lado —«lista», «evento», «anotar»—.
    {"padron_ver", "quienes estan cuantos llegaron cuantos faltan quorum lista de asistentes "
                   "padron del evento presentes ausentes quien vino quien falta invitados del evento "
                   "a quien dejar pasar solo sin aprobar quienes entran solos"},
    {"padron_anotar", "anotar a alguien en la lista del evento sumar invitado agregar persona al padron "
                      "dar de alta un asistente panelista orador prensa que entre solo sin aprobar"},
    {"padron_cambiar", "cambiarle los datos a alguien del padron editar asistente papel canal estado "
                       "engancharle la ficha de rostro que entre sola sin que yo apruebe darle "
                       "permiso para entrar solo dejarla pasar sin aprobacion"},
    {"padron_acreditar", "acreditar marcar que llego registrar la entrada de alguien check in "
                         "llego se fue ya esta aca entro por la puerta salida"},
    // --- Las preguntas del publico ----------------------------------------
    // Ojo con «pregunta»: el asistente pregunta, el operador pregunta y el
    // publico pregunta. Lo que desempata son las palabras de al lado —publico,
    // cola, moderar, al aire— y por eso las etiquetas las llevan siempre.
    {"preguntas_ver", "preguntas del publico que pregunto la gente cola de preguntas ver las "
                      "preguntas que mandaron moderar cual sigue cual es la proxima pregunta"},
    {"pregunta_moderar", "elegir una pregunta sacarla al aire darla por respondida descartar "
                         "pregunta poner al aire la pregunta siguiente pregunta limpiar la cola"},
    {"pregunta_sumar", "meter una pregunta en la cola sumar lo que preguntaron por el chat "
                       "agregar pregunta de youtube del salon a viva voz"},
    // Votar: «votacion» es formal y «encuesta» es enganche. Las dos palabras
    // van en las dos herramientas porque quien pide no siempre sabe cual
    // quiere, y la ayuda de la herramienta se encarga de explicarle la
    // diferencia.
    {"votacion_abrir", "abrir una votacion poner algo a votar hacer una encuesta al publico "
                       "votemos si aprobamos preguntar y que voten levantar la mano"},
    {"votacion_ver", "como va la votacion resultado de la encuesta cuantos votaron quienes "
                     "faltan votar cerrar la votacion quorum del voto"},
    {"publico_pagina", "enlace y qr para que el publico pregunte pagina del publico codigo qr para "
                       "proyectar titulo y fondo de la pagina del publico"},
    {"invitacion", "la invitacion de alguien enlace personal para invitar mandarle el qr a una "
                   "persona convocar a un invitado su codigo para acreditarse"},
    {"invitado_chequeo", "estan listos los que esperan probar camara y microfono antes prueba "
                         "tecnica previa se escucha bien el invitado esta listo para salir"},
    {"padron_puerta", "camara de la entrada acreditar solo a quien llega al lugar reconocer a los que "
                      "llegan al salon puerta acreditacion presencial mostrador de entrada"},
    {"padron_quitar", "sacar a alguien de la lista del evento borrar asistente del padron"},
    {"evento_correr", "correr el guion arrancar avanzar pausar el evento"},

    // --- Ventanas -------------------------------------------------------------------
    {"ventana_listar", "que ventanas hay abiertas donde esta cada una en que monitor geometria"},
    {"ventana_mover", "mover ventana a otro monitor acomodar pantalla completa maximizar "
                      "mitad izquierda derecha organizar el escritorio armar el puesto"},
    {"ventana_cerrar", "cerrar ventana quitar sacar de pantalla"},
    {"participant_admit", "admitir hacer pasar dejar entrar aceptar al invitado que espera "
                          "sacarlo del lobby meterlo a la sala"},
    {"participant_reject", "rechazar no dejar entrar negar la entrada echar del lobby"},
    {"participant_kick", "echar sacar de la sala expulsar desconectar a alguien que ya entro"},
    {"imagen_analizar", "mirar la imagen que se ve ahi que hay en camara revisar visualmente "
                        "analizar captura vision hay alguien cuadro negro se lee el rotulo"},
    {"audio_plugin_buscar", "que filtros hay que plugins tengo hay algun filtro para buscar "
                            "un efecto autotune reverb eco delay coro que la musica baje cuando "
                            "hablan ducking siseo sincronizar audio con imagen mas de doscientos"},
    {"rostro_ver", "quien esta en camara quien aparece reconocer a alguien identificar cara"},
    {"rostro_guardar", "dar de alta una persona ensenarle una cara agregar al sistema corregir ficha"},
    {"rostro_listar", "que personas hay guardadas libreta conocidas"},
    {"rostro_borrar", "borrar una persona olvidar una cara sacar de la libreta"},
    {"rostro_calidad", "sirve esta cara que le pido acercarse mirar de frente luz borrosa "
                       "enfoque alta guiada invitado webrtc no me reconoce"},
    {"rostro_sumar", "sumarle otra cara pose angulo mejorar el reconocimiento ensenarle mas "
                     "girar la cabeza que se mueva"},
    {"rostro_pendientes", "sin nombre desconocidos anotados solos completar fichas quien es este"},
    {"rostro_fusionar", "juntar dos fichas duplicada misma persona dos veces unir"},
    {"rostro_autoalta", "anotar solo automatico registrar a todos los que pasen interruptor "
                        "switch dar de alta sola lobby los que llegan"},
    {"rostro_lobby", "quien espera quien llego quien esta en el lobby reconocer a los que entran "
                     "sala de espera identificar antes de que entre "
                     // Al reescribir su descripcion para que no compitiera con
                     // admitir, perdio las palabras que la encontraban. Las
                     // etiquetas no dependen de como quedo redactada.
                     "no me acuerdo como se llama quien es el que espera como se llama el que llego"},
    {"ventanas_ordenar", "ordenar acomodar las ventanas repartir entre monitores que no se "
                         "amontonen ordena las ventanas armar el puesto"},
    {"monitor_identificar", "que pantalla es cual cual es cada monitor identificar las pantallas identificar pantallas cual es cada monitor numero nombre "
                            "cual es el de la izquierda mostrar el numero"},
    {"monitors", "monitores conectados resolucion cuantas pantallas hay segundo monitor"},

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
        // SE SUMAN, no se pisan. Estaba con `=`, asi que dos entradas para la
        // misma herramienta hacian que la segunda BORRARA a la primera, en
        // silencio: quien agregaba etiquetas creyendo que sumaba, restaba.
        // (Habia cuatro asi: monitors, participant_admit/reject/kick.)
        for (const Fila& f : kPropias) {
            std::vector<std::string> t = partir(f.terminos);
            auto& dest = m[f.clave];
            dest.insert(dest.end(), t.begin(), t.end());
        }
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
