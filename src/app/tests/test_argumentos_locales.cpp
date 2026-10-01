// src/app/tests/test_argumentos_locales.cpp — validar lo que escribe el modelo local
//
// agent/ArgumentosLocales: los casos que mostró el banco de voz (tools/voz-banco.py).

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
    CHECK((argumentos::leer("capture_program}", "capture_program") == json::object()),
          "leer: solo el nombre: {}");
    CHECK(
        (argumentos::resumen(json{{"name", "Música"}, {"volumeDb", -16}}) == "name: Música · volumeDb: -16"),
        "resumen legible");

    std::printf(g_failures == 0 ? "test_argumentos_locales: OK\n" : "test_argumentos_locales: %d fallos\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
