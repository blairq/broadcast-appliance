// ============================================================================
// src/app/mcp/McpEtiquetas.cpp — ver McpEtiquetas.hpp
// ============================================================================
#include "mcp/McpEtiquetas.hpp"

#include <algorithm>
#include <map>

#include "i18n/I18n.hpp"

namespace ba::app::mcp {
namespace {

// Las etiquetas van en el idioma del operador, como las descripciones: son las
// palabras con las que ESA persona pediría la herramienta. Cada fila está marcada
// con QT_TRANSLATE_NOOP("mcp-etiquetas", …) y se traduce al leerla (etiquetasDe):
// el que traduce pone sinónimos y maneras de pedirlo en su idioma, no una
// traducción palabra por palabra.
struct Fila {
    const char* clave;
    const char* terminos; ///< separados por espacios (marcados para traducir)
};

/// Lo que comparte una familia entera. Se aplica por prefijo del nombre, así
/// que `youtube_live_health` recibe lo de `youtube_` sin repetirlo 15 veces.
const Fila kFamilias[] = {
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_", QT_TRANSLATE_NOOP("mcp-etiquetas", "evento guion rundown escaleta programa del dia bloque cue orden del evento produccion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_", QT_TRANSLATE_NOOP("mcp-etiquetas", "linea de tiempo timeline escaleta rundown pasos piezas pronostico hora del evento "
               "conduccion guion del evento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_", QT_TRANSLATE_NOOP("mcp-etiquetas", "escena escenas placa composicion layout toma camara aire vivo obs")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"source_", QT_TRANSLATE_NOOP("mcp-etiquetas", "fuente fuentes capa elemento objeto imagen video texto navegador captura obs")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_", QT_TRANSLATE_NOOP("mcp-etiquetas", "audio sonido volumen nivel ganancia db decibeles mezcla mixer canal pista "
               "se escucha se oye suena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"capture_", QT_TRANSLATE_NOOP("mcp-etiquetas", "captura pantallazo screenshot foto imagen mirar ver revisar vista previa como se ve")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"room_", QT_TRANSLATE_NOOP("mcp-etiquetas", "sala videollamada reunion conferencia webrtc invitados lobby")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_", QT_TRANSLATE_NOOP("mcp-etiquetas", "invitado participante persona asistente publico gente lobby entrar admitir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"chat_", QT_TRANSLATE_NOOP("mcp-etiquetas", "chat mensaje texto escribir conversacion comentario")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"share_", QT_TRANSLATE_NOOP("mcp-etiquetas", "archivo compartido adjunto subida documento foto pdf")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speaker_", QT_TRANSLATE_NOOP("mcp-etiquetas", "orador expositor panelista disertante fila cola turno atril escenario")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speakers_", QT_TRANSLATE_NOOP("mcp-etiquetas", "orador expositor panelista fila cola turno backstage")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ui_", QT_TRANSLATE_NOOP("mcp-etiquetas", "interfaz pantalla ventana app vista pestaña navegar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dashboard_", QT_TRANSLATE_NOOP("mcp-etiquetas", "tablero dashboard tablet panel control remoto botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_web_", QT_TRANSLATE_NOOP("mcp-etiquetas", "control web dashboard consola navegador tablet notebook aparato emparejar qr perfil permisos red local")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_", QT_TRANSLATE_NOOP("mcp-etiquetas", "youtube yt google transmitir stream vivo directo plataforma red social")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_", QT_TRANSLATE_NOOP("mcp-etiquetas", "twitch transmitir stream vivo directo plataforma red social gaming")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_", QT_TRANSLATE_NOOP("mcp-etiquetas", "facebook meta fb transmitir stream vivo directo plataforma red social")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "alerta aviso notificacion avisame vigilar vigilancia monitorear disparador disparadores trigger")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"assistant_", QT_TRANSLATE_NOOP("mcp-etiquetas", "asistente ia llm modelo claude copiloto ayudante")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"account_", QT_TRANSLATE_NOOP("mcp-etiquetas", "cuenta credenciales login acceso usuario clave conectar plataforma")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"accounts_", QT_TRANSLATE_NOOP("mcp-etiquetas", "cuenta credenciales login acceso plataformas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_", QT_TRANSLATE_NOOP("mcp-etiquetas", "grafico placa zocalo lower third animacion html overlay superposicion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rotulo_", QT_TRANSLATE_NOOP("mcp-etiquetas", "rotulo zocalo nombre cargo placa identificacion lower third")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"log_", QT_TRANSLATE_NOOP("mcp-etiquetas", "registro log bitacora diagnostico error historial que paso depurar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"receta_", QT_TRANSLATE_NOOP("mcp-etiquetas", "receta automatizacion macro secuencia guardada atajo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_", QT_TRANSLATE_NOOP("mcp-etiquetas", "agente automatico rol ia bot trabajador delegar encargar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_", QT_TRANSLATE_NOOP("mcp-etiquetas", "modelo local ia de esta maquina qwen motor local bajar modelo sin internet")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_", QT_TRANSLATE_NOOP("mcp-etiquetas", "acceso remoto desde afuera tunel pinggy dominio propio direccion publica internet "
                       "que entren de afuera conectarse de lejos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descarga_", QT_TRANSLATE_NOOP("mcp-etiquetas", "descarga bajar actualizar actualizacion version nueva modelo agente cola de descargas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descargas_", QT_TRANSLATE_NOOP("mcp-etiquetas", "descargas bajar actualizaciones version nueva modelos agentes cola de descargas que se esta bajando")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_", QT_TRANSLATE_NOOP("mcp-etiquetas", "voz hablar escuchar dictado microfono en voz alta asistente por voz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_", QT_TRANSLATE_NOOP("mcp-etiquetas", "teleprompter prompter apuntador discurso texto orador leer guion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_", QT_TRANSLATE_NOOP("mcp-etiquetas", "pantalla televisor tv monitor chromecast proyector cartel proyectar castear")},
    // OJO, son DOS cosas distintas y la palabra «carrusel» lleva a las dos:
    // carrusel_* rota mensajes y QR en una pantalla secundaria; laminas_* es la
    // tira de imagenes de una escena (la presentacion del orador).
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"carrusel_", QT_TRANSLATE_NOOP("mcp-etiquetas", "carrusel rotacion pasos secuencia pantalla cartelera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_", QT_TRANSLATE_NOOP("mcp-etiquetas", "lamina diapositiva presentacion powerpoint pptx pdf carrusel imagenes tira "
                 "slide placa volver atras saltar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superficie_", QT_TRANSLATE_NOOP("mcp-etiquetas", "superficie pagina pantalla web canal control telemetria viva")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"qr_", QT_TRANSLATE_NOOP("mcp-etiquetas", "qr codigo escanear celular enlace wifi contacto vcard")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grilla_personas_", QT_TRANSLATE_NOOP("mcp-etiquetas", "grilla mosaico galeria cuadricula personas gente conectados invitados integrantes caras todos a la vez reparto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_", QT_TRANSLATE_NOOP("mcp-etiquetas", "control windows escritorio mouse teclado captura pantalla programa ejecutar administrador "
                 "permisos computadora maquina pc")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_", QT_TRANSLATE_NOOP("mcp-etiquetas", "medio archivo video musica audio cancion tema clip pelicula reproducir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_", QT_TRANSLATE_NOOP("mcp-etiquetas", "totem registro de personas registrarse acreditacion inscripcion formulario hablado kiosco "
               "pantalla de bienvenida tv con camara qr para registrarse datos de los asistentes")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_", QT_TRANSLATE_NOOP("mcp-etiquetas", "rostro cara persona quien es reconocer identificar nombre cargo libreta "
                "caras conocidas quien llego quien esta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ventana_", QT_TRANSLATE_NOOP("mcp-etiquetas", "ventana monitor pantalla escritorio mover acomodar ordenar posicion tamaño "
                 "multimonitor dos monitores segundo monitor puesto de trabajo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_", QT_TRANSLATE_NOOP("mcp-etiquetas", "transmitir emitir salir al aire vivo directo rtmp stream broadcast")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"record_", QT_TRANSLATE_NOOP("mcp-etiquetas", "grabar grabacion guardar archivo mp4 registro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"settings_", QT_TRANSLATE_NOOP("mcp-etiquetas", "configuracion ajuste preferencia opcion parametro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"system_", QT_TRANSLATE_NOOP("mcp-etiquetas", "sistema equipo maquina hardware version actualizar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"operator_", QT_TRANSLATE_NOOP("mcp-etiquetas", "operador humano persona preguntar avisar consultar cabina")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ext_", QT_TRANSLATE_NOOP("mcp-etiquetas", "extension plugin modulo complemento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ndi_", QT_TRANSLATE_NOOP("mcp-etiquetas", "ndi red camara remota fuente por red")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"network_", QT_TRANSLATE_NOOP("mcp-etiquetas", "red ip conexion internet ancho de banda")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_", QT_TRANSLATE_NOOP("mcp-etiquetas", "video camara webcam dispositivo entrada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mixer_", QT_TRANSLATE_NOOP("mcp-etiquetas", "mezcla audio volumen canal pista mixer")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"transition_", QT_TRANSLATE_NOOP("mcp-etiquetas", "transicion corte fundido disolvencia cambio de escena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guest_", QT_TRANSLATE_NOOP("mcp-etiquetas", "invitado participante persona sala conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"help", QT_TRANSLATE_NOOP("mcp-etiquetas", "ayuda como se hace no se que herramienta buscar manual documentacion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"feeds_", QT_TRANSLATE_NOOP("mcp-etiquetas", "feed señal fuente video vista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitors", QT_TRANSLATE_NOOP("mcp-etiquetas", "monitor pantalla fisica display escritorio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"input_", QT_TRANSLATE_NOOP("mcp-etiquetas", "entrada dispositivo camara microfono captura")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"output_", QT_TRANSLATE_NOOP("mcp-etiquetas", "salida ventana proyeccion monitor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"historico_", QT_TRANSLATE_NOOP("mcp-etiquetas", "historico archivo de eventos eventos anteriores pasados sesiones viejas bitacora "
                   "lo que paso antes historial")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_", QT_TRANSLATE_NOOP("mcp-etiquetas", "superresolucion superres agrandar escalar mejorar resolucion nitidez upscale x2 4k "
                  "imagen chica borrosa")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_", QT_TRANSLATE_NOOP("mcp-etiquetas", "plugin complemento extension python programa propio script")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_", QT_TRANSLATE_NOOP("mcp-etiquetas", "central de conferencia bosch dcn ccu microfonos de banca unidades consola delegados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"glosario_", QT_TRANSLATE_NOOP("mcp-etiquetas", "glosario terminos traduccion vocabulario palabras del evento como se dice")},
};

/// Lo propio de cada herramienta: lo que la distingue de sus hermanas.
const Fila kPropias[] = {
    // --- Ayuda y estado ---------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"help", QT_TRANSLATE_NOOP("mcp-etiquetas", "indice catalogo guia empezar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"help_search", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar herramienta encontrar funcion que hay para")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"status", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado general resumen como esta todo panorama salud")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mcp_aire_lista", QT_TRANSLATE_NOOP("mcp-etiquetas", "aire guardas confirmar acciones peligrosas en vivo transmision bloqueo seguridad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mcp_aire_ajustar", QT_TRANSLATE_NOOP("mcp-etiquetas", "aire guardas agregar sacar herramienta confirmar en vivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_reconocer", QT_TRANSLATE_NOOP("mcp-etiquetas", "reconocer alarma silenciar sonido aviso critico disparador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_fabrica", QT_TRANSLATE_NOOP("mcp-etiquetas", "disparadores fabrica alarmas por defecto restablecer silencio saturacion senal disco")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_chequeo", QT_TRANSLATE_NOOP("mcp-etiquetas", "chequeo lista antes de salir marcar probado ptz casteo respaldo evento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_chequeo_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "chequeo lista antes de salir ver pendientes evento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"escenas_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "editor escenas corte directo armar preview boton al aire")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"aire_ftb", QT_TRANSLATE_NOOP("mcp-etiquetas", "ftb fundido a negro negro salida corte seguridad emergencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"aire_placa", QT_TRANSLATE_NOOP("mcp-etiquetas", "placa seguridad cuarto intermedio volvemos enseguida minuto de silencio cartel emergencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"aire_placas_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "placas textos ftb estado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_destinos", QT_TRANSLATE_NOOP("mcp-etiquetas", "destinos transmision multidestino respaldo varias plataformas simultaneo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_destino_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar destino transmision respaldo multidestino youtube facebook twitch rtmp srt rist passphrase latencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"replay_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "repeticion replay buffer ultimos segundos instant replay")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"replay_guardar", QT_TRANSLATE_NOOP("mcp-etiquetas", "repetir repeticion replay guardar clip ultimos segundos volver a pasar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_scope", QT_TRANSLATE_NOOP("mcp-etiquetas", "scope forma de onda waveform vectorscopio parade histograma niveles exposicion quemado legal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_legalizar", QT_TRANSLATE_NOOP("mcp-etiquetas", "legalizar broadcast safe tv rango legal gama compuesta entregar canal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_api_remota", QT_TRANSLATE_NOOP("mcp-etiquetas", "companion generic http api remota botonera stream deck token acceso http")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_aprobados", QT_TRANSLATE_NOOP("mcp-etiquetas", "plugins aprobados para el aire aprobar plugin hash en vivo seguro firmado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_sincro_medir", QT_TRANSLATE_NOOP("mcp-etiquetas", "medir sincronia labios aplauso claqueta retardo audio video desfasado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_medidores", QT_TRANSLATE_NOOP("mcp-etiquetas", "medidores vumetro ppm vu pico balistica aguja")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"invitado_prueba", QT_TRANSLATE_NOOP("mcp-etiquetas", "prueba tecnica invitado eco acople auriculares red latencia perdida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"proxy_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "proxy dominio dns certificado https lets encrypt vence traefik nombres servicios")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"proxy_renovar", QT_TRANSLATE_NOOP("mcp-etiquetas", "renovar certificado lets encrypt acme proxy")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_configurar_con_api_key", QT_TRANSLATE_NOOP("mcp-etiquetas", "pinggy api key armar todo solo token automatico subdominio fijo cambiar subdominio elegir nombre dns tunel pro sin entrar al panel")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mezclador_pestana", QT_TRANSLATE_NOOP("mcp-etiquetas", "mezclador pestana consola calibracion mostrar ventana")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_entrada_fuente", QT_TRANSLATE_NOOP("mcp-etiquetas", "audio rack tarjeta entrada fuente cambiar traer audio de otra")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_foco", QT_TRANSLATE_NOOP("mcp-etiquetas", "camara ptz foco autofoco enfocar nitido")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_inicio", QT_TRANSLATE_NOOP("mcp-etiquetas", "camara ptz home inicio centrar volver")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_archivos", QT_TRANSLATE_NOOP("mcp-etiquetas", "prompter discurso carpeta renombrar mover borrar ordenar archivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_olvidar", QT_TRANSLATE_NOOP("mcp-etiquetas", "pantalla tv olvidar sacar borrar conocida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_transcripcion_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "voz transcripcion borrar vaciar limpiar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion sala acustica ecualizacion analisis modos rt60 reverberacion schroeder propuesta verificacion eq")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion sala nueva sesion medidas largo ancho alto abrir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_sala", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion medidas sala curva objetivo pendiente")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion parlante microfono punto escucha croquis agregar sm58 medicion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion mover quitar punto parlante microfono calibracion borrar medicion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_medir", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion medir barrido sweep respuesta impulsiva punto parlante microfono rt60 snr")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_medicion", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion medicion resultado respuesta rt60 c50 llegada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_sustitucion", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion microfono sustitucion referencia umik sm58 corregir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"calibracion_aplicar", QT_TRANSLATE_NOOP("mcp-etiquetas", "calibracion aplicar eq sala ecualizador revertir sacar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mezclador_analisis_lsp", QT_TRANSLATE_NOOP("mcp-etiquetas", "lsp analizador espectro fase osciloscopio latencia profiler perfil rt60 ver interfaz abrir medir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mezclador_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "mezclador niveles vumetros entradas salidas loudness lufs pico rms programa sin salida mezcla consola")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_conectar", QT_TRANSLATE_NOOP("mcp-etiquetas", "conectar vpn internet afuera tunel prender apagar desconectar pinggy cloudflare sacar la sala sacar la conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_cortar_tuneles", QT_TRANSLATE_NOOP("mcp-etiquetas", "pinggy cortar bajar tunel matar sesion activa otro equipo mismo token liberar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_web", QT_TRANSLATE_NOOP("mcp-etiquetas", "pagina web del evento sitio frontend en vivo seccion portada cierre html personalizar dominio subdominio url del evento pagina publica")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "pinggy tunel acceso remoto desde afuera internet url publica enlace externo dns cname dominio propio subdominio turn invitados de afuera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"proxy_configurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar proxy dominio dns certificados automaticos lets encrypt traefik 443")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"playlist_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "playlist lista de videos clips armar playlist in out encadenar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"playlist_control", QT_TRANSLATE_NOOP("mcp-etiquetas", "playlist play siguiente anterior parar auto next loop reproducir lista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"salida_limpia", QT_TRANSLATE_NOOP("mcp-etiquetas", "senal limpia clean feed sin graficos sin zocalos sin dsk sin logo programa limpio canal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"salidas_profesionales", QT_TRANSLATE_NOOP("mcp-etiquetas", "srt rist ndi decklink sdi contribucion salidas profesionales")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"salidas_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "salidas programas operadores cuantas salidas varios programas otra salida segundo programa tope multiprograma")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"salida_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear salida nueva otro programa segundo programa otra transmision otro operador sumar salida agregar salida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"salida_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "renombrar salida color de la salida nombre de la salida tope de salidas cambiar salida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"salida_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar salida sacar salida eliminar programa quitar salida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_destino_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar destino transmision")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_destino_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar destino transmision")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"record_disco", QT_TRANSLATE_NOOP("mcp-etiquetas", "disco espacio grabacion horas alcanza remux")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"disco_prioridad", QT_TRANSLATE_NOOP("mcp-etiquetas", "disco lento trabado apuros usb prioridad grabacion programa pausar iso repeticion registro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"record_remux", QT_TRANSLATE_NOOP("mcp-etiquetas", "remux pasar mkv a mp4 mov grabacion convertir sin recodificar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"record_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "grabacion contenedor formato mkv mp4 remux ajustes")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_loudness", QT_TRANSLATE_NOOP("mcp-etiquetas", "loudness lufs r128 integrado true peak lra programa sonoridad volumen salida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_loudness_reset", QT_TRANSLATE_NOOP("mcp-etiquetas", "loudness reset reiniciar integrado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_loudness_objetivo", QT_TRANSLATE_NOOP("mcp-etiquetas", "loudness objetivo r128 a85 -23 -24 -16 -14 lufs")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_rack_vigia", QT_TRANSLATE_NOOP("mcp-etiquetas", "vigia modulos clap lento colgado bypass automatico watchdog audio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_module_rearmar", QT_TRANSLATE_NOOP("mcp-etiquetas", "rearmar modulo saltado lento bypass automatico audio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"invitado_retorno", QT_TRANSLATE_NOOP("mcp-etiquetas", "mix-minus retorno invitado eco se escucha a si mismo oye programa sin su voz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_calidad", QT_TRANSLATE_NOOP("mcp-etiquetas", "calidad invitado red mala bajar resolucion 720p 360p bitrate conexion pixelada cortes")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cuerpo_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "integrantes miembros socios consejeros delegados cuerpo bancas bloque cargo asamblea")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cuerpo_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "cuerpo asamblea nombre crear")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cuerpo_importar", QT_TRANSLATE_NOOP("mcp-etiquetas", "importar planilla csv excel integrantes miembros socios bancas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cuerpo_plantilla", QT_TRANSLATE_NOOP("mcp-etiquetas", "planilla ejemplo integrantes csv")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"integrante_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "integrante miembro agregar editar banca cargo bloque preset camara microfono")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"integrante_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "integrante miembro buscar banca")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"integrante_validar", QT_TRANSLATE_NOOP("mcp-etiquetas", "validar integrante nombre cargo zocalo revisado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"integrante_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar integrante")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"banca_tiempo", QT_TRANSLATE_NOOP("mcp-etiquetas", "sumar tiempo al orador dar mas tiempo cuanto tiempo tiene cada uno para hablar minutos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "banca virtual sesion remota integrantes conectados desde afuera celular quien esta conectado presentes en linea")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_credencial", QT_TRANSLATE_NOOP("mcp-etiquetas", "link personal pin del integrante generar acceso a la sesion remota banca virtual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_clave", QT_TRANSLATE_NOOP("mcp-etiquetas", "usuario y contraseña del integrante entrar sin link tipeando sesion remota")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_datos", QT_TRANSLATE_NOOP("mcp-etiquetas", "datos de contacto del conferencista email telefono nota conexion remota")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_revocar", QT_TRANSLATE_NOOP("mcp-etiquetas", "revocar link sacar acceso del integrante a la sesion remota")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_habilitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "habilitar al integrante que entro por la banca virtual darle presente validar por camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_expulsar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar de la sesion remota desconectar al integrante de la banca virtual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_votacion", QT_TRANSLATE_NOOP("mcp-etiquetas", "votar desde el celular votacion remota abrir votacion sin la central cartel rojo cuenta regresiva")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_acceso_opciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "pin en el qr acceso fijo link y pin fijos que no cambien no renovar credencial permanente integrante asamblea")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_consola", QT_TRANSLATE_NOOP("mcp-etiquetas", "consola web de la presidencia acceso tablet de la presidencia link y pin de la consola")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_registro", QT_TRANSLATE_NOOP("mcp-etiquetas", "registro de la sesion remota quien entro intentos fallidos acta banca virtual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "central de conferencia bosch dcn microfonos de banca estado de la sala quien habla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_microfono", QT_TRANSLATE_NOOP("mcp-etiquetas", "dale la palabra a abri el microfono de cerra el microfono de la banca darle audio cortarle el audio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_siguiente", QT_TRANSLATE_NOOP("mcp-etiquetas", "siguiente orador dale la palabra al siguiente el que sigue en la lista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_cerrar_todos", QT_TRANSLATE_NOOP("mcp-etiquetas", "cerra todos los microfonos callar la sala apagar microfonos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_pedidos", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien pidio la palabra lista de pedidos de palabra quien quiere hablar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_pedido", QT_TRANSLATE_NOOP("mcp-etiquetas", "anotar en la lista de oradores pasar adelante sacar de la lista de pedidos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_pedidos_limpiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar la lista de pedidos vaciar los pedidos de palabra")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_oradores", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien tiene la palabra lista de oradores microfonos abiertos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_orador_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar al orador quitarle la palabra")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_modo", QT_TRANSLATE_NOOP("mcp-etiquetas", "modo de los microfonos operador por voz cuantos microfonos abiertos maximo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_tiempo", QT_TRANSLATE_NOOP("mcp-etiquetas", "avisar que queda un minuto se termino el tiempo en la unidad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_unidades", QT_TRANSLATE_NOOP("mcp-etiquetas", "unidades de la central banca de cada unidad microfonos conectados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_unidad_asignar", QT_TRANSLATE_NOOP("mcp-etiquetas", "aprender unidades asignar unidad a la banca apretar el boton de la banca")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_votacion", QT_TRANSLATE_NOOP("mcp-etiquetas", "votar en la central abrir la votacion cerrar la votacion votacion nominal de la sala")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_votacion_resultado", QT_TRANSLATE_NOOP("mcp-etiquetas", "como va la votacion resultado de la votacion quien voto que")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_asistencia", QT_TRANSLATE_NOOP("mcp-etiquetas", "presentes en la sala asistencia de la central quien esta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_votacion_a_quorum", QT_TRANSLATE_NOOP("mcp-etiquetas", "pasar la votacion a quorum resultado oficial al acta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_presentes_quorum", QT_TRANSLATE_NOOP("mcp-etiquetas", "quorum con los presentes de la central asistencia para el quorum")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_simular_voto", QT_TRANSLATE_NOOP("mcp-etiquetas", "simular voto probar votacion sin la central")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_simular_presencia", QT_TRANSLATE_NOOP("mcp-etiquetas", "simular presente ausente probar asistencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_conectar", QT_TRANSLATE_NOOP("mcp-etiquetas", "conectar la central bosch dcn puerto serie rs232 ccu conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_desconectar", QT_TRANSLATE_NOOP("mcp-etiquetas", "soltar la central desconectar la ccu")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_puertos", QT_TRANSLATE_NOOP("mcp-etiquetas", "puertos serie adaptador usb serie")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_traza", QT_TRANSLATE_NOOP("mcp-etiquetas", "tramas de la central diagnostico protocolo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_simular_boton", QT_TRANSLATE_NOOP("mcp-etiquetas", "simular un pedido de palabra probar sin la central")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"integrante_foto", QT_TRANSLATE_NOOP("mcp-etiquetas", "foto del integrante cara del boton subir foto capturar foto con la camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"recinto_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "grilla del recinto bancas plano de la sala donde se sienta cada uno")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"recinto_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "armar la grilla del recinto acomodar bancas mover banca filas columnas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"recinto_apuntar", QT_TRANSLATE_NOOP("mcp-etiquetas", "apuntar la camara a la banca preparar el plano de un integrante")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_posicion_foto", QT_TRANSLATE_NOOP("mcp-etiquetas", "foto de la posicion miniatura como se ve el plano guardado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"invitado_info_aprobar", QT_TRANSLATE_NOOP("mcp-etiquetas", "aprobar cargo invitado zocalo validar info rotulo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"publico_chat_leer", QT_TRANSLATE_NOOP("mcp-etiquetas", "chat mensajes recientes publico destacar" " comentarios del publico chat del vivo leer el chat lo que escribe la gente")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"chat_destacar", QT_TRANSLATE_NOOP("mcp-etiquetas", "destacar mensaje chat aire pantalla publico pregunta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"chat_quitar_destacado", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar destacado chat")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_interlock", QT_TRANSLATE_NOOP("mcp-etiquetas", "interlock ptz camara al aire no mover bloqueo seguridad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"recinto_bancas_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "bancas oradores presenciales fila integrantes recinto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"banca_cola", QT_TRANSLATE_NOOP("mcp-etiquetas", "fila banca agregar quitar orador presencial" " anotar en la fila pedir la palabra lista de oradores presenciales")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"banca_al_aire", QT_TRANSLATE_NOOP("mcp-etiquetas", "banca al aire orador presencial preset camara microfono zocalo consejero habla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"recinto_orador_terminar", QT_TRANSLATE_NOOP("mcp-etiquetas", "terminar orador presencial banca" " cortar quitar la palabra terminar el turno del que habla en el recinto concejal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"entrada_respaldo", QT_TRANSLATE_NOOP("mcp-etiquetas", "sin senal entrada caida respaldo escena cortar armar camara perdida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cc_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "subtitulos ocultos closed captions cc 608 accesibilidad sordos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cc_activar", QT_TRANSLATE_NOOP("mcp-etiquetas", "prender subtitulos ocultos closed captions cc 608 youtube accesibilidad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cc_desactivar", QT_TRANSLATE_NOOP("mcp-etiquetas", "apagar subtitulos ocultos cc")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"cc_probar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar subtitulos ocultos cc")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"eventos_temas", QT_TRANSLATE_NOOP("mcp-etiquetas", "bus eventos temas tally aire niveles alarmas suscribir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"eventos_ultimo", QT_TRANSLATE_NOOP("mcp-etiquetas", "ultimo valor tema evento bus")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"eventos_esperar", QT_TRANSLATE_NOOP("mcp-etiquetas", "esperar evento tema reaccionar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_perfiles", QT_TRANSLATE_NOOP("mcp-etiquetas", "botonera companion stream deck tablet perfiles puestos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_plantillas", QT_TRANSLATE_NOOP("mcp-etiquetas", "plantillas botonera director audio asamblea prompter")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_perfil_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear perfil botonera puesto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_perfil_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar perfil botonera permisos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_perfil_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar perfil botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_exportar", QT_TRANSLATE_NOOP("mcp-etiquetas", "exportar perfil botonera json")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_importar", QT_TRANSLATE_NOOP("mcp-etiquetas", "importar perfil botonera json")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_paginas", QT_TRANSLATE_NOOP("mcp-etiquetas", "paginas botonera grilla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_pagina_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear pagina botonera grilla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_pagina_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar pagina botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_pagina_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar pagina botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_celdas", QT_TRANSLATE_NOOP("mcp-etiquetas", "celdas botones botonera estado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_celda_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "boton fader perilla celda botonera accion tally ligadura")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_celda_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar boton celda botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_presionar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar boton apretar botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_dispositivos", QT_TRANSLATE_NOOP("mcp-etiquetas", "tablets aparatos stream deck botonera conectados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_dispositivo_asignar", QT_TRANSLATE_NOOP("mcp-etiquetas", "asignar perfil tablet aparato botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_emparejar", QT_TRANSLATE_NOOP("mcp-etiquetas", "emparejar tablet qr botonera codigo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_revocar", QT_TRANSLATE_NOOP("mcp-etiquetas", "revocar tablet token botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_auditoria", QT_TRANSLATE_NOOP("mcp-etiquetas", "auditoria registro botonera acciones")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mcp_token_rotar", QT_TRANSLATE_NOOP("mcp-etiquetas", "rotar token mcp seguridad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"seguridad_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "seguridad token pin sesion accesos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_salud", QT_TRANSLATE_NOOP("mcp-etiquetas", "salud transmision kbps perdidos congestion reconexiones")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_cut", QT_TRANSLATE_NOOP("mcp-etiquetas", "cut corte seco escena armada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"transicion_manual", QT_TRANSLATE_NOOP("mcp-etiquetas", "t-bar palanca transicion manual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"escena_transicion", QT_TRANSLATE_NOOP("mcp-etiquetas", "transicion de entrada escena stinger")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_afv", QT_TRANSLATE_NOOP("mcp-etiquetas", "audio sigue al video afv")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_restante", QT_TRANSLATE_NOOP("mcp-etiquetas", "cuanto le queda video medio al aire cuenta regresiva")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"supervisor_caidas", QT_TRANSLATE_NOOP("mcp-etiquetas", "caidas reinicio causa crash supervisor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"config_generaciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "configuracion version anterior restaurar corrupta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modo_vivo_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "modo en vivo proteger paginas candado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modo_vivo_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "ajustes modo en vivo paginas protegidas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"notificador_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "notificadores avisos telegram mail webhook")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"notificador_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar notificador telegram mail smtp webhook")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"notificador_probar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar notificador aviso")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"notificador_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar notificador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acciones_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "acciones catalogo take ftb atajos un golpe")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"atajos_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "atajos teclado teclas pedal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"atajo_asignar", QT_TRANSLATE_NOOP("mcp-etiquetas", "asignar atajo tecla global pedal prompter")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"atajo_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar atajo tecla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"accion_ejecutar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ejecutar accion atajo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"glosario_exportar", QT_TRANSLATE_NOOP("mcp-etiquetas", "exportar glosario csv revision traduccion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"glosario_importar", QT_TRANSLATE_NOOP("mcp-etiquetas", "importar glosario revisado csv traduccion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"traduccion_rotulo", QT_TRANSLATE_NOOP("mcp-etiquetas", "rotulo traduccion automatica dsk aviso")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"subtitulos_correccion", QT_TRANSLATE_NOOP("mcp-etiquetas", "correccion subtitulos demora corrector humano")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"subtitulos_pendientes", QT_TRANSLATE_NOOP("mcp-etiquetas", "frases pendientes corregir subtitulos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"subtitulos_puesto", QT_TRANSLATE_NOOP("mcp-etiquetas", "puesto corrector subtitulos enlace web corregir desde otra computadora estenotipista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"subtitulos_corregir", QT_TRANSLATE_NOOP("mcp-etiquetas", "corregir aprobar descartar frase subtitulos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"quorum_regla", QT_TRANSLATE_NOOP("mcp-etiquetas", "quorum mayoria simple absoluta dos tercios miembros reglas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"quorum_presentes", QT_TRANSLATE_NOOP("mcp-etiquetas", "hay quorum presentes")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"votacion_resultado_oficial", QT_TRANSLATE_NOOP("mcp-etiquetas", "resultado oficial votacion tablero afirmativos negativos abstenciones nominal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"chat_a_preguntas", QT_TRANSLATE_NOOP("mcp-etiquetas", "sumar chat a preguntas cola youtube")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"orden_del_dia_cargar", QT_TRANSLATE_NOOP("mcp-etiquetas", "orden del dia puntos expedientes sesion cargar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"orden_del_dia_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "orden del dia puntos uso de la palabra tiempos capitulos" " que se trata ahora punto actual temario")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"orden_del_dia_punto", QT_TRANSLATE_NOOP("mcp-etiquetas", "punto orden del dia tratamiento votado comision siguiente")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"seguir_microfono", QT_TRANSLATE_NOOP("mcp-etiquetas", "seguir microfono abierto banca pidio la palabra automatico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"seguir_microfono_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "seguir microfono estado propuesta banca")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dsk_capas_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "capas dsk logo mosca interprete lsa encima programa")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dsk_definir", QT_TRANSLATE_NOOP("mcp-etiquetas", "definir capa dsk logo interprete lsa lengua de senas recuadro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dsk_mostrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "subir bajar capa dsk logo interprete lsa")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_satellite", QT_TRANSLATE_NOOP("mcp-etiquetas", "companion satellite bitfocus stream deck loupedeck botonera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_satellite_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "companion satellite estado clientes")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado servidor botonera conexiones")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_vocabulario", QT_TRANSLATE_NOOP("mcp-etiquetas", "vocabulario voz texto transcripcion nombres propios")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_vocabulario_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar termino vocabulario transcripcion nombres cuerpo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_vocabulario_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar termino vocabulario transcripcion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_vocabulario_probar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar vocabulario correccion transcripcion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"supervisor_politica", QT_TRANSLATE_NOOP("mcp-etiquetas", "supervisor latido interfaz trabada colgada relanzar reiniciar watchdog aire")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"security_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "seguridad contaminacion sesion permisos aprobacion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"system_info", QT_TRANSLATE_NOOP("mcp-etiquetas", "version compilacion build acerca de datos del equipo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"system_hardware", QT_TRANSLATE_NOOP("mcp-etiquetas", "llenando llena disco almacenamiento espacio queda lugar se esta llenando el disco "
                        "disco lleno espacio libre en disco cuanto "
                        "queda de disco memoria temperatura hardware placa gpu cpu capacidad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sistema_recursos", QT_TRANSLATE_NOOP("mcp-etiquetas", "uso de cpu procesador gpu placa de video memoria ram red ancho de banda "
                         "carga cargada al limite por explotar exigida aguanta otra salida margen picos consumo rendimiento "
                         "monitor de recursos lenta se traba")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"system_update", QT_TRANSLATE_NOOP("mcp-etiquetas", "actualizar version nueva upgrade instalar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"app_quit", QT_TRANSLATE_NOOP("mcp-etiquetas", "cerrar salir apagar terminar el programa")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"app_save", QT_TRANSLATE_NOOP("mcp-etiquetas", "guardar persistir escribir configuracion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitors", QT_TRANSLATE_NOOP("mcp-etiquetas", "monitores conectados resolucion")},

    // --- Escenas y fuentes ------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "cuantas escenas hay listado de escenas listar cuales hay ver escenas")},
    // «Cortá a la cámara 2» es CAMBIAR DE ESCENA: en una cabina las escenas se
    // llaman por su cámara. Sin la palabra «camara» aca, esa frase se iba a las
    // doce herramientas que sí la tienen (capture_feed, ndi_sources...) y el
    // cambio de escena no aparecia por ningun lado.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_switch", QT_TRANSLATE_NOOP("mcp-etiquetas", "corta cortala corte pasa poné pone cambiar poner al aire pasar a cortar a programa "
                     "activa corte directo urgencia "
                     "cortar a camara pasar a camara ir a camara poner la camara "
                     "pone al aire la pone al aire poner al aire la escena arranca con "
                     "escena siguiente la escena que sigue pasar de escena otra escena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_preview", QT_TRANSLATE_NOOP("mcp-etiquetas", "dejar preparada dejala lista preparar la proxima armar la que sigue armar preparar el "
                      "proximo plano bus de preview previo antes de sacarlo "
                      "cuear siguiente escena mesa switcher")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"preview_al_aire", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar al aire lo armado sacar la armada tomar el take ya take sacar al aire la armada "
                   "cortar ahora disparar el plano preparado mesa switcher")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_create", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear nueva agregar escena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_copy", QT_TRANSLATE_NOOP("mcp-etiquetas", "copiar duplicar clonar escena" " hacer otra igual copia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_delete", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar eliminar quitar escena" " eliminar la escena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_rename", QT_TRANSLATE_NOOP("mcp-etiquetas", "renombrar cambiar nombre escena" " rebautizar ponerle otro nombre")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_move", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover ordenar reordenar posicion escena subir bajar en la lista ponerla antes que otra")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_folder", QT_TRANSLATE_NOOP("mcp-etiquetas", "carpeta agrupar organizar escenas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_protect", QT_TRANSLATE_NOOP("mcp-etiquetas", "proteger bloquear candado no tocar impedir cambios dejarla protegida que nadie la "
                      "borre ni la renombre")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_export", QT_TRANSLATE_NOOP("mcp-etiquetas", "exportar respaldo backup copiar a archivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"scene_define", QT_TRANSLATE_NOOP("mcp-etiquetas", "definir armar escena completa de una plantilla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"transition_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "que fundido esta puesto que transicion hay puesta cual es el fundido fundido corte "
                       "disolvencia stinger duracion transicion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"source_types", QT_TRANSLATE_NOOP("mcp-etiquetas", "que tipos de fuente hay disponibles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"escena_fuentes_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "que hay adentro de la escena que tiene la escena que capas tiene contenido de la escena "
                    "listar fuentes de una escena que hay adentro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"crear_fuente",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "agrega la camara agrega a esta escena sumar una camara agregar a la escena sumar a la escena meter en "
     "la escena poner en la escena agregar poner sumar fuente camara imagen texto navegador codigo qr "
     "captura de pantalla capturar una aplicacion ventana")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"captura_opciones",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "que pantallas hay que monitores que ventanas que aplicaciones se pueden capturar captura de pantalla "
     "capturar una ventana una aplicacion elegir monitor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"source_remove", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar sacar borrar fuente" " eliminar sacar de la escena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"source_update",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar editar modificar fuente correr mover el titulo o el zocalo de lugar posicion tamano "
     "sombra paralela resplandor brillo borde difuso desvanecido transparencia opacidad efectos redondear "
     "esquinas redondeadas bordes redondos" " agrandar achicar recuadro pip esquina visibilidad orden de capas adelante atras")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"source_properties", QT_TRANSLATE_NOOP("mcp-etiquetas", "propiedades opciones parametros de una fuente")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"source_set_property",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar una propiedad valor ajuste de fuente refrescar la lista de dispositivos apretar un boton")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guest_source_add", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner invitado en escena mostrar participante camara remota")},

    // --- Audio -------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "acople acoplando pitido cortar el audio bajalo ya urgente "
                  "silenciar mutear callar bajar subir volumen ganancia mute solo "
                  // Como se queja alguien en cabina: no dice «audio_set», dice
                  // que no se escucha. Sin estas palabras, «el invitado no se
                  // escucha» no traia NADA de audio entre los primeros.
                  "no se escucha no se oye sin sonido no tengo sonido no suena "
                  "esta mudo quedo mudo no le sale audio mas fuerte mas bajo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_sincro", QT_TRANSLATE_NOOP("mcp-etiquetas", "desfasado de la imagen no coincide con la imagen labios imagen adelantada sincronizar "
                     "labios desfase desfasado desincronizado adelantado atrasado "
                     "delay offset demora retardo la voz no coincide con la imagen")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_sources", QT_TRANSLATE_NOOP("mcp-etiquetas", "que fuentes de audio hay listar canales")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_escuchar", QT_TRANSLATE_NOOP("mcp-etiquetas", "acople de donde viene el pitido aislar canal "
                       "pfl solo escuchar previo monitorear probar un microfono antes de abrirlo "
                       "auriculares cascos oir sin sacar al aire")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_devices", QT_TRANSLATE_NOOP("mcp-etiquetas", "placa de sonido microfono dispositivo entrada salida hardware")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"gst_elementos", QT_TRANSLATE_NOOP("mcp-etiquetas", "plugins elementos gstreamer faltan diagnostico silencio negro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"inicio_con_el_equipo", QT_TRANSLATE_NOOP("mcp-etiquetas", "arrancar solo al encender corte de luz volver solo inicio automatico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_input_add", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar microfono entrada de audio linea")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_input_remove", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar sacar borrar microfono entrada de audio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_rack", QT_TRANSLATE_NOOP("mcp-etiquetas", "rack cadena procesamiento efectos de un canal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_plantillas", QT_TRANSLATE_NOOP("mcp-etiquetas", "plantilla preset cadena tipica voz musica")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_modules", QT_TRANSLATE_NOOP("mcp-etiquetas", "que efectos hay disponibles procesadores")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_module_add", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar efecto procesador al canal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_module_remove", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar sacar efecto procesador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_module_get", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver parametros de un efecto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_module_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar parametros de un efecto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_patch", QT_TRANSLATE_NOOP("mcp-etiquetas", "ruteo patch enviar a donde va matriz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_bypass", QT_TRANSLATE_NOOP("mcp-etiquetas", "puentear desactivar saltear efecto comparar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_gate", QT_TRANSLATE_NOOP("mcp-etiquetas", "el microfono queda abierto se escucha todo lo de alrededor micro abierto compuerta gate "
                   "ruido de fondo silencio respiracion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_compressor", QT_TRANSLATE_NOOP("mcp-etiquetas", "compresor compresion dinamica nivelar parejo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_deesser", QT_TRANSLATE_NOOP("mcp-etiquetas", "deesser sibilancia ese silbido")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_eq", QT_TRANSLATE_NOOP("mcp-etiquetas", "ecualizador eq graves medios agudos tono frecuencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_multiband", QT_TRANSLATE_NOOP("mcp-etiquetas", "multibanda compresion por bandas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_limiter", QT_TRANSLATE_NOOP("mcp-etiquetas", "limitador limite pico clip saturacion proteger")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_analyze", QT_TRANSLATE_NOOP("mcp-etiquetas", "analizar espectro medir problema ruido zumbido")},
    // ACOPLE, PITIDO, ZUMBIDO: lo que se grita en cabina. Ninguna de estas
    // palabras estaba, y «hay acople» no traia NADA de audio. Es el caso que
    // menos tiempo da para pensar, asi que es el que mejor tiene que andar.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_measure", QT_TRANSLATE_NOOP("mcp-etiquetas", "no tengo sonido sin sonido no sale audio quedamos sin audio pitido chillido silbido "
                      "agudo insoportable suena un pitido no se escucha nada al aire quedamos sin audio "
                      "acople acoplando acoplamiento microfonia feedback pitido chillido "
                      "zumbido ruido de fondo satura saturando satura el audio distorsiona "
                      "medir niveles pico "
                      "medir lufs nivel pico rms loudness normalizar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_generator", QT_TRANSLATE_NOOP("mcp-etiquetas", "tono generador prueba ruido rosa senal de test")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_outputs", QT_TRANSLATE_NOOP("mcp-etiquetas", "salidas de audio a donde sale")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_output_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar salida de audio destino")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_show", QT_TRANSLATE_NOOP("mcp-etiquetas", "mostrar abrir panel de audio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_editor", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar audio abrir editor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"mixer_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "consola mesa canales faders niveles")},

    // --- Transmision y grabacion -------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_start", QT_TRANSLATE_NOOP("mcp-etiquetas", "salimos al aire salgamos al aire arrancamos la transmision empezar a transmitir "
                     "empezar arrancar salir al aire comenzar transmision")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"stream_stop", QT_TRANSLATE_NOOP("mcp-etiquetas", "parar terminar cortar bajar del aire bajanos del aire bajalo del aire "
                    "sacanos del aire cortar la transmision fin de transmision" " terminar la transmision dejar de transmitir cortar el streaming")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"record_start", QT_TRANSLATE_NOOP("mcp-etiquetas", "empezar a grabar arrancar grabacion rec" " grabar empezar la grabacion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"record_stop", QT_TRANSLATE_NOOP("mcp-etiquetas", "para frena detene la grabacion cortar de grabar parar grabacion para la grabacion parar "
                    "de grabar frena la grabacion "
                    "cortar la grabacion terminar de grabar" " detener terminar la grabacion stop")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grabacion_individual", QT_TRANSLATE_NOOP("mcp-etiquetas", "grabar por separado pista aislada isos cada camara")},

    // --- Ver ---------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"foto_programa", QT_TRANSLATE_NOOP("mcp-etiquetas", "quedamos sin imagen no hay imagen pantalla en blanco se fue la imagen que esta al "
                        "aire que se ve ahora salida programa")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"foto_fuente", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver una fuente revisar antes de poner al aire")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"foto_todas_escenas", QT_TRANSLATE_NOOP("mcp-etiquetas", "todas las escenas de una grilla multiview")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"capture_feed", QT_TRANSLATE_NOOP("mcp-etiquetas", "se congelo la camara del invitado su camara quedo trabada imagen congelada ver una "
                     "señal entrante feed camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"foto_invitado", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver a una persona invitado invitada camara de alguien cara como esta saliendo "
                       "como sale como se ve el invitado mostrame al invitado mostrame a la invitada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"foto_todos_invitados",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "ver a todos los invitados grilla de personas toda la gente que hay quienes estan en la sala quienes estan en la conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"capture_sequence", QT_TRANSLATE_NOOP("mcp-etiquetas", "varias capturas seguidas ver que cambia movimiento" " cuadros seguidos congelado trabado titila varias fotos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"capture_ui", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver la interfaz de la app pantalla del operador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"feeds_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "que camaras hay conectadas que señales llegan que señales hay entrando fuentes de video")},

    // --- Sala --------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"room_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "se cayo un invitado se desconecto se fue quien quedo quien esta en la sala en la conferencia conectados "
                    "lista de gente cuanta gente "
                    "cuantos hay cuantos son quienes estan esperando en el lobby "
                    "estado de la sala estado de la conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"room_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar sala configurar conferencia cupo cerrar abrir limite")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"room_pin", QT_TRANSLATE_NOOP("mcp-etiquetas", "pin clave codigo de acceso contrasena de la sala de la conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"room_link", QT_TRANSLATE_NOOP("mcp-etiquetas", "enlace invitacion link para entrar compartir acceso url de la sala url de la conferencia")},
    // las que solo tenian las palabras de su familia (revision de la busqueda, 17/10/2026)
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "subir bajar volumen silenciar mutear desmutear callar cerrar abrir el micro mic acople satura fuerte despacio nivel db")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descarga_actualizar", QT_TRANSLATE_NOOP("mcp-etiquetas", "actualizar instalar version nueva")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descarga_cancelar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cancelar parar cortar la descarga")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descarga_reintentar", QT_TRANSLATE_NOOP("mcp-etiquetas", "reintentar volver a bajar fallo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descargas_buscar_actualizaciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar actualizaciones hay version nueva chequear")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descargas_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado progreso avance cuanto falta velocidad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"descargas_limpiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "limpiar historial borrar la lista terminadas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_activar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cargar activar usar prender el modelo local")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "ajustes sesgo derivar mtp al arrancar configurar el modelo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_apagar", QT_TRANSLATE_NOOP("mcp-etiquetas", "apagar liberar memoria gpu")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_bajar", QT_TRANSLATE_NOOP("mcp-etiquetas", "bajar descargar instalar un modelo catalogo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar eliminar desinstalar liberar espacio disco")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_cancelar_descarga", QT_TRANSLATE_NOOP("mcp-etiquetas", "cancelar la descarga del modelo parar la bajada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_elegir", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar una frase que herramienta elige clasificar test")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "que modelos hay instalados cargado estado vision")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "elegir la voz que contesta hablante velocidad sintesis tts")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_atender", QT_TRANSLATE_NOOP("mcp-etiquetas", "tratar como dictado como si lo dijera el operador atender un pedido")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_automaticas", QT_TRANSLATE_NOOP("mcp-etiquetas", "acciones automaticas sin confirmar sin preguntar permitir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_decir", QT_TRANSLATE_NOOP("mcp-etiquetas", "decir hablar decirle al operador por los auriculares avisar en voz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_microfono", QT_TRANSLATE_NOOP("mcp-etiquetas", "microfono del operador mic de cabina talkback hablarle al lobby")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_reconocedores", QT_TRANSLATE_NOOP("mcp-etiquetas", "reconocedor de voz parakeet nemotron streaming varias voces a la vez interrumpir al asistente "
                          "voz a texto memoria los dos modelos cargados unificar transcripcion traduccion en tiempo real")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_teleprompter", QT_TRANSLATE_NOOP("mcp-etiquetas", "leer el teleprompter en voz alta locutor virtual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_texto", QT_TRANSLATE_NOOP("mcp-etiquetas", "voz a texto dictado transcribir subtitular reconocimiento de voz stt")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_transcripcion", QT_TRANSLATE_NOOP("mcp-etiquetas", "transcripcion lo que se dijo texto transcripto ultimas frases")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_bus_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar crear bus salida virtual entrada virtual envio mix menos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_bus_dispositivo", QT_TRANSLATE_NOOP("mcp-etiquetas", "placa de sonido a donde sale dispositivo de la salida virtual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_bus_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar borrar eliminar bus virtual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_bus_renombrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "renombrar cambiar el nombre bus pista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_dispositivos", QT_TRANSLATE_NOOP("mcp-etiquetas", "dispositivos placas de sonido auriculares salidas del equipo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_filtros_catalogo", QT_TRANSLATE_NOOP("mcp-etiquetas", "catalogo de filtros lsp compresor compuerta ecualizador que filtros hay")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_lsp_restablecer_interfaz", QT_TRANSLATE_NOOP("mcp-etiquetas", "restablecer resetear interfaz lsp ventanas zoom escala")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_matriz", QT_TRANSLATE_NOOP("mcp-etiquetas", "matriz de mezcla ruteo envios a donde va cada fuente buses pistas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_matriz_cruce", QT_TRANSLATE_NOOP("mcp-etiquetas", "cruce rutear mandar una fuente a un bus envio enviar a una pista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_matriz_equipo", QT_TRANSLATE_NOOP("mcp-etiquetas", "entradas y salidas del equipo enchufado parlantes placa usb canales")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"help", QT_TRANSLATE_NOOP("mcp-etiquetas", "ayuda guia como se hace no se como catalogo de herramientas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_web_aparatos", QT_TRANSLATE_NOOP("mcp-etiquetas", "aparatos emparejados tablets conectadas solicitudes dashboard web")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_web_emparejar", QT_TRANSLATE_NOOP("mcp-etiquetas", "emparejar vincular una tablet un celular codigo qr dashboard")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_web_perfil", QT_TRANSLATE_NOOP("mcp-etiquetas", "permisos perfil categorias que puede usar el aparato")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_web_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar revocar desvincular sacarle el acceso a un aparato")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_web_resolver", QT_TRANSLATE_NOOP("mcp-etiquetas", "aprobar rechazar solicitud pedido de acceso de un aparato")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_web_tunel", QT_TRANSLATE_NOOP("mcp-etiquetas", "control web desde afuera tunel pin dashboard remoto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitors", QT_TRANSLATE_NOOP("mcp-etiquetas", "monitores pantallas conectadas resolucion hz pantalla principal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_bitacora", QT_TRANSLATE_NOOP("mcp-etiquetas", "bitacora historial que paso registro cronologia log del evento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_config", QT_TRANSLATE_NOOP("mcp-etiquetas", "configuracion con la que se hizo el evento foto de la config")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_grabaciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "grabaciones archivos vivos transcripciones capitulos del evento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_preparar", QT_TRANSLATE_NOOP("mcp-etiquetas", "preparar senal de ajuste barras tono placa antes de empezar fin de transmision")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_sellos", QT_TRANSLATE_NOOP("mcp-etiquetas", "sellos integridad no se toco intacto sha256")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_verificar", QT_TRANSLATE_NOOP("mcp-etiquetas", "verificar lista de control checklist esta todo listo para salir chequeo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver la linea pronostico a que hora entra cada cosa back time cuanto falta over under escaleta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "que esta al aire que sigue entra solo hold vamos atrasados adelantados hard start corte de luz retoma")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar la linea agregar paso mover quitar cambiar hora fija hard start pieza zocalo en lote probar diff")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_deshacer", QT_TRANSLATE_NOOP("mcp-etiquetas", "deshacer volver atras el cambio de la linea revision anterior")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_revisar", QT_TRANSLATE_NOOP("mcp-etiquetas", "revisar la linea reparos conflictos camaras compartidas hora fija imposible anclas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_ajustar", QT_TRANSLATE_NOOP("mcp-etiquetas", "llegar a horario terminar a las recortar acortar back timing nos pasamos de tiempo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_go", QT_TRANSLATE_NOOP("mcp-etiquetas", "go take entra el siguiente paso adelante dale")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_hold", QT_TRANSLATE_NOOP("mcp-etiquetas", "hold frenar retener que no pase solo esperar automatico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_siguiente", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover el proximo para adelante saltear hasta f10 next")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_anterior", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover el proximo para atras volver a pasar f9")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_saltar", QT_TRANSLATE_NOOP("mcp-etiquetas", "saltear saltar el proximo paso no va")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_ir", QT_TRANSLATE_NOOP("mcp-etiquetas", "ir a un paso volver a saltar a cargar el estado reponer musica graficos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_repetir", QT_TRANSLATE_NOOP("mcp-etiquetas", "repetir el paso otra vez de nuevo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_insertar", QT_TRANSLATE_NOOP("mcp-etiquetas", "insertar un paso del estante plantilla meter algo ahora imprevisto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_adlib", QT_TRANSLATE_NOOP("mcp-etiquetas", "adlib disparar a mano boton del estante aplausos placa zocalo ahora")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_recorrido", QT_TRANSLATE_NOOP("mcp-etiquetas", "lo que paso lo emitido recorrido informe capitulos acta cuando entro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_retomar", QT_TRANSLATE_NOOP("mcp-etiquetas", "volvio la luz corte de luz que esta pasando descripcion retomar reenganchar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"linea_soltar", QT_TRANSLATE_NOOP("mcp-etiquetas", "soltar el hold seguir como estaba despues del corte confirmar decision")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_live_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "vivos de facebook transmisiones lista estado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_login", QT_TRANSLATE_NOOP("mcp-etiquetas", "conectar la cuenta de facebook iniciar sesion login")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_logout", QT_TRANSLATE_NOOP("mcp-etiquetas", "desconectar la cuenta de facebook cerrar sesion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_setup", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar la app de meta id clave secreta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado de facebook conectado pagina")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_video_update", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar borrar un video de facebook titulo descripcion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_video_upload", QT_TRANSLATE_NOOP("mcp-etiquetas", "subir un video a facebook publicar la grabacion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_videos", QT_TRANSLATE_NOOP("mcp-etiquetas", "videos de la pagina de facebook lista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_lobby", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien esta esperando en el lobby reconocer identificar caras")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_clave", QT_TRANSLATE_NOOP("mcp-etiquetas", "clave token api key guardar credencial cloudflare")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_configurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar prender apagar el tunel region dominio turn")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_dns_instrucciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "instrucciones dns cname para el administrador del dominio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_dns_verificar", QT_TRANSLATE_NOOP("mcp-etiquetas", "verificar el dns cname apunta bien")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_panel", QT_TRANSLATE_NOOP("mcp-etiquetas", "panel de la cuenta tuneles activos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"acceso_remoto_probar_direccion", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar una direccion subdominio disponible")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"room_pin_bloqueos", QT_TRANSLATE_NOOP("mcp-etiquetas", "bloqueados por pin mal desbloquear destrabar ip")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_login", QT_TRANSLATE_NOOP("mcp-etiquetas", "conectar la cuenta de twitch iniciar sesion login")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_logout", QT_TRANSLATE_NOOP("mcp-etiquetas", "desconectar la cuenta de twitch cerrar sesion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_setup", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar la app de twitch id de cliente")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado de twitch conectado canal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_video_delete", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar vods videos del canal de twitch")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_videos", QT_TRANSLATE_NOOP("mcp-etiquetas", "vods videos del canal de twitch lista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_tarjeta_ayuda", QT_TRANSLATE_NOOP("mcp-etiquetas", "ayuda de una tarjeta de video parametros que hace gstreamer")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_live_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "eventos en vivo de youtube transmisiones programadas lista")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_live_update", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar un evento de youtube titulo privacidad pasar a vivo terminar borrar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_login", QT_TRANSLATE_NOOP("mcp-etiquetas", "conectar la cuenta de youtube iniciar sesion google login")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_logout", QT_TRANSLATE_NOOP("mcp-etiquetas", "desconectar la cuenta de youtube cerrar sesion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"enlaces_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "enlaces links servicios web urls que esta prendido estado de los servicios lista de enlaces")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"enlace_activar", QT_TRANSLATE_NOOP("mcp-etiquetas", "prender apagar habilitar deshabilitar servicio web companion satellite tunel abrir cerrar la sala cerrar la conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"multiview", QT_TRANSLATE_NOOP("mcp-etiquetas", "multiview multivista muro mosaico de camaras ver varias fuentes juntas pantalla dividida")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"diag_probar_caida", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar una caida volcado crash dump prueba de errores")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"direcciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "direccion ip url enlace link red local tunel afuera puerto donde entra 127.0.0.1 localhost pinggy")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"web_tema", QT_TRANSLATE_NOOP("mcp-etiquetas", "tema apariencia estilo template plantilla neumorfismo claro oscuro paginas web tablet botonera presidencia colores")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"qr_mostrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "qr codigo qr mostrar el qr pasame el qr escanear celular copiar url link enlace portapapeles ventana")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_admit",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "hace pasar hacelo pasar que pase dejalo entrar adentro dejar entrar aceptar admitir aprobar del lobby")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_reject", QT_TRANSLATE_NOOP("mcp-etiquetas", "rechazar no dejar entrar negar acceso")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_kick", QT_TRANSLATE_NOOP("mcp-etiquetas", "echar expulsar sacar desconectar de la sala de la conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_requeue", QT_TRANSLATE_NOOP("mcp-etiquetas", "devolver mandar atras de nuevo al final de la fila el que ya hablo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_admit_all", QT_TRANSLATE_NOOP("mcp-etiquetas", "dejar entrar a todos admitir todos vaciar lobby")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_talk", QT_TRANSLATE_NOOP("mcp-etiquetas", "hablarle privado audio al invitado susurrar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_request", QT_TRANSLATE_NOOP("mcp-etiquetas", "pedile que prenda encienda apague que active su camara su microfono pedirle "
                            "algo pedir camara microfono solicitar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_profile", QT_TRANSLATE_NOOP("mcp-etiquetas", "ficha datos nombre cargo informacion de la persona" " perfil del invitado cambiarle el nombre")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"chat_send",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "mandale un mensaje escribile decile por chat avisale mandar mensaje escribir al chat avisar por chat")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"invitados_chat_leer", QT_TRANSLATE_NOOP("mcp-etiquetas", "que me dijo que dijo que escribio leer lo que mandaron que contesto historial "
                     "conversacion que dijeron leer chat")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"chat_wait", QT_TRANSLATE_NOOP("mcp-etiquetas", "esperar mensaje quedarse esperando a que escriban avisame cuando escriba el invitado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"chat_overlay_add", QT_TRANSLATE_NOOP("mcp-etiquetas", "chat en pantalla mostrar comentarios al aire overlay")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"share_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "que archivos mandaron subidos adjuntos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"compartida_revisar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver archivo mirar adjunto abrir subida revisar lo que mando antes de ponerlo" " ver la imagen compartida mostrar lo que subio el invitado archivo compartido pixelada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"share_take", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner archivo al aire mostrar foto documento subido")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"share_delete", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar archivo subido eliminar adjunto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guest_chat_pending", QT_TRANSLATE_NOOP("mcp-etiquetas", "mensajes sin responder pendientes de invitados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guest_reply_suggest", QT_TRANSLATE_NOOP("mcp-etiquetas", "sugerir respuesta redactar contestacion borrador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"network_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "se esta cortando se corta entrecortado se cae la transmision va lento red internet "
                       "conexion ancho de banda tunel calidad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"gateway_restart",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "reiniciar sala gateway reiniciar servidor de la sala la sala no responde esta colgada reiniciar conferencia la conferencia no responde")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"operator_feed",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "camara del operador cabina mi camara con que imagen me ven y me oyen los que esperan en el lobby")},

    // --- Oradores ----------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"remotos_fila_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien esta en la fila estado de oradores")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speakers_config",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar fila de oradores tiempos reglas activar el modo oradores minutos por orador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speaker_on_air", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner orador al aire darle el aire a quien habla" " dar el aire al orador remoto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speaker_next", QT_TRANSLATE_NOOP("mcp-etiquetas", "pasa al que sigue el siguiente orador proximo orador turno siguiente siguiente orador "
                     "pasar al que sigue avanzar la fila")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speaker_move", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover en la fila reordenar oradores pasar a alguien al primer lugar de la fila")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speaker_queue", QT_TRANSLATE_NOOP("mcp-etiquetas", "fila cola de oradores agregar sacar" " anotar en la lista de oradores remotos cola")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speaker_presenter", QT_TRANSLATE_NOOP("mcp-etiquetas", "presentador conductor moderador anfitrion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"speaker_time", QT_TRANSLATE_NOOP("mcp-etiquetas", "tiempo cronometro reloj cuanto le queda al orador")},

    // --- Interfaz ----------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ui_show_page", QT_TRANSLATE_NOOP("mcp-etiquetas", "ir a la pestaña abrir seccion navegar mostrar pagina personas rostros")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ui_page_window", QT_TRANSLATE_NOOP("mcp-etiquetas", "abrir en ventana aparte sacar a otra ventana")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "que archivos hay musica videos imagenes presentaciones que tengo en medios")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_find", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar un archivo de medios encontrar el logo donde esta ese video")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ui_panel_window", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar el padron o las preguntas o la urna a su ventana panel en otro "
                        "monitor ventana dedicada para moderar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ui_notify", QT_TRANSLATE_NOOP("mcp-etiquetas", "avisar en pantalla toast mensaje al operador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ui_barra", QT_TRANSLATE_NOOP("mcp-etiquetas", "barra lateral menu navegacion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"output_window", QT_TRANSLATE_NOOP("mcp-etiquetas", "ventana de salida proyeccion pantalla completa")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dashboard_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "tableros que hay tablet")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dashboard_card", QT_TRANSLATE_NOOP("mcp-etiquetas", "boton tarjeta del tablero agregar accion direccion al aire armada take "
                       "tiempos del guion desvio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dashboard_background", QT_TRANSLATE_NOOP("mcp-etiquetas", "fondo del tablero imagen color")},

    // --- Plataformas -------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado de youtube conectado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_setup", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar youtube api credenciales")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_videos", QT_TRANSLATE_NOOP("mcp-etiquetas", "videos subidos lista del canal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_video_upload", QT_TRANSLATE_NOOP("mcp-etiquetas", "subir video a youtube publicar archivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_video_update", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar titulo descripcion de un video")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_video_delete", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar video de youtube")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_live_create", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear transmision programar vivo en youtube")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_live_start", QT_TRANSLATE_NOOP("mcp-etiquetas", "empezar vivo en youtube salir al aire")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_live_end", QT_TRANSLATE_NOOP("mcp-etiquetas", "terminar vivo en youtube cortar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_live_health", QT_TRANSLATE_NOOP("mcp-etiquetas", "salud de la transmision calidad problemas youtube")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"youtube_chat", QT_TRANSLATE_NOOP("mcp-etiquetas", "chat de youtube comentarios del vivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_channel", QT_TRANSLATE_NOOP("mcp-etiquetas", "canal de twitch titulo categoria juego")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_live_start", QT_TRANSLATE_NOOP("mcp-etiquetas", "empezar vivo en twitch")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_live_stop", QT_TRANSLATE_NOOP("mcp-etiquetas", "terminar vivo en twitch")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_stream_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado del vivo en twitch espectadores")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"twitch_chat", QT_TRANSLATE_NOOP("mcp-etiquetas", "chat de twitch")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_pages", QT_TRANSLATE_NOOP("mcp-etiquetas", "paginas de facebook cuales hay")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_live_create", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear vivo en facebook")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_live_publish", QT_TRANSLATE_NOOP("mcp-etiquetas", "publicar empezar vivo en facebook")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_live_end", QT_TRANSLATE_NOOP("mcp-etiquetas", "terminar vivo en facebook")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"facebook_chat", QT_TRANSLATE_NOOP("mcp-etiquetas", "chat de facebook comentarios")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"accounts_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "que cuentas hay conectadas plataformas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"account_setup_guide", QT_TRANSLATE_NOOP("mcp-etiquetas", "como conectar una cuenta pasos guia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"account_open", QT_TRANSLATE_NOOP("mcp-etiquetas", "abrir la pagina de la cuenta en el navegador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"account_credentials_request", QT_TRANSLATE_NOOP("mcp-etiquetas", "pedir credenciales al operador clave token")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"account_check", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar cuenta verificar que ande")},

    // --- Alertas y recetas --------------------------------------------------
    // SIN LOS SINTOMAS. Tenia «se cayo la transmision» y «disco lleno» como
    // ejemplos de lo que se puede vigilar, y con eso ganaba las preguntas de
    // cuando esas cosas ESTAN PASANDO —que son de `status` y `system_hardware`,
    // no de leer un catalogo de tipos—.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_types", QT_TRANSLATE_NOOP("mcp-etiquetas", "que se puede vigilar tipos de disparador de vigilancia eventos disponibles parametros")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_create", QT_TRANSLATE_NOOP("mcp-etiquetas", "avisame cuando crear alerta vigilar crear disparador vigilancia cuando digan")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "que alertas hay configuradas disparadores vigilancias")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_update", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar alerta editar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_delete", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar alerta quitar aviso borrar disparador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_events", QT_TRANSLATE_NOOP("mcp-etiquetas", "que paso historial de alertas disparos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"alert_wait", QT_TRANSLATE_NOOP("mcp-etiquetas", "esperar a que pase algo quedarse esperando evento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"receta_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que recetas hay automatizaciones guardadas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"receta_guardar", QT_TRANSLATE_NOOP("mcp-etiquetas", "guardar receta automatizar secuencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"receta_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar receta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"receta_correr", QT_TRANSLATE_NOOP("mcp-etiquetas", "correr receta ejecutar automatizacion")},

    // --- Asistente y agentes -------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"assistant_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado del asistente que modelo motor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documentos_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "documentos del evento pdf documentacion que documentos hay")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documento_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar pdf documento documentacion subir archivo caratula")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documento_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "describir documento relacionar con punto del orden del dia seccion publicar cambiar caratula miniatura pagina de tapa")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documento_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar sacar borrar documento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documento_texto", QT_TRANSLATE_NOOP("mcp-etiquetas", "leer texto del pdf resumir documento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pdf_convertir", QT_TRANSLATE_NOOP("mcp-etiquetas", "convertir pasar a pdf exportar word docx excel planilla powerpoint html markdown texto imagen imprimir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pdf_escena_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "pdf en la escena mostrar pdf en pantalla que pagina")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pdf_escena_poner", QT_TRANSLATE_NOOP("mcp-etiquetas", "pasar pagina pdf siguiente anterior elegir pdf mostrar documento en pantalla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pdf_escena_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "escena para pdf armar escena mostrar pdf")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_escenarios", QT_TRANSLATE_NOOP("mcp-etiquetas", "tipos de evento guias plantillas preparar un evento clase asamblea reunion streaming acto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_escenario", QT_TRANSLATE_NOOP("mcp-etiquetas", "guia para preparar un evento que preguntar checklist protocolo invitaciones verificar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plantillas_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "plantillas de evento cuales hay ordenar administrar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plantilla_ajustar", QT_TRANSLATE_NOOP("mcp-etiquetas", "renombrar ocultar ordenar plantilla guardar evento como plantilla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documento_abrir", QT_TRANSLATE_NOOP("mcp-etiquetas", "abrir mostrar ver documento pdf en pantalla visor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_nuevo", QT_TRANSLATE_NOOP("mcp-etiquetas", "arrancar de cero evento nuevo empezar algo nuevo desde cero copiar el actual plantilla "
                     "crear evento aparte sin tocar las escenas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_cambiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "abrir otro evento cambiar de evento pasar a otro evento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"eventos_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que eventos hay lista de eventos plantillas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_mapa", QT_TRANSLATE_NOOP("mcp-etiquetas", "latencias del pipeline cuanto tarda demora latencia retardo delay de cada entrada a cada salida tiempos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_medir", QT_TRANSLATE_NOOP("mcp-etiquetas", "medir los tiempos correr un analisis de demoras cuanto tarda hasta youtube muestrear por elemento sondas gpu memoria ram baja sube cruce plugin que agrega")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_eventos", QT_TRANSLATE_NOOP("mcp-etiquetas", "eventos de la linea de tiempo conexion desconexion cambio de escena que paso latencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_evento_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "marcar un momento poner una nota en la linea de tiempo evento manual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_evento_impacto", QT_TRANSLATE_NOOP("mcp-etiquetas", "que cambio cuando por que subio la latencia antes y despues de un evento analizar delay")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_linea", QT_TRANSLATE_NOOP("mcp-etiquetas", "mostrar en la linea de tiempo graficar esta tarjeta comparar latencias")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_nodo", QT_TRANSLATE_NOOP("mcp-etiquetas", "cuanto tarda esta tarjeta este modulo esta salida historia de la demora")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_pipeline", QT_TRANSLATE_NOOP("mcp-etiquetas", "por dentro de una entrada elemento por elemento cadena de video pipeline gpu ram baja sube cruce copia rompe la cadena que plugin tarda retiene cuadros declara latencia diagnostico delay demora")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"resolucion_plan", QT_TRANSLATE_NOOP("mcp-etiquetas", "resolucion tamaño cuanto se ve sobra falta superres al pedo zoom digital ptz detalle modo de la camara escala de mas colores alarma amarillo naranja rojo tarjetas mal armado orden")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"tiempos_plugin", QT_TRANSLATE_NOOP("mcp-etiquetas", "como trabaja un plugin modulo filtro tarjeta gpu o cpu memoria acepta pads templates alternativa mas rapida propiedades por que tarda")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar eliminar sacar un evento tirar a la papelera evento de prueba")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_recuperar", QT_TRANSLATE_NOOP("mcp-etiquetas", "recuperar devolver un evento borrado papelera deshacer borrado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_papelera_vaciar", QT_TRANSLATE_NOOP("mcp-etiquetas", "vaciar la papelera liberar espacio en disco borrar definitivamente eventos borrados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ia_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "que no anda de la inteligencia artificial que falta configurar por donde empiezo asistente "
                  "offline voz del operador dictado modelo local microfono auriculares puesta en marcha")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"assistant_config", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar asistente modelo motor clave")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"assistant_install", QT_TRANSLATE_NOOP("mcp-etiquetas", "instalar asistente descargar motor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"assistant_run", QT_TRANSLATE_NOOP("mcp-etiquetas", "correr asistente pedirle algo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"assistant_send", QT_TRANSLATE_NOOP("mcp-etiquetas", "mandarle texto al asistente escribirle")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_roles", QT_TRANSLATE_NOOP("mcp-etiquetas", "que roles hay agentes disponibles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_configurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar agente motor modelo prender apagar rol darle consola a un rol "
                          "permitirle disco e internet herramientas propias del agente")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_encargar", QT_TRANSLATE_NOOP("mcp-etiquetas", "encargarle algo a un agente delegar pedirle")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_corridas", QT_TRANSLATE_NOOP("mcp-etiquetas", "que hicieron los agentes historial trabajos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_corrida", QT_TRANSLATE_NOOP("mcp-etiquetas", "detalle de una corrida paso a paso que penso")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_detener", QT_TRANSLATE_NOOP("mcp-etiquetas", "parar agente cancelar trabajo")},

    // --- Entradas ------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_devices", QT_TRANSLATE_NOOP("mcp-etiquetas", "camaras webcams capturadoras dispositivos de video")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dispositivos_video", QT_TRANSLATE_NOOP("mcp-etiquetas", "gst device monitor caps formatos mjpeg h264 crudo resolucion modos de la camara por que no da 1080 30 api media foundation v4l2 avfoundation")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_input_add", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar camara webcam capturadora")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"network_input_add", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar camara ip rtsp srt stream entrante url")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ndi_sources", QT_TRANSLATE_NOOP("mcp-etiquetas", "que fuentes ndi hay en la red")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ndi_input_add", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar fuente ndi camara por red")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"input_status", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado de las entradas que esta llegando")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ext_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que extensiones hay plugins disponibles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ext_prender", QT_TRANSLATE_NOOP("mcp-etiquetas", "activar extension plugin prender")},

    // --- Cámaras PTZ ------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voces", QT_TRANSLATE_NOOP("mcp-etiquetas", "voces para hablar idiomas texto a voz que voces hay ingles portugues frances")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"texto_a_voz", QT_TRANSLATE_NOOP("mcp-etiquetas", "texto a voz tarjeta que hable decir en voz alta en otro idioma leer la transcripcion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"texto_flujos",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "salidas de texto transcripciones con nombre que se esta transcribiendo flujos de texto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_camaras", QT_TRANSLATE_NOOP("mcp-etiquetas", "camaras ptz que puede cada camara estado del ptz backend visca onvif")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_mover", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover camara paneo pan tilt zoom acercar alejar girar subir bajar la camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_parar", QT_TRANSLATE_NOOP("mcp-etiquetas", "parar la camara frenar el movimiento" " detener stop dejar quieta la camara frenar ptz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_preset", QT_TRANSLATE_NOOP("mcp-etiquetas", "preset posicion guardada ir a la posicion guardar posicion de camara plano")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_encuadre", QT_TRANSLATE_NOOP("mcp-etiquetas", "encuadre recorte zoom digital reencuadrar acercar el plano centrar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_apuntar", QT_TRANSLATE_NOOP("mcp-etiquetas", "apuntar la camara a ese punto centrar ahi encuadrar ese cuadro acercar a esa zona")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_configurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar ptz camara motorizada visca onvif panasonic ip de la camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_posiciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "posiciones guardadas con nombre de la camara lista de planos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_posicion_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "guardar posicion con nombre registrar plano de la camara agregar posicion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_posicion_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "renombrar posicion cambiar comentario regrabar plano")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_posicion_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar posicion eliminar plano guardado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_ir_posicion", QT_TRANSLATE_NOOP("mcp-etiquetas", "llevar la camara a la posicion ir al plano mover a la posicion guardada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"enfocar", QT_TRANSLATE_NOOP("mcp-etiquetas", "enfocar enfoca banca a la persona al consejero apuntar la camara a mostrar a fulano plano de la banca presidencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"banca_toma", QT_TRANSLATE_NOOP("mcp-etiquetas", "camara de la banca posicion de la banca toma asociar banca camara preset de la banca configurar bancas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_caras_ubicar", QT_TRANSLATE_NOOP("mcp-etiquetas", "caras que ve la camara quien esta en cuadro detectar rostros ptz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_seguir", QT_TRANSLATE_NOOP("mcp-etiquetas", "seguir a una persona tracking seguimiento ptz que la camara siga a alguien centrar cara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ptz_buscar", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar camaras onvif en la red descubrir camaras ip")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_grafo", QT_TRANSLATE_NOOP("mcp-etiquetas", "grafo de video tarjetas de video pipeline de la camara que filtros tiene")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_tarjeta_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar tarjeta de video croma chroma key fondo verde color brillo contraste "
                              "saturacion recorte espejo salida de video")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_filtros",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "filtros de video de obs lut cube correccion de color llave nitidez catalogo de filtros")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_modulos", QT_TRANSLATE_NOOP("mcp-etiquetas", "modulos gstreamer catalogo de filtros de video desentrelazar texto reloj efectos "
                      "opengl ayuda de filtros")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_entrada_virtual",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "entrada virtual senal de ajuste barras de color negro patron de prueba pipeline gstreamer rtsp srt")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_controlador", QT_TRANSLATE_NOOP("mcp-etiquetas", "misma correccion de color en todas las camaras controlador compartido igualar "
                          "camaras vincular tarjetas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_pipeline", QT_TRANSLATE_NOOP("mcp-etiquetas", "armar pipeline de video cadena de tarjetas correccion de color compartida entre "
                       "camaras igualar camaras")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_mosaico",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "mosaico de camaras imagen en imagen pip lado a lado multicamara juntar camaras en una escena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_panoramica",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "panoramica unir camaras stitching coser varias camaras lado a lado camara 360 dos lentes ojo de pez "
     "fisheye equirectangular dewarp calibrar lentes corregir deformacion distorsion encajar ptz virtual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado de la camara resolucion fps cuadros por segundo espacio de color colorimetria "
                     "entrelazado diagnostico de video")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_entrada_red", QT_TRANSLATE_NOOP("mcp-etiquetas", "stream de red srt rtmp rtsp udp recibir video por red entrada remota corresponsal "
                          "movil jitter buffer latencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_reloj", QT_TRANSLATE_NOOP("mcp-etiquetas", "reloj del estudio ntp ptp sincronizar relojes genlock reloj externo publicar reloj")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_sincronia",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "sincronizar streams desfase entre camaras medir retraso alinear video atrasado adelantado lip sync")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_desfase", QT_TRANSLATE_NOOP("mcp-etiquetas", "atrasar adelantar video retraso manual desfase de una entrada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"entrada_borrar",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar entrada eliminar camara sacar fuente de video de la app quitar entrada de red")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"equipos_escanear",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar equipos detectar dispositivos escanear red usb camaras microfonos hid serie")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"equipos_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que equipos hay dispositivos encontrados listar equipos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"equipo_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "detalle de un equipo datos acciones de un dispositivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"equipo_renombrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ponerle nombre a una camara microfono placa de sonido capturadora renombrar dispositivo "
                         "nombre del operador como se llama")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"equipo_configurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "configurar dispositivo nota formato preferido modo de la camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"equipo_olvidar", QT_TRANSLATE_NOOP("mcp-etiquetas", "olvidar dispositivo ausente desconectado sacar de la lista borrar equipo viejo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"drivers_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "drivers cuarentena driver colgado asio jack jackrouter asio4all sar placa de sonido no anda "
                       "proveedor de dispositivos wasapi media foundation decklink que driver falla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"driver_probar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar driver de nuevo sacar de cuarentena reintentar driver asio jack volver a cargar driver")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"drivers_revisar", QT_TRANSLATE_NOOP("mcp-etiquetas", "revisar todos los drivers volver a probar drivers chequear drivers instalados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"equipo_accion", QT_TRANSLATE_NOOP("mcp-etiquetas", "hacer algo con un equipo funciones onvif operacion onvif ficha agregar camara "
                      "explorar uvc pedir clave activar ptz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "plugins python extensiones programar equipo raro camara rara que plugins hay")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_escribir", QT_TRANSLATE_NOOP("mcp-etiquetas", "escribir plugin python programar soporte para equipo raro ptz usb hid logitech "
                        "rally kandao 360 ojo de pez camara que no anda")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_probar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar plugin sin tocar el aire test informe errores de python")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_activar", QT_TRANSLATE_NOOP("mcp-etiquetas", "prender apagar plugin activar desactivar arrancar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_control", QT_TRANSLATE_NOOP("mcp-etiquetas", "pedirle algo a un plugin control accion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_eventos", QT_TRANSLATE_NOOP("mcp-etiquetas", "eventos de plugins canales de datos quien habla direccion de voz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_credenciales",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "clave de un plugin usuario contraseña de un equipo token de la api de la marca formulario seguro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_exportar", QT_TRANSLATE_NOOP("mcp-etiquetas", "exportar plugin zip pasar a otro aparato copiar plugin respaldo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_importar", QT_TRANSLATE_NOOP("mcp-etiquetas", "importar plugin zip instalar plugin de otro aparato")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_tarjeta", QT_TRANSLATE_NOOP("mcp-etiquetas", "tarjeta de plugin filtro de video python filtro de audio python")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dispositivos_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que hay conectado usb hid serie camaras microfonos vid pid uvc dispositivos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"red_escanear", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar camaras en la red escanear red detectar camaras ip onvif rtsp hikvision dahua "
                     "axis ezviz dvr nvr")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"camara_credenciales",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "usuario y clave de la camara contrasena codigo de verificacion ezviz credenciales")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"camara_ficha",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "caracteristicas de la camara onvif perfiles resolucion ptz infrarrojo como conectar la camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"camara_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar camara ip conectar camara de red al estudio configurar ptz onvif")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"camara_urls", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar urls rtsp de la camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_tarjeta_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "quitar tarjeta de video sacar el croma sacar filtro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_tarjeta_ajustar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ajustar croma similitud suavidad brillo contraste recorte")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_cable", QT_TRANSLATE_NOOP("mcp-etiquetas", "conectar tarjetas de video cable rama del grafo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_salida_a_escena", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner la salida del grafo en la escena camara con croma en la escena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_grafo_limpiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "volver la camara a como vino sacar todas las tarjetas de video")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_mejora", QT_TRANSLATE_NOOP("mcp-etiquetas", "mejorar imagen ruido nitidez calidad de la camara color corrido led")},

    // --- Medios ---------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "que archivos hay musica videos biblioteca")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_find", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar archivo cancion video por nombre")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_view", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver miniatura previsualizar archivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"medio_poner",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "musica cancion tema pista de audio video clip poner musica poner musica de fondo musica de fondo poner "
     "una cancion un video poner musica reproducir cancion video al aire cortina")},

    // --- Operador ------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"operator_message", QT_TRANSLATE_NOOP("mcp-etiquetas", "avisarle al operador decirle mandarle un mensaje")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"operator_ask", QT_TRANSLATE_NOOP("mcp-etiquetas", "preguntale al de cabina consultale al operador pedile permiso al humano preguntarle al "
                     "operador consultar pedir permiso confirmar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"operator_messages", QT_TRANSLATE_NOOP("mcp-etiquetas", "que dijo el operador leer sus mensajes")},

    // --- Graficos y rotulos ---------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que graficos hay placas disponibles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_probar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar grafico previsualizar antes de sacarlo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_en_escena", QT_TRANSLATE_NOOP("mcp-etiquetas", "grafico dentro de una escena agregar que animados hay puestos en las escenas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_repertorio", QT_TRANSLATE_NOOP("mcp-etiquetas", "catalogo de graficos plantillas disponibles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_actualizar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar los numeros del grafico al aire actualizar la votacion en vivo torta barras "
                           "sin que vuelva a entrar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_medios", QT_TRANSLATE_NOOP("mcp-etiquetas", "que gif videos imagenes tiene el grafico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_medio_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner un gif animado en el grafico video con transparencia alfa webm prores "
                              "secuencia de png logo animado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_medio_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar un gif o video del grafico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rotulo_vinculos", QT_TRANSLATE_NOOP("mcp-etiquetas", "que rotulos estan vinculados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rotulo_vincular", QT_TRANSLATE_NOOP("mcp-etiquetas", "vincular rotulo a una persona nombre y cargo "
                        "poner el nombre en pantalla mostrar quien habla "
                        "identificar a quien esta al aire placa con el nombre")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rotulo_desvincular", QT_TRANSLATE_NOOP("mcp-etiquetas", "desvincular rotulo quitar")},

    // --- Registro --------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"log_leer", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver el registro que paso errores diagnosticar buscar en el log")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"log_nivel", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar nivel de registro detalle debug traza")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"log_marcar", QT_TRANSLATE_NOOP("mcp-etiquetas", "marcar en el registro poner una marca hito")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"logs_tail", QT_TRANSLATE_NOOP("mcp-etiquetas", "ultimas lineas del log cola seguir el registro")},

    // --- Teleprompter -----------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "estado del teleprompter quien esta conectado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_documentos", QT_TRANSLATE_NOOP("mcp-etiquetas", "que discursos hay textos guardados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_leer", QT_TRANSLATE_NOOP("mcp-etiquetas", "leer un discurso ver el texto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_escribir", QT_TRANSLATE_NOOP("mcp-etiquetas", "escribir discurso guardar texto del orador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_cargar", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner un texto en el teleprompter cargar discurso")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"prompter_control", QT_TRANSLATE_NOOP("mcp-etiquetas", "arrancar parar velocidad del teleprompter avanzar")},

    // --- Pantallas ---------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que televisores hay pantallas conocidas estado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_buscar", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar televisores en la red descubrir chromecast")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_mostrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "mandar a la tv proyectar castear mostrar en pantalla qr mensaje reloj "
                         "tally luz roja camara al aire escena en la tele una escena en el televisor del hall")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"catalogo_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que se puede mostrar que puedo poner en un monitor o en la tele lista de cosas "
                        "paneles escenas multiview paginas tarjetas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitor_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que muestra cada monitor monitores de la maquina orden de los monitores cual es "
                       "el de la izquierda pantallas conectadas que hay en el segundo monitor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitor_asignar", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner en el monitor dejar fijo en el segundo monitor el programa en el monitor del "
                        "escenario la multiview en el monitor de la izquierda llenar los monitores grilla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitor_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar del monitor dejar el monitor vacio que no muestre nada dejar de esperar un monitor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_carpetas", QT_TRANSLATE_NOOP("mcp-etiquetas", "como estan ordenados los medios que carpetas hay arbol")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"media_ordenar", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover archivos a otra carpeta ordenar renombrar crear carpeta borrar "
                      "limpiar acomodar los medios papelera")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ventana_captura", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver la interfaz mirar la pantalla del aparato como se ve la pagina "
                        "captura de la ventana revisar el gui algo se ve mal encimado cortado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_fijar", QT_TRANSLATE_NOOP("mcp-etiquetas", "dejar fijo que siempre muestre lo mismo permanente")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_renombrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ponerle nombre a un televisor renombrar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar televisor a mano por ip")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pantalla_parar", QT_TRANSLATE_NOOP("mcp-etiquetas", "dejar de mostrar apagar la proyeccion cortar el casteo" " sacar de la pantalla apagar la tele dejar de proyectar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"carrusel_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que carruseles hay rotaciones")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"carrusel_armar", QT_TRANSLATE_NOOP("mcp-etiquetas", "armar carrusel rotacion de pantallas pasos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"carruseles_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que carruseles de laminas hay presentaciones cargadas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"carrusel_laminas_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "que laminas tiene cuantas diapositivas de que trata cada una")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_mostrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner grafico al aire zocalo placa GC generador de caracteres rotulo "
                        "pie de pagina faldon "
                        "el apellido esta mal corregir el nombre cambiar el cargo poner el nombre "
                        "del que habla levantar el zocalo tirar la placa "
                        "camara en el zocalo video en vivo dentro del grafico recuadro con la camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grafico_ocultar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar grafico esconder bajar el zocalo sacar la placa quitar el GC "
                        "limpiar la pantalla fuera el cartel")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_buscar", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscame la imagen de los perros cual era la del grafico donde esta la "
                       "lamina que dice buscar adentro de la presentacion por lo que se ve")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_ir", QT_TRANSLATE_NOOP("mcp-etiquetas", "volve a la lamina del grafico saltar a la diapositiva numero poner esa placa "
                   "pasa a la siguiente la que sigue la anterior avanza una atras una "
                   "pone la diapositiva mostrar esa lamina pasar a la")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_desarmar", QT_TRANSLATE_NOOP("mcp-etiquetas", "convertime este powerpoint en imagenes convertir un pdf desarmar "
                         "presentacion importar armar carrusel desde un archivo "
                         "el orador trajo un pdf armalo para proyectar cargar la charla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_armar", QT_TRANSLATE_NOOP("mcp-etiquetas", "armar un carrusel con estas imagenes juntar placas sueltas crear carrusel "
                      "nuevo con los logos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar una lamina cambiarla de lugar reordenar renombrar el carrusel borrarlo "
                       "ponela al final al principio cambiar el orden de las laminas adelantar atrasar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_aplicar", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner el carrusel en una escena agregar la presentacion a la escena "
                        "mostrar las laminas al aire vincular")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_sin_analizar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cual falta mirar etiquetar proxima sin analizar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"laminas_etiquetar", QT_TRANSLATE_NOOP("mcp-etiquetas", "guardar lo que se ve en la lamina describir para poder buscarla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superficie_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que paginas estan vivas realmente mostrando telemetria retardo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superficie_mostrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar lo que muestra sin recastear empujar url recargar")},

    // --- QR -----------------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"qr_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que codigos qr hay guardados tipos iconos qr del publico qr de la sala qr de la conferencia para elegir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grilla_personas_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "quienes se ven en la grilla cuantos quedaron afuera mas n quien habla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grilla_personas_ajustar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar tope orden prioridad grupo repartir entre escenas parejo llenar en orden solo con camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grilla_personas_fijar", QT_TRANSLATE_NOOP("mcp-etiquetas", "fijar dejar fijo siempre en la grilla que no salga poner a alguien en el mosaico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grilla_personas_sacar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar quitar a alguien de la grilla que no aparezca en el mosaico esconder")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grilla_personas_mover", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover pasar a alguien de una grilla a otra cambiar de mosaico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"grilla_personas_devolver", QT_TRANSLATE_NOOP("mcp-etiquetas", "devolver volver a automatico soltar desfijar que vuelva a la grilla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ui_abrir_panel", QT_TRANSLATE_NOOP("mcp-etiquetas", "abrir panel dialogo ventana superresolucion entrada virtual catalogo modulos mostrar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_laboratorio", QT_TRANSLATE_NOOP("mcp-etiquetas", "superresolucion laboratorio bibliotecas python pytorch paquetes bajar instalar borrar espacio directml")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_modelo_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar modelo superresolucion red onnx espacio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_trabajo", QT_TRANSLATE_NOOP("mcp-etiquetas", "superresolucion trabajo avance progreso cancelar esperar descarga entrenamiento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "puede el asistente manejar la pc mouse captura permisos del control de windows")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"control_configurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "habilitar prender apagar control de windows permitir administrador mouse teclado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"qr_generar", QT_TRANSLATE_NOOP("mcp-etiquetas", "generar codigo qr crear sin guardar probar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"qr_guardar", QT_TRANSLATE_NOOP("mcp-etiquetas", "guardar codigo qr en la biblioteca")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"qr_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar codigo qr")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"qr_exportar", QT_TRANSLATE_NOOP("mcp-etiquetas", "exportar qr a archivo png svg imprimir")},

    // --- Evento ---------------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guion_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que eventos hay guiones guardados")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guion_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "que sigue en el guion que viene ahora proximo bloque proximo cue ver el guion del "
                   "evento leer rundown cuando es el evento que dia a que hora arranca")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guion_abrir", QT_TRANSLATE_NOOP("mcp-etiquetas", "abrir evento cargar guion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"modelo_local_disco", QT_TRANSLATE_NOOP("mcp-etiquetas", "modelos instalados disco espacio limpiar borrar viejos versiones gigas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"agente_limpieza", QT_TRANSLATE_NOOP("mcp-etiquetas", "limpiar chats asistente borrar conversaciones reinstalar agente versiones viejas espacio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_tarjeta_mover", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover tarjeta orden cadena antes despues ptz rostros reordenar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_en_curso", QT_TRANSLATE_NOOP("mcp-etiquetas", "que evento esta en curso historico jornada del dia registro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_iniciar", QT_TRANSLATE_NOOP("mcp-etiquetas", "empezar evento poner en curso registrar historico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_terminar", QT_TRANSLATE_NOOP("mcp-etiquetas", "terminar cerrar evento en curso historico automatico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guion_carpeta", QT_TRANSLATE_NOOP("mcp-etiquetas", "carpeta mover ordenar guiones agrupar archivo historico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_ficha", QT_TRANSLATE_NOOP("mcp-etiquetas", "ficha evento lugar etiquetas notas estado historico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guion_bloque", QT_TRANSLATE_NOOP("mcp-etiquetas", "bloque del guion seccion agregar mover")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"configuraciones_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que configuraciones hay perfiles demo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"configuracion_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "nueva configuracion crear perfil copiar la configuracion demo de prueba")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"configuracion_cambiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar de configuracion pasar a otra configuracion abrir la demo usar otro perfil")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"configuracion_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "renombrar borrar describir configuracion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"asamblea_revisar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que falta para sesionar asamblea concejo consejo preparar sesion escenas camaras bancas revisar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"asamblea_escenas_base", QT_TRANSLATE_NOOP("mcp-etiquetas", "armar escenas de la asamblea crear escenas para las bancas plano general debate remoto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"debate_iniciar", QT_TRANSLATE_NOOP("mcp-etiquetas", "debate entre dos cara a cara empezar debate dos remotos sala contra remoto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"debate_turno", QT_TRANSLATE_NOOP("mcp-etiquetas", "turno del debate darle la palabra a uno callar al otro ambos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"debate_terminar", QT_TRANSLATE_NOOP("mcp-etiquetas", "terminar el debate cortar debate")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"debate_config", QT_TRANSLATE_NOOP("mcp-etiquetas", "escena del debate tiempo de cada turno configurar debate")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"debate_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "como va el debate quien habla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"presidencia_callar_todos", QT_TRANSLATE_NOOP("mcp-etiquetas", "silenciar a todos callar a todos mute general cerrar todos los microfonos remotos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"presidencia_hablar", QT_TRANSLATE_NOOP("mcp-etiquetas", "hablo yo habla el presidente la presidencia toma la palabra prioridad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_vistas", QT_TRANSLATE_NOOP("mcp-etiquetas", "que ven los remotos en el celular canales escena salida principal")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sesion_hablar", QT_TRANSLATE_NOOP("mcp-etiquetas", "hablarle a un remoto apretar para hablar talkback intercom")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "ajustes de la central prioridad cancela pedidos tono ambiente modo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guion_cue", QT_TRANSLATE_NOOP("mcp-etiquetas", "cue paso accion del guion agregar editar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"guion_guardar", QT_TRANSLATE_NOOP("mcp-etiquetas", "guardar el guion del evento ponerle fecha hora y huso horario cambiarle el titulo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_revisar", QT_TRANSLATE_NOOP("mcp-etiquetas", "revisar guion antes buscar problemas validar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_ensayo_rapido", QT_TRANSLATE_NOOP("mcp-etiquetas", "ensayo acelerado probar el guion sin tocar el aire")},
    // «Informe» y «acta» son dos cosas y el que pide una no quiere la otra: el
    // informe es de tiempos (para el operador) y el acta es de gente y
    // decisiones (para el que organizo).
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_informe", QT_TRANSLATE_NOOP("mcp-etiquetas", "informe del evento tiempos reales cuanto duro desvios de cada bloque")},
    // Sin «quorum» a secas: preguntar si HAY quorum es algo que se pregunta
    // ANTES de votar y lo contesta el padron. El acta lo lleva adentro, pero
    // nadie pide un acta para saber si puede empezar.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_acta", QT_TRANSLATE_NOOP("mcp-etiquetas", "acta de la reunion quienes asistieron que se decidio que se voto "
                    "minuta del evento documento de la asamblea")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_armar", QT_TRANSLATE_NOOP("mcp-etiquetas", "armar un evento entero crear el evento con sus bloques y su gente "
                     "organizar una asamblea preparar una reunion de cero")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_capitulos", QT_TRANSLATE_NOOP("mcp-etiquetas", "capitulos de la grabacion marcas de tiempo para la descripcion del video "
                         "cortar el video por bloque timestamps de youtube")},
    // --- El padron: la gente del evento -----------------------------------
    // «Quienes estan» es LA pregunta de un evento hibrido, y las palabras con
    // que se la hace no se parecen al nombre de la herramienta: se pregunta por
    // los que llegaron, los que faltan y el quorum, nunca por «el padron».
    //
    // Ojo con «invitado»: en la sala un invitado es el que entra por webrtc
    // (participant_*), y en un evento es el de la lista. Va en las dos, y lo
    // que desempata son las palabras de al lado —«lista», «evento», «anotar»—.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"padron_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "quienes estan cuantos llegaron cuantos faltan quorum lista de asistentes "
                   "padron del evento presentes ausentes quien vino quien falta invitados del evento "
                   "a quien dejar pasar solo sin aprobar quienes entran solos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"padron_anotar", QT_TRANSLATE_NOOP("mcp-etiquetas", "anotar a alguien en la lista del evento sumar invitado agregar persona al padron "
                      "dar de alta un asistente panelista orador prensa que entre solo sin aprobar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"padron_cambiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiarle los datos a alguien del padron editar asistente papel canal estado "
                       "engancharle la ficha de rostro que entre sola sin que yo apruebe darle "
                       "permiso para entrar solo dejarla pasar sin aprobacion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"padron_acreditar", QT_TRANSLATE_NOOP("mcp-etiquetas", "acreditar marcar que llego registrar la entrada de alguien check in "
                         "llego se fue ya esta aca entro por la puerta salida")},
    // --- Las preguntas del publico ----------------------------------------
    // Ojo con «pregunta»: el asistente pregunta, el operador pregunta y el
    // publico pregunta. Lo que desempata son las palabras de al lado —publico,
    // cola, moderar, al aire— y por eso las etiquetas las llevan siempre.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"preguntas_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "preguntas del publico que pregunto la gente cola de preguntas ver las "
                      "preguntas que mandaron moderar cual sigue cual es la proxima pregunta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pregunta_moderar", QT_TRANSLATE_NOOP("mcp-etiquetas", "elegir una pregunta sacarla al aire darla por respondida descartar "
                         "pregunta poner al aire la pregunta siguiente pregunta limpiar la cola")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"pregunta_sumar", QT_TRANSLATE_NOOP("mcp-etiquetas", "meter una pregunta en la cola sumar lo que preguntaron por el chat "
                       "agregar pregunta de youtube del salon a viva voz")},
    // Votar: «votacion» es formal y «encuesta» es enganche. Las dos palabras
    // van en las dos herramientas porque quien pide no siempre sabe cual
    // quiere, y la ayuda de la herramienta se encarga de explicarle la
    // diferencia.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"votacion_abrir", QT_TRANSLATE_NOOP("mcp-etiquetas", "abrir una votacion poner algo a votar hacer una encuesta al publico "
                       "votemos si aprobamos preguntar y que voten levantar la mano")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"votacion_recuento", QT_TRANSLATE_NOOP("mcp-etiquetas", "como va la votacion resultado de la encuesta cuantos votaron quienes "
                     "faltan votar cerrar la votacion quorum del voto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"publico_pagina", QT_TRANSLATE_NOOP("mcp-etiquetas", "enlace y qr para que el publico pregunte pagina del publico codigo qr para "
                       "proyectar titulo y fondo de la pagina del publico")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"invitacion", QT_TRANSLATE_NOOP("mcp-etiquetas", "la invitacion de alguien enlace personal para invitar mandarle el qr a una "
                   "persona convocar a un invitado su codigo para acreditarse")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"invitado_chequeo", QT_TRANSLATE_NOOP("mcp-etiquetas", "estan listos los que esperan probar camara y microfono antes prueba "
                         "tecnica previa se escucha bien el invitado esta listo para salir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"padron_puerta", QT_TRANSLATE_NOOP("mcp-etiquetas", "camara de la entrada acreditar solo a quien llega al lugar reconocer a los que "
                      "llegan al salon puerta acreditacion presencial mostrador de entrada")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"padron_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar a alguien de la lista del evento borrar asistente del padron")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"evento_correr", QT_TRANSLATE_NOOP("mcp-etiquetas", "correr el guion arrancar avanzar pausar el evento" " ejecutar siguiente cue"
                      " hold frenar retener que no pase solo automatico")},

    // --- Ventanas -------------------------------------------------------------------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ventana_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que ventanas hay abiertas donde esta cada una en que monitor geometria")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ventana_mover", QT_TRANSLATE_NOOP("mcp-etiquetas", "mover ventana a otro monitor acomodar pantalla completa maximizar "
                      "mitad izquierda derecha organizar el escritorio armar el puesto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ventana_cerrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cerrar ventana quitar sacar de pantalla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_admit", QT_TRANSLATE_NOOP("mcp-etiquetas", "admitir hacer pasar dejar entrar aceptar al invitado que espera "
                          "sacarlo del lobby meterlo a la sala meterlo a la conferencia")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_reject", QT_TRANSLATE_NOOP("mcp-etiquetas", "rechazar no dejar entrar negar la entrada echar del lobby")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"participant_kick", QT_TRANSLATE_NOOP("mcp-etiquetas", "echar sacar de la sala sacar de la conferencia expulsar desconectar a alguien que ya entro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"imagen_preguntar", QT_TRANSLATE_NOOP("mcp-etiquetas", "mirar la imagen que se ve ahi que hay en camara revisar visualmente "
                        "analizar captura vision hay alguien cuadro negro se lee el rotulo" " preguntarle a la imagen contar personas leer el texto de la imagen")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_plugin_buscar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que filtros hay que plugins tengo hay algun filtro para buscar "
                            "un efecto autotune reverb eco delay coro que la musica baje cuando "
                            "hablan ducking siseo sincronizar audio con imagen mas de doscientos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"persona_identificar", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien esta en camara quien aparece reconocer a alguien identificar cara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_guardar", QT_TRANSLATE_NOOP("mcp-etiquetas", "dar de alta una persona ensenarle una cara agregar al sistema corregir ficha")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que personas hay guardadas libreta conocidas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar una persona olvidar una cara sacar de la libreta")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_calidad", QT_TRANSLATE_NOOP("mcp-etiquetas", "sirve esta cara que le pido acercarse mirar de frente luz borrosa "
                       "enfoque alta guiada invitado webrtc no me reconoce")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_sumar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sumarle otra cara pose angulo mejorar el reconocimiento ensenarle mas "
                     "girar la cabeza que se mueva")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_pendientes", QT_TRANSLATE_NOOP("mcp-etiquetas", "sin nombre desconocidos anotados solos completar fichas quien es este")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_fusionar", QT_TRANSLATE_NOOP("mcp-etiquetas", "juntar dos fichas duplicada misma persona dos veces unir")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"atencion_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien atiende a los que llegan recepcion lobby atencion hablando voz asistente atiende "
                        "a quien esta atendiendo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"atencion_configurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "los atiende el asistente atender hablando con voz prender atencion lobby asamblea "
                            "sesion remota microfono del que espera alta automatica anotar en personas voz del "
                            "atendedor")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"atencion_voz_probar", QT_TRANSLATE_NOOP("mcp-etiquetas", "probar la voz del atendedor como suena generar audio sin reproducir wav")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_listar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que totems hay paginas de registro configuradas que pregunta cada uno")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear un totem armar un registro nuevo pedir nombre correo telefono a los que llegan "
                    "registrar gente hablando")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_cambiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar las preguntas del registro agregar un campo saludo despedida que pida el telefono "
                      "confirmar los datos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar un totem sacar el registro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_enlace", QT_TRANSLATE_NOOP("mcp-etiquetas", "link del totem url del registro qr para registrarse copiar el enlace renovar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_proyectar", QT_TRANSLATE_NOOP("mcp-etiquetas", "mandar el registro a la tele proyectar el qr del registro en un televisor chromecast")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien se esta registrando ahora conexiones del totem en que paso va que le entendio")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_cortar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cortar la conexion de un totem colgado trabado echar desconectar celular tablet del registro")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_registrados", QT_TRANSLATE_NOOP("mcp-etiquetas", "quienes se registraron lo que contestaron datos del registro correos telefonos "
                          "lista de inscriptos")},
    // OJO: documento_leer/documento_tipos son documentos de IDENTIDAD (el DNI por su código
    // de barras); documento_agregar/texto/abrir son los PDF del evento. Las palabras no se cruzan.
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documento_leer", QT_TRANSLATE_NOOP("mcp-etiquetas", "leer el dni escanear el documento de identidad codigo de barras pdf417 cedula licencia "
                       "de conducir pasaporte credencial mostrar el dni a la camara probar el lector del totem "
                       "identificar a la persona por su documento dorso parte de atras renglones mrz zona de lectura mecanica ocr")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"documento_tipos", QT_TRANSLATE_NOOP("mcp-etiquetas", "que documentos de identidad entiende tipos de dni licencias pasaportes que acepta el totem "
                        "documentos de otros paises plugin de documentos se puede leer el dorso cedula mrz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_atencion_enlace", QT_TRANSLATE_NOOP("mcp-etiquetas", "link de atencion remota del registro qr para el equipo atender los totems desde "
                              "el celular mirar y escuchar las conversaciones ayudar a la gente renovar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_atender", QT_TRANSLATE_NOOP("mcp-etiquetas", "atender una conversacion del totem tomar el registro pausar al asistente callar al asistente "
                      "devolver al asistente guardar el registro ya cancelar intervenir ayudar a alguien que no puede")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"totem_dato", QT_TRANSLATE_NOOP("mcp-etiquetas", "completar un dato del registro corregir el correo poner el telefono cambiar el nombre "
                   "borrar un dato que quedo mal escribirle los datos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_autoalta", QT_TRANSLATE_NOOP("mcp-etiquetas", "anotar solo automatico registrar a todos los que pasen interruptor "
                        "switch dar de alta sola lobby los que llegan")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"rostro_lobby", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien espera quien llego quien esta en el lobby reconocer a los que entran "
                     "sala de espera identificar antes de que entre "
                     // Al reescribir su descripcion para que no compitiera con
                     // admitir, perdio las palabras que la encontraban. Las
                     // etiquetas no dependen de como quedo redactada.
                     "no me acuerdo como se llama quien es el que espera como se llama el que llego")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"traducir", QT_TRANSLATE_NOOP("mcp-etiquetas", "traducir traduccion idioma ingles portugues frances aleman ruso chino como se dice probar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"traduccion_",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "traduccion en vivo subtitulos en otro idioma traducir al orador doblaje pista en ingles bajar idioma")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"traducciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "traducciones activas en vivo idiomas que se estan traduciendo demora")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_plugins", QT_TRANSLATE_NOOP("mcp-etiquetas", "plugins superresolucion lista modelos realesrgan esrgan fsrcnnx anime4k instalados disponibles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_instalar", QT_TRANSLATE_NOOP("mcp-etiquetas", "instalar bajar plugin superresolucion modelo coreml onnx tensorrt")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_plugin_elemento", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear plugin propio elemento gstreamer superresolucion tensorrt cuda memoria gpu contrato")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_plugin_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar desinstalar plugin superresolucion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_variantes", QT_TRANSLATE_NOOP("mcp-etiquetas", "variantes superresolucion resolucion tamano fijo compilada coreml onnx camara red")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_variante_crear", QT_TRANSLATE_NOOP("mcp-etiquetas", "crear compilar variante resolucion tamano entrada fijo coreml neural engine tensorrt onnx camara")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_variante_editar", QT_TRANSLATE_NOOP("mcp-etiquetas", "editar variante para que descripcion superresolucion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_variante_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar variante superresolucion tamano")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_destilar", QT_TRANSLATE_NOOP("mcp-etiquetas", "destilar destilacion entrenar red propia maestro esrgan imitar camara superresolucion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"entrada_control", QT_TRANSLATE_NOOP("mcp-etiquetas", "control evento entrada gstreamer cadena propagar efecto senal activar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_programador", QT_TRANSLATE_NOOP("mcp-etiquetas", "programador superresolucion preview programa activar reposo camaras gpu capacidad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"estudio_latencia", QT_TRANSLATE_NOOP("mcp-etiquetas", "latencia del estudio demora atraso tope llega tarde camara lenta compensacion sincronia sincronizar camaras quien marca la latencia bajar latencia colchon alinear entradas en vivo salto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"sincronia_clase", QT_TRANSLATE_NOOP("mcp-etiquetas", "sincronizar entrada con la sala propia demora invitado webrtc remoto no arrastrar latencia clase de sincronia camaras alineadas desfase entre camaras")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_estudio", QT_TRANSLATE_NOOP("mcp-etiquetas", "vista general todo el audio cadenas pipelines fuentes rack mezcla de un vistazo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"automixer", QT_TRANSLATE_NOOP("mcp-etiquetas", "automixer mesa redonda dugan bancas microfonos ganancia el que habla")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"entrada_filtros_audio", QT_TRANSLATE_NOOP("mcp-etiquetas", "audio filtros entrada plugin python compresor ecualizador eco gstreamer cadena")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_banco", QT_TRANSLATE_NOOP("mcp-etiquetas", "banco pruebas benchmark medir maquina capacidad superresolucion acelerador en vivo rapido cuanto tarda")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_runtime_gpu", QT_TRANSLATE_NOOP("mcp-etiquetas", "gpu placa nvidia amd intel cuda tensorrt rocm migraphx openvino directml runtime onnx")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_runtime_gpu_instalar", QT_TRANSLATE_NOOP("mcp-etiquetas", "bajar instalar runtime gpu nvidia cuda tensorrt amd rocm intel openvino")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"traduccion_agregar",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "traducir en vivo tarjeta traducir rack de audio subtitulos en ingles otro idioma salida de texto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"traduccion_ajustar", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar pausar quitar tarjeta traducir idioma espera rotulo archivo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"glosario_", QT_TRANSLATE_NOOP("mcp-etiquetas", "glosario terminos vocabulario asamblea como se traduce palabra nombre propio "
                  "corregir traduccion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"voz_cortes", QT_TRANSLATE_NOOP("mcp-etiquetas", "no corta nunca tarda en traducir pausa silencio cortar frase largo maximo umbral ruido "
                   "sala detector de voz")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"texto_publicar",
     QT_TRANSLATE_NOOP("mcp-etiquetas", "escribir subtitulo a mano mandar texto a una salida de texto probar subtitulos sin hablar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"subtitulos_limpiar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar subtitulos sacar subtitulo de pantalla limpiar cuadro de subtitulos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_", QT_TRANSLATE_NOOP("mcp-etiquetas", "superresolucion mejorar resolucion camara baja definicion vieja pal agrandar nitidez "
                  "upscale escalar entrenar modelo fsr span esrgan")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"audio_ordenar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ordenar acomodar tarjetas del rack de audio grilla prolijo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"video_ordenar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ordenar acomodar tarjetas del grafo de video franjas prolijo")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_panel", QT_TRANSLATE_NOOP("mcp-etiquetas", "panel del plugin controles deslizador boton mover control del plugin indicador")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"ventanas_ordenar", QT_TRANSLATE_NOOP("mcp-etiquetas", "ordenar acomodar las ventanas repartir entre monitores que no se "
                         "amontonen ordena las ventanas armar el puesto")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitor_identificar", QT_TRANSLATE_NOOP("mcp-etiquetas", "que pantalla es cual cual es cada monitor identificar las pantallas identificar "
                            "pantallas cual es cada monitor numero nombre "
                            "cual es el de la izquierda mostrar el numero")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"monitors", QT_TRANSLATE_NOOP("mcp-etiquetas", "monitores conectados resolucion cuantas pantallas hay segundo monitor")},

    // --- Las que no tenían ninguna (revisado contra tools/list el 2026-10-08) ---------
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"botonera_cara", QT_TRANSLATE_NOOP("mcp-etiquetas", "cara del boton foto del boton video en un boton imagen del boton icono tecla "
                      "stream deck capturar de una camara foto de la camara gif animado")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"historico_buscar", QT_TRANSLATE_NOOP("mcp-etiquetas", "buscar en todos los eventos cuando se dijo quien hablo de que tema buscar en las "
                         "transcripciones buscar una votacion vieja")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"historico_restaurar", QT_TRANSLATE_NOOP("mcp-etiquetas", "volver a la configuracion de un evento restaurar como estaba deshacer cambios "
                            "configuracion vieja dejarlo como en la sesion anterior")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"historico_traer_del_dia", QT_TRANSLATE_NOOP("mcp-etiquetas", "asignar grabaciones viejas a un evento juntar las grabaciones del dia "
                                "grabaciones sueltas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"historico_retencion", QT_TRANSLATE_NOOP("mcp-etiquetas", "espacio en disco borrar grabaciones viejas cuanto disco queda limpieza "
                            "liberar espacio retencion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"historico_exportar", QT_TRANSLATE_NOOP("mcp-etiquetas", "exportar un evento copiar un evento a un disco llevarse la sesion entregar "
                           "las grabaciones pendrive")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"historico_ajustes", QT_TRANSLATE_NOOP("mcp-etiquetas", "que se registra que se guarda en la bitacora jornada automatica privacidad")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"traduccion_bajar", QT_TRANSLATE_NOOP("mcp-etiquetas", "bajar idioma descargar paquetes de traduccion agregar un idioma ingles portugues "
                         "frances aleman ruso chino sin internet")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"glosario_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver el glosario que terminos respeta la traduccion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"glosario_agregar", QT_TRANSLATE_NOOP("mcp-etiquetas", "agregar un termino corregir una traduccion se traduce mal nombre propio sigla "
                         "como se dice en ingles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"glosario_quitar", QT_TRANSLATE_NOOP("mcp-etiquetas", "sacar un termino apagar un grupo de terminos")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"motor_mapa", QT_TRANSLATE_NOOP("mcp-etiquetas", "gpu cpu donde pasa el video copias memoria rendimiento lento placa de video "
                   "pipelines del motor diagnostico de video")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_estado", QT_TRANSLATE_NOOP("mcp-etiquetas", "que motores de superresolucion hay modelos instalados progreso del entrenamiento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_tarjeta", QT_TRANSLATE_NOOP("mcp-etiquetas", "poner superresolucion en una camara agrandar una entrada mejorar una camara "
                         "de baja resolucion tarjeta de superresolucion")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_aplicar", QT_TRANSLATE_NOOP("mcp-etiquetas", "superresolucion en un stream de red agrandar una entrada de red")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_modelo", QT_TRANSLATE_NOOP("mcp-etiquetas", "bajar un modelo de superresolucion span esrgan real esrgan convertir a onnx")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"superres_entrenar", QT_TRANSLATE_NOOP("mcp-etiquetas", "entrenar una red para esta camara aprender de la camara mejorar a medida "
                          "entrenamiento")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_ver", QT_TRANSLATE_NOOP("mcp-etiquetas", "ver el codigo de un plugin leer un ejemplo manifiesto archivos del plugin")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_desde_ejemplo", QT_TRANSLATE_NOOP("mcp-etiquetas", "copiar un ejemplo empezar un plugin desde un ejemplo plantilla de plugin")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_reiniciar", QT_TRANSLATE_NOOP("mcp-etiquetas", "reiniciar un plugin recargar despues de cambiar el codigo no responde")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_registro", QT_TRANSLATE_NOOP("mcp-etiquetas", "errores de un plugin que imprimio log del plugin por que falla traceback")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"plugin_borrar", QT_TRANSLATE_NOOP("mcp-etiquetas", "borrar un plugin sacar un plugin desinstalar")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_simular_prioridad", QT_TRANSLATE_NOOP("mcp-etiquetas", "simular el boton de prioridad del presidente prueba sin central")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_funciones", QT_TRANSLATE_NOOP("mcp-etiquetas", "funciones sin documentar de la central open interface lo que no esta en el manual")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_funcion", QT_TRANSLATE_NOOP("mcp-etiquetas", "llamar una funcion sin documentar de la central comando especial")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_proximo", QT_TRANSLATE_NOOP("mcp-etiquetas", "quien sigue el proximo en hablar a quien le toca votar siguiente orador de la central")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_cerrar_tras_siguiente", QT_TRANSLATE_NOOP("mcp-etiquetas", "cerrar los microfonos al pasar al siguiente apagar el que hablaba")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_llamado_nominal", QT_TRANSLATE_NOOP("mcp-etiquetas", "orden del llamado nominal votacion nominal en que orden votan las bancas")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"dcn_simular_aviso", QT_TRANSLATE_NOOP("mcp-etiquetas", "simular un aviso de la central probar votacion pedida desde la consola")},

    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"perfil", QT_TRANSLATE_NOOP("mcp-etiquetas", "rendimiento lento cpu gpu consumo que consume uso del procesador placa de video hilos "
               "perfilar diagnostico de rendimiento por que va lento se traba")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"settings_list", QT_TRANSLATE_NOOP("mcp-etiquetas", "que se puede configurar opciones disponibles")},
    //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
    {"settings_set", QT_TRANSLATE_NOOP("mcp-etiquetas", "cambiar una configuracion ajustar opcion")},
};

/// Las palabras de una fila, ya en el idioma del operador.
void partir(const char* terminos, std::vector<std::string>& out) {
    const std::string s = ba::i18n::traducir("mcp-etiquetas", terminos);
    std::string cur;
    for (const char c : s) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
}

/// Las filas propias de cada herramienta, sin traducir (se traducen al leerlas).
const std::map<std::string, std::vector<const char*>>& tabla() {
    static const std::map<std::string, std::vector<const char*>> t = [] {
        std::map<std::string, std::vector<const char*>> m;
        // SE SUMAN, no se pisan. Estaba con `=`, asi que dos entradas para la
        // misma herramienta hacian que la segunda BORRARA a la primera, en
        // silencio: quien agregaba etiquetas creyendo que sumaba, restaba.
        // (Habia cuatro asi: monitors, participant_admit/reject/kick.)
        for (const Fila& f : kPropias) m[f.clave].push_back(f.terminos);
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
        partir(f.terminos, out);
    }
    for (const std::string& n : nombres)
        if (const auto it = tabla().find(n); it != tabla().end())
            for (const char* terminos : it->second) partir(terminos, out);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace ba::app::mcp
