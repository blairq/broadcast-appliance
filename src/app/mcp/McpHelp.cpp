// ============================================================================
// src/app/mcp/McpHelp.cpp — Sistema de ayuda del servidor MCP
//
// Instrucciones del initialize, temas (tambien como recursos ba://ayuda/...),
// guias paso a paso (prompts MCP) y recursos con el estado vivo.
// ============================================================================

#include <QJsonDocument>

#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {

// Lo largo (las instrucciones del initialize, el cuerpo de cada tema y el prompt del
// asistente) son archivos del idioma: assets/idiomas/<código>/ayuda/<id>.md y
// prompts/asistente.md, en el idioma del operador (ba::i18n::leer). Los ids de los temas y
// los recursos ba://ayuda/<id> no cambian con el idioma; el título y el resumen se traducen
// al registrarlos (QT_TRANSLATE_NOOP + traducir "mcp").
std::string instrucciones() { return ba::i18n::leer("ayuda/instrucciones.md"); }

struct TopicDef {
    const char* id;
    const char* title;   ///< QT_TRANSLATE_NOOP("mcp", …)
    const char* summary; ///< QT_TRANSLATE_NOOP("mcp", …)
};

const TopicDef kTopics[] = {
    {"primeros_pasos", QT_TRANSLATE_NOOP("mcp", "Primeros pasos"),
     QT_TRANSLATE_NOOP("mcp", "Como esta armado el appliance y por donde empezar.")},
    {"conectar", QT_TRANSLATE_NOOP("mcp", "Conectar un cliente MCP"),
     QT_TRANSLATE_NOOP("mcp", "HTTP con token, o stdio con el puente ba-mcp.")},
    {"escenas", QT_TRANSLATE_NOOP("mcp", "Escenas"),
     QT_TRANSLATE_NOOP("mcp", "Cambiar, crear, definir completas, exportar/importar, transiciones.")},
    {"fuentes", QT_TRANSLATE_NOOP("mcp", "Fuentes y ubicacion"),
     QT_TRANSLATE_NOOP("mcp", "Tipos, settings mas usados, propiedades, presets de ubicacion.")},
    {"imagenes", QT_TRANSLATE_NOOP("mcp", "Imagenes y video (vision)"),
     QT_TRANSLATE_NOOP("mcp", "Que herramienta de captura usar y como analizar video.")},
    {"audio", QT_TRANSLATE_NOOP("mcp", "Audio"),
     QT_TRANSLATE_NOOP("mcp", "Volumen, silencio y como leer los niveles.")},
    {"salidas", QT_TRANSLATE_NOOP("mcp", "Transmision y grabacion"),
     QT_TRANSLATE_NOOP("mcp", "Salir al aire, grabar, controles previos.")},
    {"sala", QT_TRANSLATE_NOOP("mcp", "Conferencia (videollamada con invitados)"),
     QT_TRANSLATE_NOOP("mcp", "Lobby, admitir, hablar en privado, pedidos, chat, archivos, enlaces.")},
    {"acceso_remoto", QT_TRANSLATE_NOOP("mcp", "Acceso desde internet: pinggy, Cloudflare, túnel propio"),
     QT_TRANSLATE_NOOP("mcp", "Sacar la conferencia a internet. Con pinggy alcanza la API key: la app arma todo sola.")},
    {"evento", QT_TRANSLATE_NOOP("mcp", "Guion del evento (storyboard)"),
     QT_TRANSLATE_NOOP("mcp", "Bloques, cues, tiempos previstos, revision previa, ensayo y teleprompter.")},
    {"prompter", QT_TRANSLATE_NOOP("mcp", "Teleprompter"),
     QT_TRANSLATE_NOOP("mcp", "Escribir y acortar discursos, mandarlos al atril y manejar el texto mientras alguien lo lee.")},
    {"oradores", QT_TRANSLATE_NOOP("mcp", "Modo oradores"),
     QT_TRANSLATE_NOOP("mcp", "Fila con turnos y reloj, carrusel, tablero.")},
    {"conferencia", QT_TRANSLATE_NOOP("mcp", "Central de conferencia (Bosch DCN)"),
     QT_TRANSLATE_NOOP("mcp", "Micrófonos de banca, pedidos de palabra, siguiente orador.")},
    {"sesion", QT_TRANSLATE_NOOP("mcp", "Banca virtual (sesión remota)"),
     QT_TRANSLATE_NOOP("mcp", "Integrantes desde el navegador, en la sala o afuera.")},
    {"totems", QT_TRANSLATE_NOOP("mcp", "Registro de personas (tótems)"),
     QT_TRANSLATE_NOOP("mcp", "Una TV con cámara o el celular: la gente se registra hablando.")},
    {"asamblea", QT_TRANSLATE_NOOP("mcp", "Sesión de asamblea"),
     QT_TRANSLATE_NOOP("mcp", "Cómo se prepara una sesión: Cuerpo, recinto, cámaras, ESCENAS, central, remotos, debate.")},
    {"procedimiento_asamblea", QT_TRANSLATE_NOOP("mcp", "Procedimiento: sesión de asamblea"),
     QT_TRANSLATE_NOOP("mcp", "Paso a paso para diseñar, controlar, ensayar y salir con una sesión de asamblea (cuerpo con bancas, quórum, orden del día, votaciones, remotos).")},
    {"graficos", QT_TRANSLATE_NOOP("mcp", "Gráficos animados"),
     QT_TRANSLATE_NOOP("mcp", "Zócalos, placas, tickers; GIF y videos con alfa adentro; tortas y barras con datos que cambian en vivo; gráficos que dibuja un plugin de Python.")},
    {"laminas", QT_TRANSLATE_NOOP("mcp", "Carruseles de laminas"),
     QT_TRANSLATE_NOOP("mcp", "La presentacion del orador, lamina por lamina.")},
    {"configuracion", QT_TRANSLATE_NOOP("mcp", "Configuracion"),
     QT_TRANSLATE_NOOP("mcp", "Leer y cambiar cualquier ajuste.")},
    {"documentos", QT_TRANSLATE_NOOP("mcp", "Documentación del evento y PDF"),
     QT_TRANSLATE_NOOP("mcp", "Los PDF del evento (por punto del orden del día y por sección) que la web publica, y convertir cualquier cosa a PDF sin Chrome.")},
    {"configuraciones", QT_TRANSLATE_NOOP("mcp", "Configuraciones con nombre"),
     QT_TRANSLATE_NOOP("mcp", "Varias configuraciones enteras (escenas, medios, plugins, botonera, Cuerpo, eventos), la demo, y pasar de una a otra sin copiar nada.")},
    {"puesta-en-marcha", QT_TRANSLATE_NOOP("mcp", "Poner en marcha la asistencia con IA"),
     QT_TRANSLATE_NOOP("mcp", "Qué activar y configurar, en qué orden, para que el asistente, la voz del operador, el dictado y las voces anden: modelos, micrófono, auriculares, voz hablada.")},
    {"interfaz", QT_TRANSLATE_NOOP("mcp", "Interfaz y ventanas"),
     QT_TRANSLATE_NOOP("mcp", "Paginas, avisos, dashboard, ventanas de salida.")},
    {"youtube", QT_TRANSLATE_NOOP("mcp", "YouTube"),
     QT_TRANSLATE_NOOP("mcp", "Conectar la cuenta, videos, subidas, eventos en vivo, chat y chat en pantalla.")},
    {"cuentas", QT_TRANSLATE_NOOP("mcp", "Conectar YouTube, Twitch y Facebook"),
     QT_TRANSLATE_NOOP("mcp", "Guiar a la persona para crear las credenciales y conectar las cuentas, sin que los secretos pasen por el chat.")},
    {"audio_rack", QT_TRANSLATE_NOOP("mcp", "Rack de audio: analizar, armar y calibrar"),
     QT_TRANSLATE_NOOP("mcp", "Procesar el audio de cada fuente con modulos LSP (gate, compresor, de-esser, EQ, multibanda, limitador), medir con el analizador y calibrar con numeros, no a oido.")},
    {"twitch", QT_TRANSLATE_NOOP("mcp", "Twitch"),
     QT_TRANSLATE_NOOP("mcp", "Conectar el canal, titulo y categoria, transmitir, VODs y chat.")},
    {"facebook", QT_TRANSLATE_NOOP("mcp", "Facebook"),
     QT_TRANSLATE_NOOP("mcp", "Conectar Paginas, vivos (prueba y publico), videos, subidas y comentarios.")},
    {"equipo", QT_TRANSLATE_NOOP("mcp", "El equipo"),
     QT_TRANSLATE_NOOP("mcp", "Monitores, audio, camaras, GPU: para decidir con datos.")},
    {"entradas", QT_TRANSLATE_NOOP("mcp", "Entradas de video"),
     QT_TRANSLATE_NOOP("mcp", "Camaras USB, capturadoras y camaras/señales por la red.")},
    {"video", QT_TRANSLATE_NOOP("mcp", "Grafo de video"),
     QT_TRANSLATE_NOOP("mcp", "Armar pipelines de video: correccion compartida entre camaras, modulos de GStreamer, compositor, unir camaras (panoramica, 360), entradas de red, relojes y sincronia.")},
    {"equipos", QT_TRANSLATE_NOOP("mcp", "Dispositivos y equipos"),
     QT_TRANSLATE_NOOP("mcp", "Detectar equipos (red local, camaras, microfonos y placas de la maquina, NDI, USB/HID/serie), ponerles nombre y manejarlos con acciones: ficha, funciones ONVIF, operaciones ONVIF crudas, pedidos HTTP a la API de la marca, claves, agregar como entrada, PTZ real, explorar UVC.")},
    {"botonera", QT_TRANSLATE_NOOP("mcp", "Botonera: tablet por QR, Stream Deck, MIDI"),
     QT_TRANSLATE_NOOP("mcp", "La superficie de control estilo Bitfocus Companion: perfiles (puestos) con permisos, paginas de botones, faders y perillas con estado vivo del bus de eventos, emparejar tablets por QR, y aparatos de hardware por plugin.")},
    {"plugins", QT_TRANSLATE_NOOP("mcp", "Plugins de Python"),
     QT_TRANSLATE_NOOP("mcp", "Cuando un equipo o un pedido no se puede con lo que hay: escribir un plugin (PTZ raros por USB/HID, camaras 360, varias camaras que se sincronizan, datos de quien habla, tarjetas de video y audio en Python, manejar el sistema).")},
    {"camaras_red", QT_TRANSLATE_NOOP("mcp", "Camaras de la red"),
     QT_TRANSLATE_NOOP("mcp", "Encontrar camaras IP, grabadores y equipos de video en la red y conectarlos bien (marca, URLs, clave, PTZ, normalizar).")},
    {"camaras", QT_TRANSLATE_NOOP("mcp", "Camaras PTZ"),
     QT_TRANSLATE_NOOP("mcp", "Mover las camaras (pan, tilt, zoom, foco, presets) y mejorar su imagen.")},
    {"herramientas", QT_TRANSLATE_NOOP("mcp", "Herramientas de medios"),
     QT_TRANSLATE_NOOP("mcp", "ffmpeg, ffprobe, yt-dlp y gst-launch INCLUIDOS en el programa (no se bajan): arreglar un archivo de video o audio (desfase de audio, formato, recorte, volumen) o traer un video de una pagina.")},
    {"traduccion", QT_TRANSLATE_NOOP("mcp", "Traducción en vivo"),
     QT_TRANSLATE_NOOP("mcp", "Traducir lo que dice el orador (castellano) a inglés, portugués, francés, alemán, ruso o chino, sin red: subtítulos y voz en otro idioma; el glosario de términos del evento (los de una asamblea incluidos).")},
    {"superres", QT_TRANSLATE_NOOP("mcp", "Superresolución"),
     QT_TRANSLATE_NOOP("mcp", "Mejorar la resolución de una cámara de baja definición (PAL, 720p): motores apple, fsr y redes ONNX (red chica entrenada con la propia cámara, SPAN, Real-ESRGAN).")},
    {"medios", QT_TRANSLATE_NOOP("mcp", "Medios"),
     QT_TRANSLATE_NOOP("mcp", "Buscar imagenes, videos y musica del sistema y usarlos en escenas.")},
    {"operador", QT_TRANSLATE_NOOP("mcp", "Hablar con el operador"),
     QT_TRANSLATE_NOOP("mcp", "Barra de mensajes, preguntas y respuestas sugeridas a invitados.")},
    {"alertas", QT_TRANSLATE_NOOP("mcp", "Disparadores (alertas)"),
     QT_TRANSLATE_NOOP("mcp", "Que el servidor vigile algo y avise o actue solo.")},
    {"sincronia", QT_TRANSLATE_NOOP("mcp", "Sincronía: todo alineado con la menor latencia"),
     QT_TRANSLATE_NOOP("mcp", "Cómo se alinean las entradas, quién marca la latencia y cómo bajarla sin trabar el aire.")},
    {"tiempos", QT_TRANSLATE_NOOP("mcp", "Latencias: la cadena de demoras y cómo trabajan los plugins"),
     QT_TRANSLATE_NOOP("mcp", "Cuánto tarda cada cosa, por qué, y qué plugin rompe la cadena en la GPU.")},
    {"diagnostico", QT_TRANSLATE_NOOP("mcp", "Diagnostico"),
     QT_TRANSLATE_NOOP("mcp", "Recetas cuando algo anda mal.")},
    {"recetas", QT_TRANSLATE_NOOP("mcp", "Recetas"),
     QT_TRANSLATE_NOOP("mcp", "Flujos completos de produccion.")},
    {"agentes", QT_TRANSLATE_NOOP("mcp", "Agentes automaticos"),
     QT_TRANSLATE_NOOP("mcp", "Roles que atienden solos lo que llega de afuera, y como pedirles algo.")},
};

std::string settingsJson(SettingsModel& s) {
    s.revert();
    return modelRows(s).dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace

QString McpService::agentPrompt() const {
    std::string topics;
    for (const TopicDef& t : kTopics)
        topics += std::string("- `") + t.id + "` — " + ba::i18n::traducir("mcp", t.title) + ": " +
                  ba::i18n::traducir("mcp", t.summary) + "\n";
    std::string cats;
    for (const auto& [id, title] : core_.categoryTitles())
        cats += "- " + title + " (`" + id + "`)\n";
    // El texto: prompts/asistente.md, con %1 los temas, %2 las categorías y %3 las instrucciones.
    const std::string body = ba::i18n::llenar(ba::i18n::leer("prompts/asistente.md"), {topics, cats, instrucciones()});
    return QString::fromStdString(body);
}

void McpService::registerHelp() {
    core_.setServerInfo("broadcast-appliance", "1.0", instrucciones());
    for (const TopicDef& t : kTopics)
        core_.addTopic(HelpTopic{t.id, ba::i18n::traducir("mcp", t.title), ba::i18n::traducir("mcp", t.summary),
                                 ba::i18n::leer(std::string("ayuda/") + t.id + ".md")});

    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    SettingsModel* settings = settings_.get();
    core_.addResource(
        Resource{"ba://estado", "estado", ba::i18n::traducir("mcp", "Estado general en vivo (JSON)."), "application/json", [&rt]() {
                     const EngineStatus st = rt.engine().status();
                     return json{{"currentScene", rt.engine().currentScene()},
                                 {"streaming", st.streaming},
                                 {"recording", st.recording},
                                 {"fps", st.fps},
                                 {"droppedFrames", st.droppedFrames},
                                 {"videoEncoder", st.videoEncoder}}
                         .dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
                 }});
    core_.addResource(
        Resource{"ba://escenas", "escenas", ba::i18n::traducir("mcp", "Escenas y fuentes (JSON)."), "application/json", [&rt]() {
                     json out = json::array();
                     for (const auto& n : rt.engine().sceneNames())
                         out.push_back(json{{"name", n}, {"sources", rt.engine().sceneSources(n)}});
                     return json{{"current", rt.engine().currentScene()}, {"scenes", out}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
                 }});
    core_.addResource(Resource{"ba://sala", "sala", ba::i18n::traducir("mcp", "Estado de la conferencia (JSON)."), "application/json",
                               [&vm]() { return roomSnapshot(vm).dump(2, ' ', false, nlohmann::json::error_handler_t::replace); }});
    core_.addResource(Resource{"ba://config", "config", ba::i18n::traducir("mcp", "Todos los ajustes (JSON)."), "application/json",
                               [settings]() { return settingsJson(*settings); }});

    // Las guías (prompts MCP): el nombre es API; el título, la descripción y el texto van en
    // el idioma del operador. Un dato opcional entra como %1 entero (una oración o nada).
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    core_.addPrompt(Prompt{
        "director", traducir("mcp", "Asistente de direccion"),
        traducir("mcp", "Operar el programa como director: mirar, decidir, cambiar y verificar."),
        json::array({json{{"name", "objetivo"},
                          {"description", traducir("mcp", "Que se quiere lograr en el programa.")},
                          {"required", false}}}),
        [](const json& a) {
            const std::string objetivo = a.value("objetivo", "");
            return llenar(traducir("mcp", "Sos el director tecnico del programa en vivo de Broadcast Appliance. "
                                          "%1Trabaja en ciclos: (1) `status` y `foto_programa` para ver que sale al aire; "
                                          "(2) `room_status`/`foto_todos_invitados` si hay invitados; (3) decidi el cambio minimo; "
                                          "(4) prepara lo nuevo fuera del aire y revisalo con `foto_fuente`; (5) aplicalo "
                                          "(`scene_switch`, `source_update`...) y verifica con `foto_programa`. Explica en "
                                          "una linea cada decision. Ante dudas: `help`."),
                          {objetivo.empty() ? std::string() : llenar(traducir("mcp", "Objetivo: %1. "), {objetivo})});
        }});
    core_.addPrompt(Prompt{
        "salir_al_aire", traducir("mcp", "Checklist para salir al aire"),
        traducir("mcp", "Revisa todo y arranca la transmision."), json::array(), [](const json&) {
            return traducir("mcp",
                            "Hace el checklist para salir al aire: 1) `status` (fps estable, sin cuadros perdidos, "
                            "gateway vivo); 2) `foto_programa` y describi lo que se ve; 3) `mixer_list` (niveles "
                            "razonables, nada muteado por error); 4) `room_status` si hay invitados; 5) si todo "
                            "esta bien, `record_start` y `stream_start`; 6) `status` para confirmar. Si algo falla, "
                            "decime que y no transmitas.");
        }});
    core_.addPrompt(Prompt{
        "conducir_oradores", traducir("mcp", "Conducir una ronda de oradores"),
        traducir("mcp", "Maneja la fila de oradores con tiempos."),
        json::array({json{{"name", "tiempo"},
                          {"description", traducir("mcp", "Tiempo por orador (m:ss).")},
                          {"required", false}}}),
        [](const json& a) {
            const std::string tiempo = a.value("tiempo", "");
            return llenar(traducir("mcp", "Conduci una ronda de oradores: `speakers_config` con enabled=true%1"
                                          "; admiti a los del lobby; `speaker_next` para el primero; revisa `remotos_fila_ver` "
                                          "seguido; cuando a alguien le queden 30 s avisale por `chat_send`; si se pasa, pasa al "
                                          "siguiente con `speaker_next`. Verifica con `foto_programa` que el orador se vea."),
                          {tiempo.empty() ? std::string() : llenar(traducir("mcp", " y defaultTime=%1"), {tiempo})});
        }});
    core_.addPrompt(Prompt{
        "armar_escena", traducir("mcp", "Armar una escena"),
        traducir("mcp", "Diseña y crea una escena a partir de una descripcion."),
        json::array({json{{"name", "descripcion"},
                          {"description", traducir("mcp", "Como tiene que verse.")},
                          {"required", true}}}),
        [](const json& a) {
            return llenar(traducir("mcp", "Arma una escena nueva que cumpla: %1"
                                          ". Pasos: `source_types` y `help {\"topic\": \"fuentes\"}` si hace falta; `scene_define` "
                                          "(sin ponerla al aire); `foto_fuente` para verla; ajusta con `source_update` hasta "
                                          "que quede bien; mostrame la captura final y preguntame antes de `scene_switch`."),
                          {a.value("descripcion", traducir("mcp", "(sin descripcion)"))});
        }});
    core_.addPrompt(Prompt{
        "diagnostico", traducir("mcp", "Diagnosticar un problema"),
        traducir("mcp", "Investiga un problema del programa o la conferencia."),
        json::array({json{{"name", "problema"},
                          {"description", traducir("mcp", "Que esta pasando.")},
                          {"required", true}}}),
        [](const json& a) {
            return llenar(traducir("mcp", "Diagnostica: %1"
                                          ". Segui `help {\"topic\": \"diagnostico\"}`: estado, capturas, mezclador, conferencia y "
                                          "registros (`log_leer`). Informa la causa probable y proponé el arreglo antes de "
                                          "aplicar algo que se vea al aire."),
                          {a.value("problema", traducir("mcp", "(sin detalle)"))});
        }});
}

} // namespace ba::app::mcp
