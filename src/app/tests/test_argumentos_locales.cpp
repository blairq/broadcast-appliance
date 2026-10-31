// src/app/tests/test_argumentos_locales.cpp — validar lo que escribe el modelo local
//
// agent/ArgumentosLocales: los casos que mostró el banco de voz (tools/entrenar/voz-banco.py).

#include "agent/ArgumentosLocales.hpp"

#include <cstdio>

using namespace ba::app::agent;
using json = nlohmann::json;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

const json kEstado = json::parse(R"J({
  "escena_actual": "Principal",
  "escenas": ["Principal", "Entrevista", "Cierre", "Doble cuadro"],
  "fuentes_de_la_escena_actual": [{"nombre": "Cámara", "visible": true}, {"nombre": "Logo", "visible": true}],
  "audio": [{"nombre": "Micrófono de MacBook Pro", "volumen_db": 0.0, "silenciada": false},
            {"nombre": "Invitado 2 (audio)", "volumen_db": -6.0, "silenciada": false
},
            {"nombre": "Música", "volumen_db": -12.0, "silenciada": false}],
  "participantes": [], "graficos": ["zocalo", "ticker"]
})J");

const json kAudioSet = json::parse(R"J({"title": "Volumen / silencio", "inputSchema": {"type": "object",
  "properties": {"name": {"type": "string"}, "volumeDb": {"type": "number", "minimum": -100, "maximum": 26},
                 "muted": {"type": "boolean"}}, "required": ["name"]}})J");
const json kSceneSwitch =
    json::parse(R"J({"title": "Poner una escena al aire", "inputSchema": {"type": "object",
  "properties": {"name": {"type": "string"}}, "required": ["name"]}})J");
const json kTransicion = json::parse(R"J({"title": "Transición", "inputSchema": {"type": "object",
  "properties": {"type": {"type": "string", "enum": ["cut", "fade"]}, "ms": {"type": "integer"}}}})J");

} // namespace

int main() {
    using argumentos::validar;

    auto r = validar("audio_set", kAudioSet, json{{"name", "musica"}, {"volumeDb", "-4"}}, kEstado);
    CHECK(r.args.value("name", "") == "Música", "nombre sin acento ni mayúscula -> Música");
    CHECK(r.args.value("volumeDb", 0.0) == -16.0, "relativo -4 sobre -12 = -16");
    CHECK(r.bloqueos.empty(), "sin bloqueos");

    r = validar("audio_set", kAudioSet, json{{"name", "Invitado 2"}, {"muted", true}}, kEstado);
    CHECK(r.args.value("name", "") == "Invitado 2 (audio)", "nombre parcial -> el único que lo contiene");
    CHECK(!r.args.contains("volumeDb"), "silenciar no toca el volumen");

    r = validar("audio_set", kAudioSet, json{{"name", "Música"}, {"volumeDb", "+50"}}, kEstado);
    CHECK(r.args.value("volumeDb", 0.0) == 26.0, "el máximo del esquema (+26)");

    r = validar("scene_switch", kSceneSwitch, json{{"name", "Cámara"}}, kEstado);
    CHECK(!r.bloqueos.empty(), "una fuente no es una escena: bloqueado");

    r = validar("scene_switch", kSceneSwitch, json{{"name", "doble cuadro"}}, kEstado);
    CHECK(r.args.value("name", "") == "Doble cuadro" && r.bloqueos.empty(),
          "escena por nombre, sin mayúsculas");

    r = validar("scene_switch", kSceneSwitch, json{{"name", "Bienvenida"}}, kEstado);
    CHECK(!r.bloqueos.empty(), "escena que no existe: bloqueado");

    r = validar("scene_switch", kSceneSwitch, json::object(), kEstado);
    CHECK(!r.bloqueos.empty(), "falta el requerido: bloqueado");

    r = validar("transition_set", kTransicion, json{{"type", "wipe"}, {"otra", 1}}, kEstado);
    CHECK(!r.bloqueos.empty(), "fuera del enum: bloqueado");
    CHECK(!r.args.contains("otra"), "clave de más: afuera");

    r = validar("transition_set", kTransicion, json{{"ms", "+200"}}, kEstado);
    CHECK(r.bloqueos.empty() && r.args.value("ms", 0) == 200, "con signo sin valor actual: el número");

    const json kCrear =
        json::parse(R"J({"inputSchema": {"type": "object", "properties": {"name": {"type": "string"}},
                                         "required": ["name"]}})J");
    r = validar("scene_create", kCrear, json{{"name", "Bloque 4"}}, kEstado);
    CHECK(r.bloqueos.empty() && r.args.value("name", "") == "Bloque 4",
          "crear: el nombre nuevo no tiene que existir");

    const json kChat = json::parse(R"J({"inputSchema": {"type": "object", "properties": {
        "to": {"type": "string", "description": "id de un participante, o \"all\" para toda la sala"},
        "text": {"type": "string"}}}})J");
    r = validar("chat_send", kChat, json{{"to", "all"}, {"text", "hola"}}, kEstado);
    CHECK(r.bloqueos.empty() && r.args.value("to", "") == "all",
          "valor especial de la descripción (\"all\")");

    r = validar("scene_switch", kSceneSwitch, json{{"name", "scene_switch"}}, kEstado);
    CHECK(!r.args.contains("name"), "el nombre de la herramienta como valor: afuera");

    CHECK((argumentos::leer(R"("name":"Cierre"} y sigo escribiendo {x})") == json{{"name", "Cierre"}}),
          "leer: el primer objeto balanceado");
    CHECK((argumentos::leer(R"("name": "a}b"})") == json{{"name", "a}b"}}), "leer: llaves dentro de textos");
    CHECK(argumentos::leer("no es json").is_null(), "leer: sin objeto, nulo");
    CHECK((argumentos::leer(R"(type: "cut"})", "transition_set") == json{{"type", "cut"}}),
          "leer: claves sin comillas");
    CHECK((argumentos::leer(R"("audio_analyze": {"source": "Música"}})", "audio_analyze") ==
           json{{"source", "Música"}}),
          "leer: envuelto en el nombre de la herramienta");
    CHECK((argumentos::leer("foto_programa}", "foto_programa") == json::object()),
          "leer: solo el nombre: {}");
    CHECK(
        (argumentos::resumen(json{{"name", "Música"}, {"volumeDb", -16}}) == "name: Música · volumeDb: -16"),
        "resumen legible");

    // v6: lo que el modelo copia en vez de un dato, y las formas raras.
    const json kProps =
        json::parse(R"J({"inputSchema": {"type": "object", "properties": {"name": {"type": "string"}},
                                         "required": ["name"]}})J");
    r = validar("source_properties", kProps, json{{"properties", {{"name", "Logo"}}}}, kEstado);
    CHECK(r.args.value("name", "") == "Logo" && r.bloqueos.empty(),
          "desenvuelto de una clave que no es del esquema");
    r = validar("scene_preview", kSceneSwitch, json{{"name", "preview_al_aire"}}, kEstado);
    CHECK(!r.args.contains("name") && !r.bloqueos.empty(),
          "un nombre de herramienta como dato: afuera (y falta)");
    const json kFeed = json::parse(R"J({"inputSchema": {"type": "object", "properties": {
        "key": {"type": "string", "description": "operator, guest:N o share:ID"}}}})J");
    r = validar("capture_feed", kFeed, json{{"key", "guest:N"}}, kEstado);
    CHECK(!r.args.contains("key"), "el comodín de la descripción (guest:N): afuera");
    r = validar("scene_export", kSceneSwitch, json{{"name", ""}}, kEstado);
    CHECK(!r.args.contains("name"), "texto vacío: afuera");
    CHECK((argumentos::leer(R"(capture_ui, format: "jpeg", maxWidth: 1280})", "capture_ui") ==
           json{{"format", "jpeg"}, {"maxWidth", 1280}}),
          "leer: «{herramienta, clave: …}»");
    CHECK((argumentos::estadoPara("scene_switch", kEstado).contains("escenas") &&
           !argumentos::estadoPara("scene_switch", kEstado).contains("audio")),
          "estadoPara: a una de escenas, solo las escenas");

    // v2: la llamada nativa (<tool_call><function=…><parameter=…>).
    auto tc =
        argumentos::leerTc("<tool_call>\n<function=audio_set>\n<parameter=name>\nMúsica\n</parameter>\n"
                           "<parameter=volumeDb>\n-4\n</parameter>\n</function>\n</tool_call><|im_end|>");
    CHECK(tc.hay && tc.funcion == "audio_set" && tc.parametros.size() == 2, "leerTc: función y parámetros");
    CHECK(tc.parametros.size() == 2 && tc.parametros[0].second == "Música" && tc.parametros[1].second == "-4",
          "leerTc: valores sin los saltos de línea");
    r = validar("audio_set", kAudioSet, argumentos::argsDesdeTc(tc, kAudioSet), kEstado);
    CHECK(r.args.value("volumeDb", 0.0) == -16.0 && r.args.value("name", "") == "Música",
          "tc: -4 es un cambio");
    tc = argumentos::leerTc("<tool_call>\n<function=audio_set>\n<parameter=name>\nMúsica\n</parameter>\n"
                            "<parameter=volumeDb>\n=-10\n</parameter>\n</function>\n</tool_call>");
    r = validar("audio_set", kAudioSet, argumentos::argsDesdeTc(tc, kAudioSet), kEstado);
    CHECK(r.args.value("volumeDb", 0.0) == -10.0, "tc: =-10 es exacto");
    tc = argumentos::leerTc("<tool_call>\n<function=audio_set>\n<parameter=name>\nMúsica\n</parameter>\n"
                            "<parameter=muted>\ntrue\n</parameter>\n</function>\n</tool_call>");
    CHECK(argumentos::argsDesdeTc(tc, kAudioSet).value("muted", false) == true, "tc: booleano");
    tc = argumentos::leerTc("<tool_call>\n<function=derivar>\n</function>\n</tool_call>");
    CHECK(tc.hay && tc.funcion == "derivar" && tc.parametros.empty(), "tc: derivar sin parámetros");
    tc = argumentos::leerTc("<tool_call>\n<function=falta_dato>\n<parameter=pregunta>\n¿A qué escena?\n"
                            "</parameter>\n</function>\n</tool_call>");
    CHECK(tc.funcion == "falta_dato" && tc.parametros.size() == 1 &&
              tc.parametros[0].second == "¿A qué escena?",
          "tc: falta_dato con la pregunta");
    CHECK(!argumentos::leerTc("Hola, ¿cómo andás?").hay, "tc: charla, sin llamada");
    tc = argumentos::leerTc("<tool_call>\n<function=transition_set>\n<parameter=ms>\n500\n</parameter>\n"
                            "</function>\n</tool_call>");
    CHECK(argumentos::argsDesdeTc(tc, kTransicion).value("ms", 0) == 500, "tc: entero sin signo, tal cual");

    // `salida` (PLAN-SALIDAS): con más de una, el estado las trae y se valida contra ellas.
    {
        json estado = kEstado;
        estado["salidas"] = json::parse(R"J([{"id": "principal", "nombre": "Principal"}, {"id": "yt2", "nombre": "YouTube 2"}])J");
        estado["salida"] = "principal";
        json ficha = kSceneSwitch;
        ficha["inputSchema"]["properties"]["salida"] = {{"type", "string"}};
        r = validar("scene_switch", ficha, json{{"name", "Cierre"}, {"salida", "youtube 2"}}, estado);
        CHECK(r.args.value("salida", "") == "YouTube 2" && r.bloqueos.empty(), "salida por nombre, sin mayúsculas");
        r = validar("scene_switch", ficha, json{{"name", "Cierre"}, {"salida", "yt2"}}, estado);
        CHECK(r.args.value("salida", "") == "yt2" && r.bloqueos.empty(), "salida por id");
        r = validar("scene_switch", ficha, json{{"name", "Cierre"}, {"salida", "Canal 9"}}, estado);
        CHECK(!r.bloqueos.empty(), "una salida que no existe: bloqueado");
        CHECK(argumentos::estadoPara("scene_switch", estado).contains("salidas"), "las de escenas ven las salidas");
        CHECK(!argumentos::estadoPara("scene_switch", kEstado).contains("salidas"), "con una sola, el estado de siempre");
    }

    std::printf(g_failures == 0 ? "test_argumentos_locales: OK\n" : "test_argumentos_locales: %d fallos\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
