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
    {"control_web_", "control web dashboard consola navegador tablet notebook aparato emparejar qr perfil permisos red local"},
    {"youtube_", "youtube yt google transmitir stream vivo directo plataforma red social"},
    {"twitch_", "twitch transmitir stream vivo directo plataforma red social gaming"},
    {"facebook_", "facebook meta fb transmitir stream vivo directo plataforma red social"},
    {"alert_",
     "alerta aviso notificacion avisame vigilar vigilancia monitorear disparador disparadores trigger"},
    {"assistant_", "asistente ia llm modelo claude copiloto ayudante"},
    {"account_", "cuenta credenciales login acceso usuario clave conectar plataforma"},
    {"accounts_", "cuenta credenciales login acceso plataformas"},
    {"grafico_", "grafico placa zocalo lower third animacion html overlay superposicion"},
    {"rotulo_", "rotulo zocalo nombre cargo placa identificacion lower third"},
    {"log_", "registro log bitacora diagnostico error historial que paso depurar"},
    {"receta_", "receta automatizacion macro secuencia guardada atajo"},
    {"agente_", "agente automatico rol ia bot trabajador delegar encargar"},
    {"modelo_local_", "modelo local ia de esta maquina qwen motor local bajar modelo sin internet"},
    {"acceso_remoto_", "acceso remoto desde afuera tunel pinggy dominio propio direccion publica internet "
                       "que entren de afuera conectarse de lejos"},
    {"descarga_", "descarga bajar actualizar actualizacion version nueva modelo agente cola de descargas"},
    {"descargas_", "descargas bajar actualizaciones version nueva modelos agentes cola de descargas que se esta bajando"},
    {"voz_", "voz hablar escuchar dictado microfono en voz alta asistente por voz"},
    {"prompter_", "teleprompter prompter apuntador discurso texto orador leer guion"},
    {"pantalla_", "pantalla televisor tv monitor chromecast proyector cartel proyectar castear"},
    // OJO, son DOS cosas distintas y la palabra «carrusel» lleva a las dos:
    // carrusel_* rota mensajes y QR en una pantalla secundaria; laminas_* es la
    // tira de imagenes de una escena (la presentacion del orador).
    {"carrusel_", "carrusel rotacion pasos secuencia pantalla cartelera"},
    {"laminas_", "lamina diapositiva presentacion powerpoint pptx pdf carrusel imagenes tira "
                 "slide placa volver atras saltar"},
    {"superficie_", "superficie pagina pantalla web canal control telemetria viva"},
    {"qr_", "qr codigo escanear celular enlace wifi contacto vcard"},
    {"grilla_personas_", "grilla mosaico galeria cuadricula personas gente conectados invitados integrantes caras todos a la vez reparto"},
    {"control_", "control windows escritorio mouse teclado captura pantalla programa ejecutar administrador "
                 "permisos computadora maquina pc"},
    {"media_", "medio archivo video musica audio cancion tema clip pelicula reproducir"},
    {"totem_", "totem registro de personas registrarse acreditacion inscripcion formulario hablado kiosco "
               "pantalla de bienvenida tv con camara qr para registrarse datos de los asistentes"},
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
    {"historico_", "historico archivo de eventos eventos anteriores pasados sesiones viejas bitacora "
                   "lo que paso antes historial"},
    {"superres_", "superresolucion superres agrandar escalar mejorar resolucion nitidez upscale x2 4k "
                  "imagen chica borrosa"},
    {"plugin_", "plugin complemento extension python programa propio script"},
    {"dcn_", "central de conferencia bosch dcn ccu microfonos de banca unidades consola delegados"},
    {"glosario_", "glosario terminos traduccion vocabulario palabras del evento como se dice"},
};

/// Lo propio de cada herramienta: lo que la distingue de sus hermanas.
const Fila kPropias[] = {
    // --- Ayuda y estado ---------------------------------------------------
    {"help", "indice catalogo guia empezar"},
    {"help_search", "buscar herramienta encontrar funcion que hay para"},
    {"status", "estado general resumen como esta todo panorama salud"},
    {"mcp_aire_lista", "aire guardas confirmar acciones peligrosas en vivo transmision bloqueo seguridad"},
    {"mcp_aire_ajustar", "aire guardas agregar sacar herramienta confirmar en vivo"},
    {"alert_reconocer", "reconocer alarma silenciar sonido aviso critico disparador"},
    {"alert_fabrica", "disparadores fabrica alarmas por defecto restablecer silencio saturacion senal disco"},
    {"evento_chequeo", "chequeo lista antes de salir marcar probado ptz casteo respaldo evento"},
    {"evento_chequeo_ver", "chequeo lista antes de salir ver pendientes evento"},
    {"escenas_ajustes", "editor escenas corte directo armar preview boton al aire"},
    {"aire_ftb", "ftb fundido a negro negro salida corte seguridad emergencia"},
    {"aire_placa", "placa seguridad cuarto intermedio volvemos enseguida minuto de silencio cartel emergencia"},
    {"aire_placas_listar", "placas textos ftb estado"},
    {"stream_destinos", "destinos transmision multidestino respaldo varias plataformas simultaneo"},
    {"stream_destino_agregar", "agregar destino transmision respaldo multidestino youtube facebook twitch rtmp srt rist passphrase latencia"},
    {"replay_ajustes", "repeticion replay buffer ultimos segundos instant replay"},
    {"replay_guardar", "repetir repeticion replay guardar clip ultimos segundos volver a pasar"},
    {"video_scope", "scope forma de onda waveform vectorscopio parade histograma niveles exposicion quemado legal"},
    {"video_legalizar", "legalizar broadcast safe tv rango legal gama compuesta entregar canal"},
    {"botonera_api_remota", "companion generic http api remota botonera stream deck token acceso http"},
    {"plugin_aprobados", "plugins aprobados para el aire aprobar plugin hash en vivo seguro firmado"},
    {"audio_sincro_medir", "medir sincronia labios aplauso claqueta retardo audio video desfasado"},
    {"audio_medidores", "medidores vumetro ppm vu pico balistica aguja"},
    {"invitado_prueba", "prueba tecnica invitado eco acople auriculares red latencia perdida"},
    {"proxy_estado", "proxy dominio dns certificado https lets encrypt vence traefik nombres servicios"},
    {"proxy_renovar", "renovar certificado lets encrypt acme proxy"},
    {"acceso_remoto_configurar_con_api_key", "pinggy api key armar todo solo token automatico subdominio fijo cambiar subdominio elegir nombre dns tunel pro sin entrar al panel"},
    {"mezclador_pestana", "mezclador pestana consola calibracion mostrar ventana"},
    {"audio_entrada_fuente", "audio rack tarjeta entrada fuente cambiar traer audio de otra"},
    {"ptz_foco", "camara ptz foco autofoco enfocar nitido"},
    {"ptz_inicio", "camara ptz home inicio centrar volver"},
    {"prompter_archivos", "prompter discurso carpeta renombrar mover borrar ordenar archivo"},
    {"pantalla_olvidar", "pantalla tv olvidar sacar borrar conocida"},
    {"voz_transcripcion_borrar", "voz transcripcion borrar vaciar limpiar"},
    {"calibracion_ver", "calibracion sala acustica ecualizacion analisis modos rt60 reverberacion schroeder propuesta verificacion eq"},
    {"calibracion_crear", "calibracion sala nueva sesion medidas largo ancho alto abrir"},
    {"calibracion_sala", "calibracion medidas sala curva objetivo pendiente"},
    {"calibracion_agregar", "calibracion parlante microfono punto escucha croquis agregar sm58 medicion"},
    {"calibracion_editar", "calibracion mover quitar punto parlante microfono calibracion borrar medicion"},
    {"calibracion_medir", "calibracion medir barrido sweep respuesta impulsiva punto parlante microfono rt60 snr"},
    {"calibracion_medicion", "calibracion medicion resultado respuesta rt60 c50 llegada"},
    {"calibracion_sustitucion", "calibracion microfono sustitucion referencia umik sm58 corregir"},
    {"calibracion_aplicar", "calibracion aplicar eq sala ecualizador revertir sacar"},
    {"mezclador_analisis_lsp", "lsp analizador espectro fase osciloscopio latencia profiler perfil rt60 ver interfaz abrir medir"},
    {"mezclador_estado", "mezclador niveles vumetros entradas salidas loudness lufs pico rms programa sin salida mezcla consola"},
    {"acceso_remoto_conectar", "conectar vpn internet afuera tunel prender apagar desconectar pinggy cloudflare sacar la sala"},
    {"acceso_remoto_cortar_tuneles", "pinggy cortar bajar tunel matar sesion activa otro equipo mismo token liberar"},
    {"evento_web", "pagina web del evento sitio frontend en vivo seccion portada cierre html personalizar dominio subdominio url del evento pagina publica"},
    {"acceso_remoto_estado", "pinggy tunel acceso remoto desde afuera internet url publica enlace externo dns cname dominio propio subdominio turn invitados de afuera"},
    {"proxy_configurar", "configurar proxy dominio dns certificados automaticos lets encrypt traefik 443"},
    {"playlist_crear", "playlist lista de videos clips armar playlist in out encadenar"},
    {"playlist_control", "playlist play siguiente anterior parar auto next loop reproducir lista"},
    {"salida_limpia", "senal limpia clean feed sin graficos sin zocalos sin dsk sin logo programa limpio canal"},
    {"salidas_profesionales", "srt rist ndi decklink sdi contribucion salidas profesionales"},
    {"stream_destino_editar", "editar destino transmision"},
    {"stream_destino_borrar", "borrar destino transmision"},
    {"record_disco", "disco espacio grabacion horas alcanza remux"},
    {"disco_prioridad", "disco lento trabado apuros usb prioridad grabacion programa pausar iso repeticion registro"},
    {"record_remux", "remux pasar mkv a mp4 mov grabacion convertir sin recodificar"},
    {"record_ajustes", "grabacion contenedor formato mkv mp4 remux ajustes"},
    {"audio_loudness", "loudness lufs r128 integrado true peak lra programa sonoridad volumen salida"},
    {"audio_loudness_reset", "loudness reset reiniciar integrado"},
    {"audio_loudness_objetivo", "loudness objetivo r128 a85 -23 -24 -16 -14 lufs"},
    {"audio_rack_vigia", "vigia modulos clap lento colgado bypass automatico watchdog audio"},
    {"audio_module_rearmar", "rearmar modulo saltado lento bypass automatico audio"},
    {"invitado_retorno", "mix-minus retorno invitado eco se escucha a si mismo oye programa sin su voz"},
    {"participant_calidad", "calidad invitado red mala bajar resolucion 720p 360p bitrate conexion pixelada cortes"},
    {"cuerpo_listar", "integrantes miembros socios consejeros delegados cuerpo bancas bloque cargo asamblea"},
    {"cuerpo_crear", "cuerpo asamblea nombre crear"},
    {"cuerpo_importar", "importar planilla csv excel integrantes miembros socios bancas"},
    {"cuerpo_plantilla", "planilla ejemplo integrantes csv"},
    {"integrante_editar", "integrante miembro agregar editar banca cargo bloque preset camara microfono"},
    {"integrante_ver", "integrante miembro buscar banca"},
    {"integrante_validar", "validar integrante nombre cargo zocalo revisado"},
    {"integrante_borrar", "borrar integrante"},
    {"banca_tiempo", "sumar tiempo al orador dar mas tiempo cuanto tiempo tiene cada uno para hablar minutos"},
    {"sesion_estado", "banca virtual sesion remota integrantes conectados desde afuera celular quien esta conectado presentes en linea"},
    {"sesion_credencial", "link personal pin del integrante generar acceso a la sesion remota banca virtual"},
    {"sesion_clave", "usuario y contraseña del integrante entrar sin link tipeando sesion remota"},
    {"sesion_datos", "datos de contacto del conferencista email telefono nota conexion remota"},
    {"sesion_revocar", "revocar link sacar acceso del integrante a la sesion remota"},
    {"sesion_habilitar", "habilitar al integrante que entro por la banca virtual darle presente validar por camara"},
    {"sesion_expulsar", "sacar de la sesion remota desconectar al integrante de la banca virtual"},
    {"sesion_votacion", "votar desde el celular votacion remota abrir votacion sin la central cartel rojo cuenta regresiva"},
    {"sesion_acceso_opciones", "pin en el qr acceso fijo link y pin fijos que no cambien no renovar credencial permanente integrante asamblea"},
    {"sesion_consola", "consola web de la presidencia acceso tablet de la presidencia link y pin de la consola"},
    {"sesion_registro", "registro de la sesion remota quien entro intentos fallidos acta banca virtual"},
    {"dcn_estado", "central de conferencia bosch dcn microfonos de banca estado de la sala quien habla"},
    {"dcn_microfono", "dale la palabra a abri el microfono de cerra el microfono de la banca darle audio cortarle el audio"},
    {"dcn_siguiente", "siguiente orador dale la palabra al siguiente el que sigue en la lista"},
    {"dcn_cerrar_todos", "cerra todos los microfonos callar la sala apagar microfonos"},
    {"dcn_pedidos", "quien pidio la palabra lista de pedidos de palabra quien quiere hablar"},
    {"dcn_pedido", "anotar en la lista de oradores pasar adelante sacar de la lista de pedidos"},
    {"dcn_pedidos_limpiar", "borrar la lista de pedidos vaciar los pedidos de palabra"},
    {"dcn_oradores", "quien tiene la palabra lista de oradores microfonos abiertos"},
    {"dcn_orador_quitar", "sacar al orador quitarle la palabra"},
    {"dcn_modo", "modo de los microfonos operador por voz cuantos microfonos abiertos maximo"},
    {"dcn_tiempo", "avisar que queda un minuto se termino el tiempo en la unidad"},
    {"dcn_unidades", "unidades de la central banca de cada unidad microfonos conectados"},
    {"dcn_unidad_asignar", "aprender unidades asignar unidad a la banca apretar el boton de la banca"},
    {"dcn_votacion", "votar en la central abrir la votacion cerrar la votacion votacion nominal de la sala"},
    {"dcn_votacion_resultado", "como va la votacion resultado de la votacion quien voto que"},
    {"dcn_asistencia", "presentes en la sala asistencia de la central quien esta"},
    {"dcn_votacion_a_quorum", "pasar la votacion a quorum resultado oficial al acta"},
    {"dcn_presentes_quorum", "quorum con los presentes de la central asistencia para el quorum"},
    {"dcn_simular_voto", "simular voto probar votacion sin la central"},
    {"dcn_simular_presencia", "simular presente ausente probar asistencia"},
    {"dcn_conectar", "conectar la central bosch dcn puerto serie rs232 ccu conferencia"},
    {"dcn_desconectar", "soltar la central desconectar la ccu"},
    {"dcn_puertos", "puertos serie adaptador usb serie"},
    {"dcn_traza", "tramas de la central diagnostico protocolo"},
    {"dcn_simular_boton", "simular un pedido de palabra probar sin la central"},
    {"integrante_foto", "foto del integrante cara del boton subir foto capturar foto con la camara"},
    {"recinto_ver", "grilla del recinto bancas plano de la sala donde se sienta cada uno"},
    {"recinto_editar", "armar la grilla del recinto acomodar bancas mover banca filas columnas"},
    {"recinto_apuntar", "apuntar la camara a la banca preparar el plano de un integrante"},
    {"ptz_posicion_foto", "foto de la posicion miniatura como se ve el plano guardado"},
    {"invitado_info_aprobar", "aprobar cargo invitado zocalo validar info rotulo"},
    {"publico_chat_leer", "chat mensajes recientes publico destacar" " comentarios del publico chat del vivo leer el chat lo que escribe la gente"},
    {"chat_destacar", "destacar mensaje chat aire pantalla publico pregunta"},
    {"chat_quitar_destacado", "sacar destacado chat"},
    {"ptz_interlock", "interlock ptz camara al aire no mover bloqueo seguridad"},
    {"recinto_bancas_ver", "bancas oradores presenciales fila integrantes recinto"},
    {"banca_cola", "fila banca agregar quitar orador presencial" " anotar en la fila pedir la palabra lista de oradores presenciales"},
    {"banca_al_aire", "banca al aire orador presencial preset camara microfono zocalo consejero habla"},
    {"recinto_orador_terminar", "terminar orador presencial banca" " cortar quitar la palabra terminar el turno del que habla en el recinto concejal"},
    {"entrada_respaldo", "sin senal entrada caida respaldo escena cortar armar camara perdida"},
    {"cc_estado", "subtitulos ocultos closed captions cc 608 accesibilidad sordos"},
    {"cc_activar", "prender subtitulos ocultos closed captions cc 608 youtube accesibilidad"},
    {"cc_desactivar", "apagar subtitulos ocultos cc"},
    {"cc_probar", "probar subtitulos ocultos cc"},
    {"eventos_temas", "bus eventos temas tally aire niveles alarmas suscribir"},
    {"eventos_ultimo", "ultimo valor tema evento bus"},
    {"eventos_esperar", "esperar evento tema reaccionar"},
    {"botonera_perfiles", "botonera companion stream deck tablet perfiles puestos"},
    {"botonera_plantillas", "plantillas botonera director audio asamblea prompter"},
    {"botonera_perfil_crear", "crear perfil botonera puesto"},
    {"botonera_perfil_editar", "editar perfil botonera permisos"},
    {"botonera_perfil_borrar", "borrar perfil botonera"},
    {"botonera_exportar", "exportar perfil botonera json"},
    {"botonera_importar", "importar perfil botonera json"},
    {"botonera_paginas", "paginas botonera grilla"},
    {"botonera_pagina_crear", "crear pagina botonera grilla"},
    {"botonera_pagina_editar", "editar pagina botonera"},
    {"botonera_pagina_borrar", "borrar pagina botonera"},
    {"botonera_celdas", "celdas botones botonera estado"},
    {"botonera_celda_set", "boton fader perilla celda botonera accion tally ligadura"},
    {"botonera_celda_borrar", "borrar boton celda botonera"},
    {"botonera_presionar", "probar boton apretar botonera"},
    {"botonera_dispositivos", "tablets aparatos stream deck botonera conectados"},
    {"botonera_dispositivo_asignar", "asignar perfil tablet aparato botonera"},
    {"botonera_emparejar", "emparejar tablet qr botonera codigo"},
    {"botonera_revocar", "revocar tablet token botonera"},
    {"botonera_auditoria", "auditoria registro botonera acciones"},
    {"mcp_token_rotar", "rotar token mcp seguridad"},
    {"seguridad_estado", "seguridad token pin sesion accesos"},
    {"stream_salud", "salud transmision kbps perdidos congestion reconexiones"},
    {"scene_cut", "cut corte seco escena armada"},
    {"transicion_manual", "t-bar palanca transicion manual"},
    {"escena_transicion", "transicion de entrada escena stinger"},
    {"audio_afv", "audio sigue al video afv"},
    {"media_restante", "cuanto le queda video medio al aire cuenta regresiva"},
    {"supervisor_caidas", "caidas reinicio causa crash supervisor"},
    {"config_generaciones", "configuracion version anterior restaurar corrupta"},
    {"modo_vivo_estado", "modo en vivo proteger paginas candado"},
    {"modo_vivo_ajustes", "ajustes modo en vivo paginas protegidas"},
    {"notificador_listar", "notificadores avisos telegram mail webhook"},
    {"notificador_agregar", "agregar notificador telegram mail smtp webhook"},
    {"notificador_probar", "probar notificador aviso"},
    {"notificador_borrar", "borrar notificador"},
    {"acciones_listar", "acciones catalogo take ftb atajos un golpe"},
    {"atajos_listar", "atajos teclado teclas pedal"},
    {"atajo_asignar", "asignar atajo tecla global pedal prompter"},
    {"atajo_quitar", "quitar atajo tecla"},
    {"accion_ejecutar", "ejecutar accion atajo"},
    {"glosario_exportar", "exportar glosario csv revision traduccion"},
    {"glosario_importar", "importar glosario revisado csv traduccion"},
    {"traduccion_rotulo", "rotulo traduccion automatica dsk aviso"},
    {"subtitulos_correccion", "correccion subtitulos demora corrector humano"},
    {"subtitulos_pendientes", "frases pendientes corregir subtitulos"},
    {"subtitulos_puesto", "puesto corrector subtitulos enlace web corregir desde otra computadora estenotipista"},
    {"subtitulos_corregir", "corregir aprobar descartar frase subtitulos"},
    {"quorum_regla", "quorum mayoria simple absoluta dos tercios miembros reglas"},
    {"quorum_presentes", "hay quorum presentes"},
    {"votacion_resultado_oficial", "resultado oficial votacion tablero afirmativos negativos abstenciones nominal"},
    {"chat_a_preguntas", "sumar chat a preguntas cola youtube"},
    {"orden_del_dia_cargar", "orden del dia puntos expedientes sesion cargar"},
    {"orden_del_dia_ver", "orden del dia puntos uso de la palabra tiempos capitulos" " que se trata ahora punto actual temario"},
    {"orden_del_dia_punto", "punto orden del dia tratamiento votado comision siguiente"},
    {"seguir_microfono", "seguir microfono abierto banca pidio la palabra automatico"},
    {"seguir_microfono_estado", "seguir microfono estado propuesta banca"},
    {"dsk_capas_ver", "capas dsk logo mosca interprete lsa encima programa"},
    {"dsk_definir", "definir capa dsk logo interprete lsa lengua de senas recuadro"},
    {"dsk_mostrar", "subir bajar capa dsk logo interprete lsa"},
    {"botonera_satellite", "companion satellite bitfocus stream deck loupedeck botonera"},
    {"botonera_satellite_estado", "companion satellite estado clientes"},
    {"botonera_estado", "estado servidor botonera conexiones"},
    {"voz_vocabulario", "vocabulario voz texto transcripcion nombres propios"},
    {"voz_vocabulario_agregar", "agregar termino vocabulario transcripcion nombres cuerpo"},
    {"voz_vocabulario_quitar", "quitar termino vocabulario transcripcion"},
    {"voz_vocabulario_probar", "probar vocabulario correccion transcripcion"},
    {"supervisor_politica", "supervisor latido interfaz trabada colgada relanzar reiniciar watchdog aire"},
    {"security_status", "seguridad contaminacion sesion permisos aprobacion"},
    {"system_info", "version compilacion build acerca de datos del equipo"},
    {"system_hardware", "llenando llena disco almacenamiento espacio queda lugar se esta llenando el disco "
                        "disco lleno espacio libre en disco cuanto "
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
    {"scene_switch", "corta cortala corte pasa poné pone cambiar poner al aire pasar a cortar a programa "
                     "activa corte directo urgencia "
                     "cortar a camara pasar a camara ir a camara poner la camara "
                     "pone al aire la pone al aire poner al aire la escena arranca con "
                     "escena siguiente la escena que sigue pasar de escena otra escena"},
    {"scene_preview", "dejar preparada dejala lista preparar la proxima armar la que sigue armar preparar el "
                      "proximo plano bus de preview previo antes de sacarlo "
                      "cuear siguiente escena mesa switcher"},
    {"preview_al_aire", "sacar al aire lo armado sacar la armada tomar el take ya take sacar al aire la armada "
                   "cortar ahora disparar el plano preparado mesa switcher"},
    {"scene_create", "crear nueva agregar escena"},
    {"scene_copy", "copiar duplicar clonar escena" " hacer otra igual copia"},
    {"scene_delete", "borrar eliminar quitar escena" " eliminar la escena"},
    {"scene_rename", "renombrar cambiar nombre escena" " rebautizar ponerle otro nombre"},
    {"scene_move", "mover ordenar reordenar posicion escena subir bajar en la lista ponerla antes que otra"},
    {"scene_folder", "carpeta agrupar organizar escenas"},
    {"scene_protect", "proteger bloquear candado no tocar impedir cambios dejarla protegida que nadie la "
                      "borre ni la renombre"},
    {"scene_export", "exportar respaldo backup copiar a archivo"},
    {"scene_define", "definir armar escena completa de una plantilla"},
    {"transition_set", "que fundido esta puesto que transicion hay puesta cual es el fundido fundido corte "
                       "disolvencia stinger duracion transicion"},
    {"source_types", "que tipos de fuente hay disponibles"},
    {"escena_fuentes_ver", "que hay adentro de la escena que tiene la escena que capas tiene contenido de la escena "
                    "listar fuentes de una escena que hay adentro"},
    {"crear_fuente",
     "agrega la camara agrega a esta escena sumar una camara agregar a la escena sumar a la escena meter en "
     "la escena poner en la escena agregar poner sumar fuente camara imagen texto navegador codigo qr "
     "captura de pantalla capturar una aplicacion ventana"},
    {"captura_opciones",
     "que pantallas hay que monitores que ventanas que aplicaciones se pueden capturar captura de pantalla "
     "capturar una ventana una aplicacion elegir monitor"},
    {"source_remove", "quitar sacar borrar fuente" " eliminar sacar de la escena"},
    {"source_update",
     "cambiar editar modificar fuente correr mover el titulo o el zocalo de lugar posicion tamano "
     "sombra paralela resplandor brillo borde difuso desvanecido transparencia opacidad efectos redondear "
     "esquinas redondeadas bordes redondos" " agrandar achicar recuadro pip esquina visibilidad orden de capas adelante atras"},
    {"source_properties", "propiedades opciones parametros de una fuente"},
    {"source_set_property",
     "cambiar una propiedad valor ajuste de fuente refrescar la lista de dispositivos apretar un boton"},
    {"guest_source_add", "poner invitado en escena mostrar participante camara remota"},

    // --- Audio -------------------------------------------------------------
    {"audio_set", "acople acoplando pitido cortar el audio bajalo ya urgente "
                  "silenciar mutear callar bajar subir volumen ganancia mute solo "
                  // Como se queja alguien en cabina: no dice «audio_set», dice
                  // que no se escucha. Sin estas palabras, «el invitado no se
                  // escucha» no traia NADA de audio entre los primeros.
                  "no se escucha no se oye sin sonido no tengo sonido no suena "
                  "esta mudo quedo mudo no le sale audio mas fuerte mas bajo"},
    {"audio_sincro", "desfasado de la imagen no coincide con la imagen labios imagen adelantada sincronizar "
                     "labios desfase desfasado desincronizado adelantado atrasado "
                     "delay offset demora retardo la voz no coincide con la imagen"},
    {"audio_sources", "que fuentes de audio hay listar canales"},
    {"audio_escuchar", "acople de donde viene el pitido aislar canal "
                       "pfl solo escuchar previo monitorear probar un microfono antes de abrirlo "
                       "auriculares cascos oir sin sacar al aire"},
    {"audio_devices", "placa de sonido microfono dispositivo entrada salida hardware"},
    {"gst_elementos", "plugins elementos gstreamer faltan diagnostico silencio negro"},
    {"inicio_con_el_equipo", "arrancar solo al encender corte de luz volver solo inicio automatico"},
    {"audio_input_add", "agregar microfono entrada de audio linea"},
    {"audio_input_remove", "quitar sacar borrar microfono entrada de audio"},
    {"audio_rack", "rack cadena procesamiento efectos de un canal"},
    {"audio_plantillas", "plantilla preset cadena tipica voz musica"},
    {"audio_modules", "que efectos hay disponibles procesadores"},
    {"audio_module_add", "agregar efecto procesador al canal"},
    {"audio_module_remove", "quitar sacar efecto procesador"},
    {"audio_module_get", "ver parametros de un efecto"},
    {"audio_module_set", "cambiar parametros de un efecto"},
    {"audio_patch", "ruteo patch enviar a donde va matriz"},
    {"audio_bypass", "puentear desactivar saltear efecto comparar"},
    {"audio_gate", "el microfono queda abierto se escucha todo lo de alrededor micro abierto compuerta gate "
                   "ruido de fondo silencio respiracion"},
    {"audio_compressor", "compresor compresion dinamica nivelar parejo"},
    {"audio_deesser", "deesser sibilancia ese silbido"},
    {"audio_eq", "ecualizador eq graves medios agudos tono frecuencia"},
    {"audio_multiband", "multibanda compresion por bandas"},
    {"audio_limiter", "limitador limite pico clip saturacion proteger"},
    {"audio_analyze", "analizar espectro medir problema ruido zumbido"},
    // ACOPLE, PITIDO, ZUMBIDO: lo que se grita en cabina. Ninguna de estas
    // palabras estaba, y «hay acople» no traia NADA de audio. Es el caso que
    // menos tiempo da para pensar, asi que es el que mejor tiene que andar.
    {"audio_measure", "no tengo sonido sin sonido no sale audio quedamos sin audio pitido chillido silbido "
                      "agudo insoportable suena un pitido no se escucha nada al aire quedamos sin audio "
                      "acople acoplando acoplamiento microfonia feedback pitido chillido "
                      "zumbido ruido de fondo satura saturando satura el audio distorsiona "
                      "medir niveles pico "
                      "medir lufs nivel pico rms loudness normalizar"},
    {"audio_generator", "tono generador prueba ruido rosa senal de test"},
    {"audio_outputs", "salidas de audio a donde sale"},
    {"audio_output_set", "cambiar salida de audio destino"},
    {"audio_show", "mostrar abrir panel de audio"},
    {"audio_editor", "editar audio abrir editor"},
    {"mixer_list", "consola mesa canales faders niveles"},

    // --- Transmision y grabacion -------------------------------------------
    {"stream_start", "salimos al aire salgamos al aire arrancamos la transmision empezar a transmitir "
                     "empezar arrancar salir al aire comenzar transmision"},
    {"stream_stop", "parar terminar cortar bajar del aire bajanos del aire bajalo del aire "
                    "sacanos del aire cortar la transmision fin de transmision" " terminar la transmision dejar de transmitir cortar el streaming"},
    {"record_start", "empezar a grabar arrancar grabacion rec" " grabar empezar la grabacion"},
    {"record_stop", "para frena detene la grabacion cortar de grabar parar grabacion para la grabacion parar "
                    "de grabar frena la grabacion "
                    "cortar la grabacion terminar de grabar" " detener terminar la grabacion stop"},
    {"grabacion_individual", "grabar por separado pista aislada isos cada camara"},

    // --- Ver ---------------------------------------------------------------
    {"foto_programa", "quedamos sin imagen no hay imagen pantalla en blanco se fue la imagen que esta al "
                        "aire que se ve ahora salida programa"},
    {"foto_fuente", "ver una fuente revisar antes de poner al aire"},
    {"foto_todas_escenas", "todas las escenas de una grilla multiview"},
    {"capture_feed", "se congelo la camara del invitado su camara quedo trabada imagen congelada ver una "
                     "señal entrante feed camara"},
    {"foto_invitado", "ver a una persona invitado invitada camara de alguien cara como esta saliendo "
                       "como sale como se ve el invitado mostrame al invitado mostrame a la invitada"},
    {"foto_todos_invitados",
     "ver a todos los invitados grilla de personas toda la gente que hay quienes estan en la sala"},
    {"capture_sequence", "varias capturas seguidas ver que cambia movimiento" " cuadros seguidos congelado trabado titila varias fotos"},
    {"capture_ui", "ver la interfaz de la app pantalla del operador"},
    {"feeds_list", "que camaras hay conectadas que señales llegan que señales hay entrando fuentes de video"},

    // --- Sala --------------------------------------------------------------
    {"room_status", "se cayo un invitado se desconecto se fue quien quedo quien esta en la sala conectados "
                    "lista de gente cuanta gente "
                    "cuantos hay cuantos son quienes estan esperando en el lobby "
                    "estado de la sala"},
    {"room_set", "configurar sala cupo cerrar abrir limite"},
    {"room_pin", "pin clave codigo de acceso contrasena de la sala"},
    {"room_link", "enlace invitacion link para entrar compartir acceso url de la sala"},
    // las que solo tenian las palabras de su familia (revision de la busqueda, 17/10/2026)
    {"audio_set", "subir bajar volumen silenciar mutear desmutear callar cerrar abrir el micro mic acople satura fuerte despacio nivel db"},
    {"descarga_actualizar", "actualizar instalar version nueva"},
    {"descarga_cancelar", "cancelar parar cortar la descarga"},
    {"descarga_reintentar", "reintentar volver a bajar fallo"},
    {"descargas_buscar_actualizaciones", "buscar actualizaciones hay version nueva chequear"},
    {"descargas_estado", "estado progreso avance cuanto falta velocidad"},
    {"descargas_limpiar", "limpiar historial borrar la lista terminadas"},
    {"modelo_local_activar", "cargar activar usar prender el modelo local"},
    {"modelo_local_ajustes", "ajustes sesgo derivar mtp al arrancar configurar el modelo"},
    {"modelo_local_apagar", "apagar liberar memoria gpu"},
    {"modelo_local_bajar", "bajar descargar instalar un modelo catalogo"},
    {"modelo_local_borrar", "borrar eliminar desinstalar liberar espacio disco"},
    {"modelo_local_cancelar_descarga", "cancelar la descarga del modelo parar la bajada"},
    {"modelo_local_elegir", "probar una frase que herramienta elige clasificar test"},
    {"modelo_local_estado", "que modelos hay instalados cargado estado vision"},
    {"voz_ajustes", "elegir la voz que contesta hablante velocidad sintesis tts"},
    {"voz_atender", "tratar como dictado como si lo dijera el operador atender un pedido"},
    {"voz_automaticas", "acciones automaticas sin confirmar sin preguntar permitir"},
    {"voz_decir", "decir hablar decirle al operador por los auriculares avisar en voz"},
    {"voz_microfono", "microfono del operador mic de cabina talkback hablarle al lobby"},
    {"voz_teleprompter", "leer el teleprompter en voz alta locutor virtual"},
    {"voz_texto", "voz a texto dictado transcribir subtitular reconocimiento de voz stt"},
    {"voz_transcripcion", "transcripcion lo que se dijo texto transcripto ultimas frases"},
    {"audio_bus_agregar", "agregar crear bus salida virtual entrada virtual envio mix menos"},
    {"audio_bus_dispositivo", "placa de sonido a donde sale dispositivo de la salida virtual"},
    {"audio_bus_quitar", "quitar borrar eliminar bus virtual"},
    {"audio_bus_renombrar", "renombrar cambiar el nombre bus pista"},
    {"audio_dispositivos", "dispositivos placas de sonido auriculares salidas del equipo"},
    {"audio_filtros_catalogo", "catalogo de filtros lsp compresor compuerta ecualizador que filtros hay"},
    {"audio_lsp_restablecer_interfaz", "restablecer resetear interfaz lsp ventanas zoom escala"},
    {"audio_matriz", "matriz de mezcla ruteo envios a donde va cada fuente buses pistas"},
    {"audio_matriz_cruce", "cruce rutear mandar una fuente a un bus envio enviar a una pista"},
    {"audio_matriz_equipo", "entradas y salidas del equipo enchufado parlantes placa usb canales"},
    {"help", "ayuda guia como se hace no se como catalogo de herramientas"},
    {"control_web_aparatos", "aparatos emparejados tablets conectadas solicitudes dashboard web"},
    {"control_web_emparejar", "emparejar vincular una tablet un celular codigo qr dashboard"},
    {"control_web_perfil", "permisos perfil categorias que puede usar el aparato"},
    {"control_web_quitar", "quitar revocar desvincular sacarle el acceso a un aparato"},
    {"control_web_resolver", "aprobar rechazar solicitud pedido de acceso de un aparato"},
    {"control_web_tunel", "control web desde afuera tunel pin dashboard remoto"},
    {"monitors", "monitores pantallas conectadas resolucion hz pantalla principal"},
    {"evento_bitacora", "bitacora historial que paso registro cronologia log del evento"},
    {"evento_config", "configuracion con la que se hizo el evento foto de la config"},
    {"evento_grabaciones", "grabaciones archivos vivos transcripciones capitulos del evento"},
    {"evento_preparar", "preparar senal de ajuste barras tono placa antes de empezar fin de transmision"},
    {"evento_sellos", "sellos integridad no se toco intacto sha256"},
    {"evento_verificar", "verificar lista de control checklist esta todo listo para salir chequeo"},
    {"facebook_live_list", "vivos de facebook transmisiones lista estado"},
    {"facebook_login", "conectar la cuenta de facebook iniciar sesion login"},
    {"facebook_logout", "desconectar la cuenta de facebook cerrar sesion"},
    {"facebook_setup", "configurar la app de meta id clave secreta"},
    {"facebook_status", "estado de facebook conectado pagina"},
    {"facebook_video_update", "editar borrar un video de facebook titulo descripcion"},
    {"facebook_video_upload", "subir un video a facebook publicar la grabacion"},
    {"facebook_videos", "videos de la pagina de facebook lista"},
    {"rostro_lobby", "quien esta esperando en el lobby reconocer identificar caras"},
    {"acceso_remoto_clave", "clave token api key guardar credencial cloudflare"},
    {"acceso_remoto_configurar", "configurar prender apagar el tunel region dominio turn"},
    {"acceso_remoto_dns_instrucciones", "instrucciones dns cname para el administrador del dominio"},
    {"acceso_remoto_dns_verificar", "verificar el dns cname apunta bien"},
    {"acceso_remoto_panel", "panel de la cuenta tuneles activos"},
    {"acceso_remoto_probar_direccion", "probar una direccion subdominio disponible"},
    {"room_pin_bloqueos", "bloqueados por pin mal desbloquear destrabar ip"},
    {"twitch_login", "conectar la cuenta de twitch iniciar sesion login"},
    {"twitch_logout", "desconectar la cuenta de twitch cerrar sesion"},
    {"twitch_setup", "configurar la app de twitch id de cliente"},
    {"twitch_status", "estado de twitch conectado canal"},
    {"twitch_video_delete", "borrar vods videos del canal de twitch"},
    {"twitch_videos", "vods videos del canal de twitch lista"},
    {"video_tarjeta_ayuda", "ayuda de una tarjeta de video parametros que hace gstreamer"},
    {"youtube_live_list", "eventos en vivo de youtube transmisiones programadas lista"},
    {"youtube_live_update", "editar un evento de youtube titulo privacidad pasar a vivo terminar borrar"},
    {"youtube_login", "conectar la cuenta de youtube iniciar sesion google login"},
    {"youtube_logout", "desconectar la cuenta de youtube cerrar sesion"},
    {"enlaces_listar", "enlaces links servicios web urls que esta prendido estado de los servicios lista de enlaces"},
    {"enlace_activar", "prender apagar habilitar deshabilitar servicio web companion satellite tunel abrir cerrar la sala"},
    {"multiview", "multiview multivista muro mosaico de camaras ver varias fuentes juntas pantalla dividida"},
    {"diag_probar_caida", "probar una caida volcado crash dump prueba de errores"},
    {"direcciones", "direccion ip url enlace link red local tunel afuera puerto donde entra 127.0.0.1 localhost pinggy"},
    {"web_tema", "tema apariencia estilo template plantilla neumorfismo claro oscuro paginas web tablet botonera presidencia colores"},
    {"qr_mostrar", "qr codigo qr mostrar el qr pasame el qr escanear celular copiar url link enlace portapapeles ventana"},
    {"participant_admit",
     "hace pasar hacelo pasar que pase dejalo entrar adentro dejar entrar aceptar admitir aprobar del lobby"},
    {"participant_reject", "rechazar no dejar entrar negar acceso"},
    {"participant_kick", "echar expulsar sacar desconectar de la sala"},
    {"participant_requeue", "devolver mandar atras de nuevo al final de la fila el que ya hablo"},
    {"participant_admit_all", "dejar entrar a todos admitir todos vaciar lobby"},
    {"participant_talk", "hablarle privado audio al invitado susurrar"},
    {"participant_request", "pedile que prenda encienda apague que active su camara su microfono pedirle "
                            "algo pedir camara microfono solicitar"},
    {"participant_profile", "ficha datos nombre cargo informacion de la persona" " perfil del invitado cambiarle el nombre"},
    {"chat_send",
     "mandale un mensaje escribile decile por chat avisale mandar mensaje escribir al chat avisar por chat"},
    {"invitados_chat_leer", "que me dijo que dijo que escribio leer lo que mandaron que contesto historial "
                     "conversacion que dijeron leer chat"},
    {"chat_wait", "esperar mensaje quedarse esperando a que escriban avisame cuando escriba el invitado"},
    {"chat_overlay_add", "chat en pantalla mostrar comentarios al aire overlay"},
    {"share_list", "que archivos mandaron subidos adjuntos"},
    {"compartida_revisar", "ver archivo mirar adjunto abrir subida revisar lo que mando antes de ponerlo" " ver la imagen compartida mostrar lo que subio el invitado archivo compartido pixelada"},
    {"share_take", "poner archivo al aire mostrar foto documento subido"},
    {"share_delete", "borrar archivo subido eliminar adjunto"},
    {"guest_chat_pending", "mensajes sin responder pendientes de invitados"},
    {"guest_reply_suggest", "sugerir respuesta redactar contestacion borrador"},
    {"network_status", "se esta cortando se corta entrecortado se cae la transmision va lento red internet "
                       "conexion ancho de banda tunel calidad"},
    {"gateway_restart",
     "reiniciar sala gateway reiniciar servidor de la sala la sala no responde esta colgada"},
    {"operator_feed",
     "camara del operador cabina mi camara con que imagen me ven y me oyen los que esperan en el lobby"},

    // --- Oradores ----------------------------------------------------------
    {"remotos_fila_ver", "quien esta en la fila estado de oradores"},
    {"speakers_config",
     "configurar fila de oradores tiempos reglas activar el modo oradores minutos por orador"},
    {"speaker_on_air", "poner orador al aire darle el aire a quien habla" " dar el aire al orador remoto"},
    {"speaker_next", "pasa al que sigue el siguiente orador proximo orador turno siguiente siguiente orador "
                     "pasar al que sigue avanzar la fila"},
    {"speaker_move", "mover en la fila reordenar oradores pasar a alguien al primer lugar de la fila"},
    {"speaker_queue", "fila cola de oradores agregar sacar" " anotar en la lista de oradores remotos cola"},
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
    {"alert_types", "que se puede vigilar tipos de disparador de vigilancia eventos disponibles parametros"},
    {"alert_create", "avisame cuando crear alerta vigilar crear disparador vigilancia cuando digan"},
    {"alert_list", "que alertas hay configuradas disparadores vigilancias"},
    {"alert_update", "cambiar alerta editar"},
    {"alert_delete", "borrar alerta quitar aviso borrar disparador"},
    {"alert_events", "que paso historial de alertas disparos"},
    {"alert_wait", "esperar a que pase algo quedarse esperando evento"},
    {"receta_listar", "que recetas hay automatizaciones guardadas"},
    {"receta_guardar", "guardar receta automatizar secuencia"},
    {"receta_borrar", "borrar receta"},
    {"receta_correr", "correr receta ejecutar automatizacion"},

    // --- Asistente y agentes -------------------------------------------------
    {"assistant_status", "estado del asistente que modelo motor"},
    {"documentos_listar", "documentos del evento pdf documentacion que documentos hay"},
    {"documento_agregar", "agregar pdf documento documentacion subir archivo caratula"},
    {"documento_editar", "describir documento relacionar con punto del orden del dia seccion publicar cambiar caratula miniatura pagina de tapa"},
    {"documento_quitar", "quitar sacar borrar documento"},
    {"documento_texto", "leer texto del pdf resumir documento"},
    {"pdf_convertir", "convertir pasar a pdf exportar word docx excel planilla powerpoint html markdown texto imagen imprimir"},
    {"pdf_escena_ver", "pdf en la escena mostrar pdf en pantalla que pagina"},
    {"pdf_escena_poner", "pasar pagina pdf siguiente anterior elegir pdf mostrar documento en pantalla"},
    {"pdf_escena_crear", "escena para pdf armar escena mostrar pdf"},
    {"evento_escenarios", "tipos de evento guias plantillas preparar un evento clase asamblea reunion streaming acto"},
    {"evento_escenario", "guia para preparar un evento que preguntar checklist protocolo invitaciones verificar"},
    {"plantillas_listar", "plantillas de evento cuales hay ordenar administrar"},
    {"plantilla_ajustar", "renombrar ocultar ordenar plantilla guardar evento como plantilla"},
    {"documento_abrir", "abrir mostrar ver documento pdf en pantalla visor"},
    {"evento_nuevo", "arrancar de cero evento nuevo empezar algo nuevo desde cero copiar el actual plantilla "
                     "crear evento aparte sin tocar las escenas"},
    {"evento_cambiar", "abrir otro evento cambiar de evento pasar a otro evento"},
    {"eventos_listar", "que eventos hay lista de eventos plantillas"},
    {"tiempos_mapa", "latencias del pipeline cuanto tarda demora latencia retardo delay de cada entrada a cada salida tiempos"},
    {"tiempos_medir", "medir los tiempos correr un analisis de demoras cuanto tarda hasta youtube muestrear por elemento sondas gpu memoria ram baja sube cruce plugin que agrega"},
    {"tiempos_eventos", "eventos de la linea de tiempo conexion desconexion cambio de escena que paso latencia"},
    {"tiempos_evento_agregar", "marcar un momento poner una nota en la linea de tiempo evento manual"},
    {"tiempos_evento_impacto", "que cambio cuando por que subio la latencia antes y despues de un evento analizar delay"},
    {"tiempos_linea", "mostrar en la linea de tiempo graficar esta tarjeta comparar latencias"},
    {"tiempos_nodo", "cuanto tarda esta tarjeta este modulo esta salida historia de la demora"},
    {"tiempos_pipeline", "por dentro de una entrada elemento por elemento cadena de video pipeline gpu ram baja sube cruce copia rompe la cadena que plugin tarda retiene cuadros declara latencia diagnostico delay demora"},
    {"resolucion_plan", "resolucion tamaño cuanto se ve sobra falta superres al pedo zoom digital ptz detalle modo de la camara escala de mas colores alarma amarillo naranja rojo tarjetas mal armado orden"},
    {"tiempos_plugin", "como trabaja un plugin modulo filtro tarjeta gpu o cpu memoria acepta pads templates alternativa mas rapida propiedades por que tarda"},
    {"evento_borrar", "borrar eliminar sacar un evento tirar a la papelera evento de prueba"},
    {"evento_recuperar", "recuperar devolver un evento borrado papelera deshacer borrado"},
    {"evento_papelera_vaciar", "vaciar la papelera liberar espacio en disco borrar definitivamente eventos borrados"},
    {"ia_estado", "que no anda de la inteligencia artificial que falta configurar por donde empiezo asistente "
                  "offline voz del operador dictado modelo local microfono auriculares puesta en marcha"},
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
    {"dispositivos_video", "gst device monitor caps formatos mjpeg h264 crudo resolucion modos de la camara por que no da 1080 30 api media foundation v4l2 avfoundation"},
    {"video_input_add", "agregar camara webcam capturadora"},
    {"network_input_add", "agregar camara ip rtsp srt stream entrante url"},
    {"ndi_sources", "que fuentes ndi hay en la red"},
    {"ndi_input_add", "agregar fuente ndi camara por red"},
    {"input_status", "estado de las entradas que esta llegando"},
    {"ext_listar", "que extensiones hay plugins disponibles"},
    {"ext_prender", "activar extension plugin prender"},

    // --- Cámaras PTZ ------------------------------------------------------------
    {"voces", "voces para hablar idiomas texto a voz que voces hay ingles portugues frances"},
    {"texto_a_voz", "texto a voz tarjeta que hable decir en voz alta en otro idioma leer la transcripcion"},
    {"texto_flujos",
     "salidas de texto transcripciones con nombre que se esta transcribiendo flujos de texto"},
    {"ptz_camaras", "camaras ptz que puede cada camara estado del ptz backend visca onvif"},
    {"ptz_mover", "mover camara paneo pan tilt zoom acercar alejar girar subir bajar la camara"},
    {"ptz_parar", "parar la camara frenar el movimiento" " detener stop dejar quieta la camara frenar ptz"},
    {"ptz_preset", "preset posicion guardada ir a la posicion guardar posicion de camara plano"},
    {"ptz_encuadre", "encuadre recorte zoom digital reencuadrar acercar el plano centrar"},
    {"ptz_apuntar", "apuntar la camara a ese punto centrar ahi encuadrar ese cuadro acercar a esa zona"},
    {"ptz_configurar", "configurar ptz camara motorizada visca onvif panasonic ip de la camara"},
    {"ptz_posiciones", "posiciones guardadas con nombre de la camara lista de planos"},
    {"ptz_posicion_crear", "guardar posicion con nombre registrar plano de la camara agregar posicion"},
    {"ptz_posicion_editar", "renombrar posicion cambiar comentario regrabar plano"},
    {"ptz_posicion_borrar", "borrar posicion eliminar plano guardado"},
    {"ptz_ir_posicion", "llevar la camara a la posicion ir al plano mover a la posicion guardada"},
    {"enfocar", "enfocar enfoca banca a la persona al consejero apuntar la camara a mostrar a fulano plano de la banca presidencia"},
    {"banca_toma", "camara de la banca posicion de la banca toma asociar banca camara preset de la banca configurar bancas"},
    {"ptz_caras_ubicar", "caras que ve la camara quien esta en cuadro detectar rostros ptz"},
    {"ptz_seguir", "seguir a una persona tracking seguimiento ptz que la camara siga a alguien centrar cara"},
    {"ptz_buscar", "buscar camaras onvif en la red descubrir camaras ip"},
    {"video_grafo", "grafo de video tarjetas de video pipeline de la camara que filtros tiene"},
    {"video_tarjeta_agregar", "agregar tarjeta de video croma chroma key fondo verde color brillo contraste "
                              "saturacion recorte espejo salida de video"},
    {"video_filtros",
     "filtros de video de obs lut cube correccion de color llave nitidez catalogo de filtros"},
    {"video_modulos", "modulos gstreamer catalogo de filtros de video desentrelazar texto reloj efectos "
                      "opengl ayuda de filtros"},
    {"video_entrada_virtual",
     "entrada virtual senal de ajuste barras de color negro patron de prueba pipeline gstreamer rtsp srt"},
    {"video_controlador", "misma correccion de color en todas las camaras controlador compartido igualar "
                          "camaras vincular tarjetas"},
    {"video_pipeline", "armar pipeline de video cadena de tarjetas correccion de color compartida entre "
                       "camaras igualar camaras"},
    {"video_mosaico",
     "mosaico de camaras imagen en imagen pip lado a lado multicamara juntar camaras en una escena"},
    {"video_panoramica",
     "panoramica unir camaras stitching coser varias camaras lado a lado camara 360 dos lentes ojo de pez "
     "fisheye equirectangular dewarp calibrar lentes corregir deformacion distorsion encajar ptz virtual"},
    {"video_estado", "estado de la camara resolucion fps cuadros por segundo espacio de color colorimetria "
                     "entrelazado diagnostico de video"},
    {"video_entrada_red", "stream de red srt rtmp rtsp udp recibir video por red entrada remota corresponsal "
                          "movil jitter buffer latencia"},
    {"video_reloj", "reloj del estudio ntp ptp sincronizar relojes genlock reloj externo publicar reloj"},
    {"video_sincronia",
     "sincronizar streams desfase entre camaras medir retraso alinear video atrasado adelantado lip sync"},
    {"video_desfase", "atrasar adelantar video retraso manual desfase de una entrada"},
    {"entrada_borrar",
     "borrar entrada eliminar camara sacar fuente de video de la app quitar entrada de red"},
    {"equipos_escanear",
     "buscar equipos detectar dispositivos escanear red usb camaras microfonos hid serie"},
    {"equipos_listar", "que equipos hay dispositivos encontrados listar equipos"},
    {"equipo_ver", "detalle de un equipo datos acciones de un dispositivo"},
    {"equipo_renombrar", "ponerle nombre a una camara microfono placa de sonido capturadora renombrar dispositivo "
                         "nombre del operador como se llama"},
    {"equipo_configurar", "configurar dispositivo nota formato preferido modo de la camara"},
    {"equipo_olvidar", "olvidar dispositivo ausente desconectado sacar de la lista borrar equipo viejo"},
    {"equipo_accion", "hacer algo con un equipo funciones onvif operacion onvif ficha agregar camara "
                      "explorar uvc pedir clave activar ptz"},
    {"plugin_listar", "plugins python extensiones programar equipo raro camara rara que plugins hay"},
    {"plugin_escribir", "escribir plugin python programar soporte para equipo raro ptz usb hid logitech "
                        "rally kandao 360 ojo de pez camara que no anda"},
    {"plugin_probar", "probar plugin sin tocar el aire test informe errores de python"},
    {"plugin_activar", "prender apagar plugin activar desactivar arrancar"},
    {"plugin_control", "pedirle algo a un plugin control accion"},
    {"plugin_eventos", "eventos de plugins canales de datos quien habla direccion de voz"},
    {"plugin_credenciales",
     "clave de un plugin usuario contraseña de un equipo token de la api de la marca formulario seguro"},
    {"plugin_exportar", "exportar plugin zip pasar a otro aparato copiar plugin respaldo"},
    {"plugin_importar", "importar plugin zip instalar plugin de otro aparato"},
    {"plugin_tarjeta", "tarjeta de plugin filtro de video python filtro de audio python"},
    {"dispositivos_listar", "que hay conectado usb hid serie camaras microfonos vid pid uvc dispositivos"},
    {"red_escanear", "buscar camaras en la red escanear red detectar camaras ip onvif rtsp hikvision dahua "
                     "axis ezviz dvr nvr"},
    {"camara_credenciales",
     "usuario y clave de la camara contrasena codigo de verificacion ezviz credenciales"},
    {"camara_ficha",
     "caracteristicas de la camara onvif perfiles resolucion ptz infrarrojo como conectar la camara"},
    {"camara_agregar", "agregar camara ip conectar camara de red al estudio configurar ptz onvif"},
    {"camara_urls", "probar urls rtsp de la camara"},
    {"video_tarjeta_quitar", "quitar tarjeta de video sacar el croma sacar filtro"},
    {"video_tarjeta_ajustar", "ajustar croma similitud suavidad brillo contraste recorte"},
    {"video_cable", "conectar tarjetas de video cable rama del grafo"},
    {"video_salida_a_escena", "poner la salida del grafo en la escena camara con croma en la escena"},
    {"video_grafo_limpiar", "volver la camara a como vino sacar todas las tarjetas de video"},
    {"video_mejora", "mejorar imagen ruido nitidez calidad de la camara color corrido led"},

    // --- Medios ---------------------------------------------------------------
    {"media_list", "que archivos hay musica videos biblioteca"},
    {"media_find", "buscar archivo cancion video por nombre"},
    {"media_view", "ver miniatura previsualizar archivo"},
    {"medio_poner",
     "musica cancion tema pista de audio video clip poner musica poner musica de fondo musica de fondo poner "
     "una cancion un video poner musica reproducir cancion video al aire cortina"},

    // --- Operador ------------------------------------------------------------
    {"operator_message", "avisarle al operador decirle mandarle un mensaje"},
    {"operator_ask", "preguntale al de cabina consultale al operador pedile permiso al humano preguntarle al "
                     "operador consultar pedir permiso confirmar"},
    {"operator_messages", "que dijo el operador leer sus mensajes"},

    // --- Graficos y rotulos ---------------------------------------------------
    {"grafico_listar", "que graficos hay placas disponibles"},
    {"grafico_probar", "probar grafico previsualizar antes de sacarlo"},
    {"grafico_en_escena", "grafico dentro de una escena agregar que animados hay puestos en las escenas"},
    {"grafico_repertorio", "catalogo de graficos plantillas disponibles"},
    {"grafico_actualizar", "cambiar los numeros del grafico al aire actualizar la votacion en vivo torta barras "
                           "sin que vuelva a entrar"},
    {"grafico_medios", "que gif videos imagenes tiene el grafico"},
    {"grafico_medio_agregar", "poner un gif animado en el grafico video con transparencia alfa webm prores "
                              "secuencia de png logo animado"},
    {"grafico_medio_quitar", "sacar un gif o video del grafico"},
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
                         "tally luz roja camara al aire escena en la tele una escena en el televisor del hall"},
    {"catalogo_listar", "que se puede mostrar que puedo poner en un monitor o en la tele lista de cosas "
                        "paneles escenas multiview paginas tarjetas"},
    {"monitor_listar", "que muestra cada monitor monitores de la maquina orden de los monitores cual es "
                       "el de la izquierda pantallas conectadas que hay en el segundo monitor"},
    {"monitor_asignar", "poner en el monitor dejar fijo en el segundo monitor el programa en el monitor del "
                        "escenario la multiview en el monitor de la izquierda llenar los monitores grilla"},
    {"monitor_quitar", "sacar del monitor dejar el monitor vacio que no muestre nada dejar de esperar un monitor"},
    {"media_carpetas", "como estan ordenados los medios que carpetas hay arbol"},
    {"media_ordenar", "mover archivos a otra carpeta ordenar renombrar crear carpeta borrar "
                      "limpiar acomodar los medios papelera"},
    {"ventana_captura", "ver la interfaz mirar la pantalla del aparato como se ve la pagina "
                        "captura de la ventana revisar el gui algo se ve mal encimado cortado"},
    {"pantalla_fijar", "dejar fijo que siempre muestre lo mismo permanente"},
    {"pantalla_renombrar", "ponerle nombre a un televisor renombrar"},
    {"pantalla_agregar", "agregar televisor a mano por ip"},
    {"pantalla_parar", "dejar de mostrar apagar la proyeccion cortar el casteo" " sacar de la pantalla apagar la tele dejar de proyectar"},
    {"carrusel_listar", "que carruseles hay rotaciones"},
    {"carrusel_armar", "armar carrusel rotacion de pantallas pasos"},
    {"carruseles_listar", "que carruseles de laminas hay presentaciones cargadas"},
    {"carrusel_laminas_ver", "que laminas tiene cuantas diapositivas de que trata cada una"},
    {"grafico_mostrar", "poner grafico al aire zocalo placa GC generador de caracteres rotulo "
                        "pie de pagina faldon "
                        "el apellido esta mal corregir el nombre cambiar el cargo poner el nombre "
                        "del que habla levantar el zocalo tirar la placa "
                        "camara en el zocalo video en vivo dentro del grafico recuadro con la camara"},
    {"grafico_ocultar", "sacar grafico esconder bajar el zocalo sacar la placa quitar el GC "
                        "limpiar la pantalla fuera el cartel"},
    {"laminas_buscar", "buscame la imagen de los perros cual era la del grafico donde esta la "
                       "lamina que dice buscar adentro de la presentacion por lo que se ve"},
    {"laminas_ir", "volve a la lamina del grafico saltar a la diapositiva numero poner esa placa "
                   "pasa a la siguiente la que sigue la anterior avanza una atras una "
                   "pone la diapositiva mostrar esa lamina pasar a la"},
    {"laminas_desarmar", "convertime este powerpoint en imagenes convertir un pdf desarmar "
                         "presentacion importar armar carrusel desde un archivo "
                         "el orador trajo un pdf armalo para proyectar cargar la charla"},
    {"laminas_armar", "armar un carrusel con estas imagenes juntar placas sueltas crear carrusel "
                      "nuevo con los logos"},
    {"laminas_editar", "sacar una lamina cambiarla de lugar reordenar renombrar el carrusel borrarlo "
                       "ponela al final al principio cambiar el orden de las laminas adelantar atrasar"},
    {"laminas_aplicar", "poner el carrusel en una escena agregar la presentacion a la escena "
                        "mostrar las laminas al aire vincular"},
    {"laminas_sin_analizar", "cual falta mirar etiquetar proxima sin analizar"},
    {"laminas_etiquetar", "guardar lo que se ve en la lamina describir para poder buscarla"},
    {"superficie_listar", "que paginas estan vivas realmente mostrando telemetria retardo"},
    {"superficie_mostrar", "cambiar lo que muestra sin recastear empujar url recargar"},

    // --- QR -----------------------------------------------------------------------
    {"qr_listar", "que codigos qr hay guardados tipos iconos qr del publico qr de la sala para elegir"},
    {"grilla_personas_estado", "quienes se ven en la grilla cuantos quedaron afuera mas n quien habla"},
    {"grilla_personas_ajustar", "cambiar tope orden prioridad grupo repartir entre escenas parejo llenar en orden solo con camara"},
    {"grilla_personas_fijar", "fijar dejar fijo siempre en la grilla que no salga poner a alguien en el mosaico"},
    {"grilla_personas_sacar", "sacar quitar a alguien de la grilla que no aparezca en el mosaico esconder"},
    {"grilla_personas_mover", "mover pasar a alguien de una grilla a otra cambiar de mosaico"},
    {"grilla_personas_devolver", "devolver volver a automatico soltar desfijar que vuelva a la grilla"},
    {"ui_abrir_panel", "abrir panel dialogo ventana superresolucion entrada virtual catalogo modulos mostrar"},
    {"superres_laboratorio", "superresolucion laboratorio bibliotecas python pytorch paquetes bajar instalar borrar espacio directml"},
    {"superres_modelo_borrar", "borrar modelo superresolucion red onnx espacio"},
    {"superres_trabajo", "superresolucion trabajo avance progreso cancelar esperar descarga entrenamiento"},
    {"control_estado", "puede el asistente manejar la pc mouse captura permisos del control de windows"},
    {"control_configurar", "habilitar prender apagar control de windows permitir administrador mouse teclado"},
    {"qr_generar", "generar codigo qr crear sin guardar probar"},
    {"qr_guardar", "guardar codigo qr en la biblioteca"},
    {"qr_borrar", "borrar codigo qr"},
    {"qr_exportar", "exportar qr a archivo png svg imprimir"},

    // --- Evento ---------------------------------------------------------------------
    {"guion_listar", "que eventos hay guiones guardados"},
    {"guion_ver", "que sigue en el guion que viene ahora proximo bloque proximo cue ver el guion del "
                   "evento leer rundown cuando es el evento que dia a que hora arranca"},
    {"guion_abrir", "abrir evento cargar guion"},
    {"modelo_local_disco", "modelos instalados disco espacio limpiar borrar viejos versiones gigas"},
    {"agente_limpieza", "limpiar chats asistente borrar conversaciones reinstalar agente versiones viejas espacio"},
    {"video_tarjeta_mover", "mover tarjeta orden cadena antes despues ptz rostros reordenar"},
    {"evento_en_curso", "que evento esta en curso historico jornada del dia registro"},
    {"evento_iniciar", "empezar evento poner en curso registrar historico"},
    {"evento_terminar", "terminar cerrar evento en curso historico automatico"},
    {"guion_carpeta", "carpeta mover ordenar guiones agrupar archivo historico"},
    {"evento_ficha", "ficha evento lugar etiquetas notas estado historico"},
    {"guion_bloque", "bloque del guion seccion agregar mover"},
    {"configuraciones_listar", "que configuraciones hay perfiles demo"},
    {"configuracion_crear", "nueva configuracion crear perfil copiar la configuracion demo de prueba"},
    {"configuracion_cambiar", "cambiar de configuracion pasar a otra configuracion abrir la demo usar otro perfil"},
    {"configuracion_editar", "renombrar borrar describir configuracion"},
    {"asamblea_revisar", "que falta para sesionar asamblea concejo consejo preparar sesion escenas camaras bancas revisar"},
    {"asamblea_escenas_base", "armar escenas de la asamblea crear escenas para las bancas plano general debate remoto"},
    {"debate_iniciar", "debate entre dos cara a cara empezar debate dos remotos sala contra remoto"},
    {"debate_turno", "turno del debate darle la palabra a uno callar al otro ambos"},
    {"debate_terminar", "terminar el debate cortar debate"},
    {"debate_config", "escena del debate tiempo de cada turno configurar debate"},
    {"debate_estado", "como va el debate quien habla"},
    {"presidencia_callar_todos", "silenciar a todos callar a todos mute general cerrar todos los microfonos remotos"},
    {"presidencia_hablar", "hablo yo habla el presidente la presidencia toma la palabra prioridad"},
    {"sesion_vistas", "que ven los remotos en el celular canales escena salida principal"},
    {"sesion_hablar", "hablarle a un remoto apretar para hablar talkback intercom"},
    {"dcn_ajustes", "ajustes de la central prioridad cancela pedidos tono ambiente modo"},
    {"guion_cue", "cue paso accion del guion agregar editar"},
    {"guion_guardar", "guardar el guion del evento ponerle fecha hora y huso horario cambiarle el titulo"},
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
    {"votacion_recuento", "como va la votacion resultado de la encuesta cuantos votaron quienes "
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
    {"evento_correr", "correr el guion arrancar avanzar pausar el evento" " ejecutar siguiente cue"},

    // --- Ventanas -------------------------------------------------------------------
    {"ventana_listar", "que ventanas hay abiertas donde esta cada una en que monitor geometria"},
    {"ventana_mover", "mover ventana a otro monitor acomodar pantalla completa maximizar "
                      "mitad izquierda derecha organizar el escritorio armar el puesto"},
    {"ventana_cerrar", "cerrar ventana quitar sacar de pantalla"},
    {"participant_admit", "admitir hacer pasar dejar entrar aceptar al invitado que espera "
                          "sacarlo del lobby meterlo a la sala"},
    {"participant_reject", "rechazar no dejar entrar negar la entrada echar del lobby"},
    {"participant_kick", "echar sacar de la sala expulsar desconectar a alguien que ya entro"},
    {"imagen_preguntar", "mirar la imagen que se ve ahi que hay en camara revisar visualmente "
                        "analizar captura vision hay alguien cuadro negro se lee el rotulo" " preguntarle a la imagen contar personas leer el texto de la imagen"},
    {"audio_plugin_buscar", "que filtros hay que plugins tengo hay algun filtro para buscar "
                            "un efecto autotune reverb eco delay coro que la musica baje cuando "
                            "hablan ducking siseo sincronizar audio con imagen mas de doscientos"},
    {"persona_identificar", "quien esta en camara quien aparece reconocer a alguien identificar cara"},
    {"rostro_guardar", "dar de alta una persona ensenarle una cara agregar al sistema corregir ficha"},
    {"rostro_listar", "que personas hay guardadas libreta conocidas"},
    {"rostro_borrar", "borrar una persona olvidar una cara sacar de la libreta"},
    {"rostro_calidad", "sirve esta cara que le pido acercarse mirar de frente luz borrosa "
                       "enfoque alta guiada invitado webrtc no me reconoce"},
    {"rostro_sumar", "sumarle otra cara pose angulo mejorar el reconocimiento ensenarle mas "
                     "girar la cabeza que se mueva"},
    {"rostro_pendientes", "sin nombre desconocidos anotados solos completar fichas quien es este"},
    {"rostro_fusionar", "juntar dos fichas duplicada misma persona dos veces unir"},
    {"atencion_estado", "quien atiende a los que llegan recepcion lobby atencion hablando voz asistente atiende "
                        "a quien esta atendiendo"},
    {"atencion_configurar", "los atiende el asistente atender hablando con voz prender atencion lobby asamblea "
                            "sesion remota microfono del que espera alta automatica anotar en personas voz del "
                            "atendedor"},
    {"atencion_voz_probar", "probar la voz del atendedor como suena generar audio sin reproducir wav"},
    {"totem_listar", "que totems hay paginas de registro configuradas que pregunta cada uno"},
    {"totem_crear", "crear un totem armar un registro nuevo pedir nombre correo telefono a los que llegan "
                    "registrar gente hablando"},
    {"totem_cambiar", "cambiar las preguntas del registro agregar un campo saludo despedida que pida el telefono "
                      "confirmar los datos"},
    {"totem_borrar", "borrar un totem sacar el registro"},
    {"totem_enlace", "link del totem url del registro qr para registrarse copiar el enlace renovar"},
    {"totem_proyectar", "mandar el registro a la tele proyectar el qr del registro en un televisor chromecast"},
    {"totem_estado", "quien se esta registrando ahora conexiones del totem en que paso va que le entendio"},
    {"totem_cortar", "cortar la conexion de un totem colgado trabado echar desconectar celular tablet del registro"},
    {"totem_registrados", "quienes se registraron lo que contestaron datos del registro correos telefonos "
                          "lista de inscriptos"},
    // OJO: documento_leer/documento_tipos son documentos de IDENTIDAD (el DNI por su código
    // de barras); documento_agregar/texto/abrir son los PDF del evento. Las palabras no se cruzan.
    {"documento_leer", "leer el dni escanear el documento de identidad codigo de barras pdf417 cedula licencia "
                       "de conducir pasaporte credencial mostrar el dni a la camara probar el lector del totem "
                       "identificar a la persona por su documento dorso parte de atras renglones mrz zona de lectura mecanica ocr"},
    {"documento_tipos", "que documentos de identidad entiende tipos de dni licencias pasaportes que acepta el totem "
                        "documentos de otros paises plugin de documentos se puede leer el dorso cedula mrz"},
    {"totem_atencion_enlace", "link de atencion remota del registro qr para el equipo atender los totems desde "
                              "el celular mirar y escuchar las conversaciones ayudar a la gente renovar"},
    {"totem_atender", "atender una conversacion del totem tomar el registro pausar al asistente callar al asistente "
                      "devolver al asistente guardar el registro ya cancelar intervenir ayudar a alguien que no puede"},
    {"totem_dato", "completar un dato del registro corregir el correo poner el telefono cambiar el nombre "
                   "borrar un dato que quedo mal escribirle los datos"},
    {"rostro_autoalta", "anotar solo automatico registrar a todos los que pasen interruptor "
                        "switch dar de alta sola lobby los que llegan"},
    {"rostro_lobby", "quien espera quien llego quien esta en el lobby reconocer a los que entran "
                     "sala de espera identificar antes de que entre "
                     // Al reescribir su descripcion para que no compitiera con
                     // admitir, perdio las palabras que la encontraban. Las
                     // etiquetas no dependen de como quedo redactada.
                     "no me acuerdo como se llama quien es el que espera como se llama el que llego"},
    {"traducir", "traducir traduccion idioma ingles portugues frances aleman ruso chino como se dice probar"},
    {"traduccion_",
     "traduccion en vivo subtitulos en otro idioma traducir al orador doblaje pista en ingles bajar idioma"},
    {"traducciones", "traducciones activas en vivo idiomas que se estan traduciendo demora"},
    {"superres_plugins", "plugins superresolucion lista modelos realesrgan esrgan fsrcnnx anime4k instalados disponibles"},
    {"superres_instalar", "instalar bajar plugin superresolucion modelo coreml onnx tensorrt"},
    {"superres_plugin_elemento", "crear plugin propio elemento gstreamer superresolucion tensorrt cuda memoria gpu contrato"},
    {"superres_plugin_borrar", "borrar desinstalar plugin superresolucion"},
    {"superres_variantes", "variantes superresolucion resolucion tamano fijo compilada coreml onnx camara red"},
    {"superres_variante_crear", "crear compilar variante resolucion tamano entrada fijo coreml neural engine tensorrt onnx camara"},
    {"superres_variante_editar", "editar variante para que descripcion superresolucion"},
    {"superres_variante_borrar", "borrar variante superresolucion tamano"},
    {"superres_destilar", "destilar destilacion entrenar red propia maestro esrgan imitar camara superresolucion"},
    {"entrada_control", "control evento entrada gstreamer cadena propagar efecto senal activar"},
    {"superres_programador", "programador superresolucion preview programa activar reposo camaras gpu capacidad"},
    {"estudio_latencia", "latencia del estudio demora atraso tope llega tarde camara lenta compensacion sincronia sincronizar camaras quien marca la latencia bajar latencia colchon alinear entradas en vivo salto"},
    {"sincronia_clase", "sincronizar entrada con la sala propia demora invitado webrtc remoto no arrastrar latencia clase de sincronia camaras alineadas desfase entre camaras"},
    {"audio_estudio", "vista general todo el audio cadenas pipelines fuentes rack mezcla de un vistazo"},
    {"automixer", "automixer mesa redonda dugan bancas microfonos ganancia el que habla"},
    {"entrada_filtros_audio", "audio filtros entrada plugin python compresor ecualizador eco gstreamer cadena"},
    {"superres_banco", "banco pruebas benchmark medir maquina capacidad superresolucion acelerador en vivo rapido cuanto tarda"},
    {"superres_runtime_gpu", "gpu placa nvidia amd intel cuda tensorrt rocm migraphx openvino directml runtime onnx"},
    {"superres_runtime_gpu_instalar", "bajar instalar runtime gpu nvidia cuda tensorrt amd rocm intel openvino"},
    {"traduccion_agregar",
     "traducir en vivo tarjeta traducir rack de audio subtitulos en ingles otro idioma salida de texto"},
    {"traduccion_ajustar", "cambiar pausar quitar tarjeta traducir idioma espera rotulo archivo"},
    {"glosario_", "glosario terminos vocabulario asamblea como se traduce palabra nombre propio "
                  "corregir traduccion"},
    {"voz_cortes", "no corta nunca tarda en traducir pausa silencio cortar frase largo maximo umbral ruido "
                   "sala detector de voz"},
    {"texto_publicar",
     "escribir subtitulo a mano mandar texto a una salida de texto probar subtitulos sin hablar"},
    {"subtitulos_limpiar", "borrar subtitulos sacar subtitulo de pantalla limpiar cuadro de subtitulos"},
    {"superres_", "superresolucion mejorar resolucion camara baja definicion vieja pal agrandar nitidez "
                  "upscale escalar entrenar modelo fsr span esrgan"},
    {"audio_ordenar", "ordenar acomodar tarjetas del rack de audio grilla prolijo"},
    {"video_ordenar", "ordenar acomodar tarjetas del grafo de video franjas prolijo"},
    {"plugin_panel", "panel del plugin controles deslizador boton mover control del plugin indicador"},
    {"ventanas_ordenar", "ordenar acomodar las ventanas repartir entre monitores que no se "
                         "amontonen ordena las ventanas armar el puesto"},
    {"monitor_identificar", "que pantalla es cual cual es cada monitor identificar las pantallas identificar "
                            "pantallas cual es cada monitor numero nombre "
                            "cual es el de la izquierda mostrar el numero"},
    {"monitors", "monitores conectados resolucion cuantas pantallas hay segundo monitor"},

    // --- Las que no tenían ninguna (revisado contra tools/list el 2026-10-08) ---------
    {"botonera_cara", "cara del boton foto del boton video en un boton imagen del boton icono tecla "
                      "stream deck capturar de una camara foto de la camara gif animado"},
    {"historico_buscar", "buscar en todos los eventos cuando se dijo quien hablo de que tema buscar en las "
                         "transcripciones buscar una votacion vieja"},
    {"historico_restaurar", "volver a la configuracion de un evento restaurar como estaba deshacer cambios "
                            "configuracion vieja dejarlo como en la sesion anterior"},
    {"historico_traer_del_dia", "asignar grabaciones viejas a un evento juntar las grabaciones del dia "
                                "grabaciones sueltas"},
    {"historico_retencion", "espacio en disco borrar grabaciones viejas cuanto disco queda limpieza "
                            "liberar espacio retencion"},
    {"historico_exportar", "exportar un evento copiar un evento a un disco llevarse la sesion entregar "
                           "las grabaciones pendrive"},
    {"historico_ajustes", "que se registra que se guarda en la bitacora jornada automatica privacidad"},
    {"traduccion_bajar", "bajar idioma descargar paquetes de traduccion agregar un idioma ingles portugues "
                         "frances aleman ruso chino sin internet"},
    {"glosario_ver", "ver el glosario que terminos respeta la traduccion"},
    {"glosario_agregar", "agregar un termino corregir una traduccion se traduce mal nombre propio sigla "
                         "como se dice en ingles"},
    {"glosario_quitar", "sacar un termino apagar un grupo de terminos"},
    {"motor_mapa", "gpu cpu donde pasa el video copias memoria rendimiento lento placa de video "
                   "pipelines del motor diagnostico de video"},
    {"superres_estado", "que motores de superresolucion hay modelos instalados progreso del entrenamiento"},
    {"superres_tarjeta", "poner superresolucion en una camara agrandar una entrada mejorar una camara "
                         "de baja resolucion tarjeta de superresolucion"},
    {"superres_aplicar", "superresolucion en un stream de red agrandar una entrada de red"},
    {"superres_modelo", "bajar un modelo de superresolucion span esrgan real esrgan convertir a onnx"},
    {"superres_entrenar", "entrenar una red para esta camara aprender de la camara mejorar a medida "
                          "entrenamiento"},
    {"plugin_ver", "ver el codigo de un plugin leer un ejemplo manifiesto archivos del plugin"},
    {"plugin_desde_ejemplo", "copiar un ejemplo empezar un plugin desde un ejemplo plantilla de plugin"},
    {"plugin_reiniciar", "reiniciar un plugin recargar despues de cambiar el codigo no responde"},
    {"plugin_registro", "errores de un plugin que imprimio log del plugin por que falla traceback"},
    {"plugin_borrar", "borrar un plugin sacar un plugin desinstalar"},
    {"dcn_simular_prioridad", "simular el boton de prioridad del presidente prueba sin central"},
    {"dcn_funciones", "funciones sin documentar de la central open interface lo que no esta en el manual"},
    {"dcn_funcion", "llamar una funcion sin documentar de la central comando especial"},
    {"dcn_proximo", "quien sigue el proximo en hablar a quien le toca votar siguiente orador de la central"},
    {"dcn_cerrar_tras_siguiente", "cerrar los microfonos al pasar al siguiente apagar el que hablaba"},
    {"dcn_llamado_nominal", "orden del llamado nominal votacion nominal en que orden votan las bancas"},
    {"dcn_simular_aviso", "simular un aviso de la central probar votacion pedida desde la consola"},

    {"perfil", "rendimiento lento cpu gpu consumo que consume uso del procesador placa de video hilos "
               "perfilar diagnostico de rendimiento por que va lento se traba"},
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

std::vector<std::string> etiquetasDe(const std::string& nombre, const std::vector<std::string>& antes) {
    std::vector<std::string> out;
    // la familia del nombre de hoy y la de cada nombre viejo: al renombrar
    // (capture_program -> foto_programa, evento_cue -> guion_cue) la herramienta
    // no pierde las palabras de su familia
    std::vector<std::string> nombres{nombre};
    nombres.insert(nombres.end(), antes.begin(), antes.end());
    for (const Fila& f : kFamilias) {
        const std::string clave = f.clave;
        if (std::none_of(nombres.begin(), nombres.end(),
                         [&](const std::string& n) { return n.compare(0, clave.size(), clave) == 0; }))
            continue;
        std::string cur;
        for (const char* p = f.terminos;; ++p) {
            if (*p == ' ' || *p == 0) {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
                if (*p == 0) break;
            } else {
                cur.push_back(*p);
            }
        }
    }
    for (const std::string& n : nombres)
        if (const auto it = tabla().find(n); it != tabla().end())
            out.insert(out.end(), it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace ba::app::mcp
