// ============================================================================
// src/app/tests/test_registro_agente.cpp — lo que entiende el modelo en un tótem (sin red)
//
//   1. El troceo en frases para la voz: corta en . ? ! … ; : con un espacio después, no en
//      «gmail.com» ni en «11.222.333», espera a ver qué sigue, comillas que cierran, una frase
//      muy larga en la última coma.
//   2. La lectura del streaming (SSE): texto de a pedazos partidos en cualquier lado, llamadas
//      a herramientas armadas por índice, [DONE], un error, una respuesta sin streaming.
//   3. Las herramientas validan: un correo que no es, un campo que no existe, confirmar sin
//      nada pendiente, una herramienta que no existe; lo que valida queda normalizado.
//   4. Los turnos, guionados (iniciativa mixta): varios datos de una se confirman juntos;
//      una corrección; un «no» suelto a varios pregunta cuál; una pregunta sobre el evento y el
//      guion retoma; un dato que no valida y el modelo repregunta (otra vuelta); el modelo no
//      hace nada y contesta el guion; sin modelo, el «no» a varios pregunta todo de nuevo.
//   5. El pedido: el prompt con el evento, los campos y la ficha; las herramientas con las
//      claves del tótem; la historia corta.
// ============================================================================
#include <cstdio>
#include <string>

#include "registro/AgenteRegistro.hpp"
#include "registro/Conversacion.hpp"

using namespace ba::registro;
using nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

bool contiene(const std::string& s, const std::string& x) { return s.find(x) != std::string::npos; }

/// Lo que se puede decir de `texto` mandado de a `paso` letras (como llega del modelo).
std::vector<std::string> trocear(const std::string& texto, std::size_t paso) {
    TrozadorFrases t;
    std::vector<std::string> out;
    for (std::size_t i = 0; i < texto.size(); i += paso)
        for (const std::string& f : t.agregar(texto.substr(i, paso))) out.push_back(f);
    if (const std::string r = t.terminar(); !r.empty()) out.push_back(r);
    return out;
}

Totem totem() {
    Totem t;
    t.id = "hall";
    t.nombre = "Hall";
    t.foto = false; // la foto es de la cámara: acá no
    t.ficha = "Empieza a las 19 en la sala A. El café está en el primer piso.";
    Campo n;
    n.clave = "nombre";
    n.etiqueta = "Nombre";
    n.tipo = TipoCampo::Nombre;
    n.obligatorio = true;
    Campo c;
    c.clave = "correo";
    c.etiqueta = "Correo";
    c.tipo = TipoCampo::Correo;
    Campo f;
    f.clave = "telefono";
    f.etiqueta = "Teléfono";
    f.tipo = TipoCampo::Telefono;
    t.campos = {n, c, f};
    t.saludo = "{hola}, bienvenido a {evento}.";
    t.despedida = "Listo.";
    return t;
}

LlamadaHerramienta poner(const std::string& campo, const std::string& valor) {
    return {"", "registro_poner", json{{"campo", campo}, {"valor", valor}}.dump()};
}

TurnoModelo turno(const std::string& texto, std::vector<LlamadaHerramienta> ll = {}) {
    TurnoModelo t;
    t.texto = texto;
    t.llamadas = std::move(ll);
    return t;
}

void frases() {
    auto f = trocear("Hola. ¿Cómo andás? Todo bien!", 1);
    CHECK(f.size() == 3 && f[0] == "Hola." && f[1] == "¿Cómo andás?" && f[2] == "Todo bien!", "corta en . ? !");
    f = trocear("Tu correo es jorge.ortega@gmail.com y tu DNI 11.222.333. Listo", 3);
    CHECK(f.size() == 2 && f[0] == "Tu correo es jorge.ortega@gmail.com y tu DNI 11.222.333." && f[1] == "Listo",
          "no corta en un correo ni en un número con puntos");
    // Espera a ver qué sigue: «Hola.» solo, al final del pedazo, todavía no.
    TrozadorFrases t;
    CHECK(t.agregar("Hola.").empty(), "un punto al final del pedazo espera");
    auto x = t.agregar(" Sigo");
    CHECK(x.size() == 1 && x[0] == "Hola.", "con el espacio, sale");
    CHECK(t.terminar() == "Sigo", "lo que queda al terminar");
    f = trocear("Te dije «dale.» Y seguí… Bueno: listo", 2);
    CHECK(f.size() == 4 && f[0] == "Te dije «dale.»" && f[1] == "Y seguí…" && f[2] == "Bueno:" && f[3] == "listo",
          "comillas que cierran, suspensivos y dos puntos");
    f = trocear("Una línea\notra", 4);
    CHECK(f.size() == 2 && f[0] == "Una línea" && f[1] == "otra", "salto de línea");
    std::string largo;
    for (int i = 0; i < 12; ++i) largo += "una parte bastante larga, ";
    TrozadorFrases t2;
    const auto l = t2.agregar(largo);
    CHECK(!l.empty() && l[0].back() == ',', "una frase muy larga sin punto se corta en una coma");
    CHECK(paraHablar("**Hola**  #vos ") == "Hola vos", "sin markdown para la voz");
}

void streaming() {
    const std::string sse =
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"Emp\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"ieza a las 19.\"}}]}\n\n"
        ": ping\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"type\":\"function\","
        "\"function\":{\"name\":\"registro_poner\",\"arguments\":\"{\\\"campo\\\":\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"nombre\\\","
        "\\\"valor\\\":\\\"Ana\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,\"id\":\"c2\",\"function\":{\"name\":"
        "\"registro_confirmar\",\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";
    // Partido en pedazos de 7 bytes (cortes en medio de un evento y de un UTF-8).
    LectorSse l;
    std::string texto;
    for (std::size_t i = 0; i < sse.size(); i += 7) texto += l.agregar(sse.substr(i, 7));
    l.terminar();
    const TurnoModelo& t = l.turno();
    CHECK(texto == "Empieza a las 19." && t.texto == texto, "el texto, de a pedazos");
    CHECK(t.llamadas.size() == 2 && t.llamadas[0].id == "c1" && t.llamadas[0].nombre == "registro_poner",
          "la herramienta, por índice");
    CHECK(json::parse(t.llamadas[0].argumentos) == json({{"campo", "nombre"}, {"valor", "Ana"}}),
          "los argumentos armados de a pedazos");
    CHECK(t.llamadas[1].nombre == "registro_confirmar" && t.fin == "tool_calls" && l.terminado(), "la segunda y el fin");

    LectorSse e;
    e.agregar("data: {\"error\":{\"message\":\"sin lugar\"}}\n\n");
    e.terminar();
    CHECK(e.turno().error == "sin lugar", "un error en el streaming");

    // El servidor no hizo streaming: un JSON entero.
    LectorSse j;
    j.agregar(R"({"choices":[{"message":{"content":"Hola","tool_calls":[{"id":"x","function":{"name":"registro_poner","arguments":"{\"campo\":\"correo\",\"valor\":\"a@b.com\"}"}}]},"finish_reason":"stop"}]})");
    j.terminar();
    CHECK(j.turno().texto == "Hola" && j.turno().llamadas.size() == 1 && j.turno().llamadas[0].id == "x",
          "una respuesta sin streaming también");
}

void herramientas() {
    Conversacion c(totem(), "el Congreso");
    c.empezar({});
    bool rechazo = false;
    json r = ejecutarHerramienta(c, poner("correo", "jorge arroba"), rechazo);
    CHECK(rechazo && contiene(r.value("error", ""), "correo"), "un correo que no es: el error, para repreguntar");
    CHECK(c.datos().empty(), "y no se anota nada");
    r = ejecutarHerramienta(c, poner("empresa", "Acme"), rechazo);
    CHECK(rechazo && contiene(r.value("error", ""), "empresa"), "un campo que no existe");
    r = ejecutarHerramienta(c, poner("correo", "jorge punto ortega arroba gmail punto com"), rechazo);
    CHECK(!rechazo && r.value("ok", false) && contiene(r.value("anotado", ""), "arroba"), "lo dictado valida");
    CHECK(c.datos().value("correo", "") == "jorge.ortega@gmail.com", "normalizado");
    CHECK(c.hayPendientes(), "pendiente de confirmar");
    // «no» no es un nombre (el modelo chico lo anotaba de «¿hay café?»): lo rechaza el validador.
    r = ejecutarHerramienta(c, poner("nombre", "no"), rechazo);
    CHECK(rechazo, "«no» como nombre, rechazado");
    r = ejecutarHerramienta(c, {"", "registro_confirmar", "{}"}, rechazo);
    CHECK(rechazo, "confirmar lo que todavía no se le leyó: no");
    r = ejecutarHerramienta(c, {"", "registro_borrar_todo", "{}"}, rechazo);
    CHECK(rechazo && contiene(r.value("error", ""), "No existe"), "una herramienta que no existe");
    r = ejecutarHerramienta(c, {"", "registro_poner", "no es json"}, rechazo);
    CHECK(rechazo, "argumentos rotos");
    // Un número sin comillas también entra (el teléfono).
    r = ejecutarHerramienta(c, {"", "registro_poner", R"({"campo":"telefono","valor":1145678912})"}, rechazo);
    CHECK(!rechazo && c.datos().value("telefono", "") == "1145678912", "un número sin comillas");
}

void turnos() {
    // --- Todo de una: tres datos en un turno, el guion los confirma juntos ---
    {
        Conversacion c(totem(), "el Congreso");
        c.usarModelo(true);
        auto r0 = c.empezar({});
        CHECK(contiene(r0.decir, "nombre"), "el guion pregunta el nombre");
        AgenteRegistro a("Sos el asistente de {evento}.\n{campos}\n{ficha}", "local");
        const json b = a.empezarTurno(c, "soy Jorge Ortega, mi correo es jorge punto ortega arroba gmail punto com y mi "
                                         "teléfono once cuatro cinco seis siete ocho nueve uno dos");
        CHECK(b.value("stream", false) && b["tools"].size() == 3 && b.value("model", "") == "local", "el pedido");
        CHECK(contiene(b["messages"][0]["content"].get<std::string>(), "el Congreso") &&
                  contiene(b["messages"][0]["content"].get<std::string>(), "sala A"),
              "el sistema con el evento y la ficha");
        CHECK(contiene(b["messages"].back()["content"].get<std::string>(), "soy Jorge Ortega"), "lo que dijo");
        auto v = a.alTerminarVuelta(c, turno("", {poner("nombre", "Jorge Ortega"),
                                                  poner("correo", "jorge punto ortega arroba gmail punto com"),
                                                  poner("telefono", "once cuatro cinco seis siete ocho nueve uno dos")}));
        CHECK(!v.otraVuelta && !v.dijoAlgo && !v.rechazos, "una sola vuelta");
        bool guion = true;
        auto r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(!guion, "lo resolvió el modelo");
        CHECK(contiene(r.decir, "Anoté nombre Jorge Ortega, correo jorge punto ortega arroba") &&
                  contiene(r.decir, " y teléfono ") && contiene(r.decir, "¿Está todo correcto?") && contiene(r.decir, "cuál dato está mal"),
              "se confirman juntos, en una frase");
        CHECK(c.pedido().value("confirmando", false), "con los botones Sí / No");
        CHECK(c.paraElGuion("sí, es correcto"), "el «sí» suelto lo resuelve el guion");
        r = c.oido("sí, es correcto");
        CHECK(r.guardar && r.terminar && c.datos().size() == 3, "con el sí, guarda (ya tiene todo)");
        CHECK(c.datos().value("telefono", "") == "1145678912", "el teléfono normalizado");
    }
    // --- El botón «Sí, es correcto» de la pantalla confirma lo mismo ---
    {
        Conversacion c(totem(), "x");
        c.usarModelo(true);
        c.empezar({});
        AgenteRegistro a("{campos}", "local");
        a.empezarTurno(c, "jorge ortega, ana@gmail.com");
        auto v = a.alTerminarVuelta(c, turno("", {poner("nombre", "Jorge Ortega"), poner("correo", "ana@gmail.com")}));
        bool guion = true;
        c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        const std::string clave = c.pedido().value("clave", "");
        auto r = c.escrito(clave, "sí");
        CHECK(!r.guardar && contiene(r.decir, "teléfono") && c.datos().size() == 2, "el botón confirma y sigue con lo que falta");
    }
    // --- Una corrección mientras confirma ---
    {
        Conversacion c(totem(), "x");
        c.usarModelo(true);
        c.empezar({});
        AgenteRegistro a("{campos}", "local");
        a.empezarTurno(c, "soy jorge ortega, jorge@gmail.com");
        auto v = a.alTerminarVuelta(c, turno("", {poner("nombre", "Jorge Ortega"), poner("correo", "jorge@gmail.com")}));
        bool guion = true;
        c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        const std::string dicho = "no, el correo es con jota, jota o erre ge e arroba gmail punto com";
        CHECK(!c.paraElGuion(dicho), "una corrección con palabras va al modelo");
        const json b = a.empezarTurno(c, dicho);
        CHECK(contiene(b["messages"].back()["content"].get<std::string>(), "Sin confirmar"), "el modelo sabe qué está pendiente");
        v = a.alTerminarVuelta(c, turno("", {poner("correo", "jorge.ok@gmail.com")}));
        auto r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(contiene(r.decir, "jorge punto ok arroba") && contiene(r.decir, "Jorge Ortega"), "se vuelve a confirmar con lo corregido");
        CHECK(c.datos().value("correo", "") == "jorge.ok@gmail.com", "el correo bueno");
    }
    // --- Un «no» suelto a varios: con modelo pregunta cuál; el modelo lo corrige ---
    {
        Conversacion c(totem(), "x");
        c.usarModelo(true);
        c.empezar({});
        AgenteRegistro a("{campos}", "local");
        a.empezarTurno(c, "jorge ortega jorge@gmail.com");
        auto v = a.alTerminarVuelta(c, turno("", {poner("nombre", "Jorge Ortega"), poner("correo", "jorge@gmail.com")}));
        bool guion = true;
        c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        auto r = c.oido("no");
        CHECK(contiene(r.decir, "¿Qué dato está mal?"), "pregunta cuál");
        a.empezarTurno(c, "el nombre, es Jorge Ortiz");
        v = a.alTerminarVuelta(c, turno("", {poner("nombre", "Jorge Ortiz")}));
        r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(contiene(r.decir, "Jorge Ortiz") && contiene(r.decir, "¿Está todo correcto?"), "y lo vuelve a confirmar");
        // «sí» con más palabras, dicho al modelo: registro_confirmar.
        a.empezarTurno(c, "sí, ahora está todo bien");
        v = a.alTerminarVuelta(c, turno("", {{"", "registro_confirmar", "{}"}}));
        r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(!c.hayPendientes() && contiene(r.decir, "teléfono"), "confirmado por el modelo, sigue con lo que falta");
    }
    // --- Una pregunta sobre el evento: contesta el modelo y el guion retoma ---
    {
        Conversacion c(totem(), "x");
        c.usarModelo(true);
        auto r0 = c.empezar({});
        AgenteRegistro a("{campos}", "local");
        a.empezarTurno(c, "¿a qué hora empieza?");
        auto v = a.alTerminarVuelta(c, turno("Empieza a las 19, en la sala A."));
        bool guion = true;
        auto r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(!guion && v.dijoAlgo, "contestó el modelo");
        CHECK(r.decir == c.retomar() && contiene(r.decir, "nombre"), "y el guion retoma la pregunta del nombre");
        // Lo mismo con registro_decir (con tool_choice «required» habla por ahí).
        {
            Conversacion c2(totem(), "x");
            c2.usarModelo(true);
            c2.empezar({});
            AgenteRegistro a2("{campos}", "local");
            // El precalentamiento: el mismo sistema y las mismas herramientas, un token.
            const json pc = a2.precalentar(c2);
            CHECK(pc.value("max_tokens", 0) == 1 && pc["tools"].size() == 3, "precalentar: un token, con las herramientas");
            const json b2 = a2.empezarTurno(c2, "¿hay café?");
            CHECK(b2["messages"][0] == pc["messages"][0] && b2["tools"] == pc["tools"], "el turno empieza igual que el precalentamiento (lo encuentra en caché)");
            CHECK(b2.value("tool_choice", "") == "required" && b2["tools"].size() == 3, "siempre con herramientas, también para hablar");
            auto v2 = a2.alTerminarVuelta(c2, turno("", {{"d1", "registro_decir", R"({"texto":"Sí, hay café en la entrada."})"}}));
            CHECK(v2.decir == "Sí, hay café en la entrada." && v2.dijoAlgo && !v2.otraVuelta && !v2.rechazos, "registro_decir es lo que se dice");
            bool g2 = true;
            auto r2 = c2.despuesDelModelo(v2.dijoAlgo, v2.rechazos, g2);
            CHECK(!g2 && r2.decir == c2.retomar(), "y el guion retoma");
        }
        a.anotarIntercambio("¿a qué hora empieza?", "Empieza a las 19. " + r.decir);
        const json b = a.empezarTurno(c, "Jorge");
        CHECK(b["messages"].size() == 4 && b["messages"][2].value("role", "") == "assistant", "la historia corta va en el turno siguiente");
        // Un turno en el que anotó: la historia lleva la llamada y su resultado, no solo lo
        // que se dijo (si no, el modelo aprende de su historia a hablar sin anotar).
        v = a.alTerminarVuelta(c, turno("", {poner("nombre", "Jorge Ortega")}));
        r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        a.anotarIntercambio("Jorge", r.decir);
        const json b2 = a.empezarTurno(c, "sí, es correcto");
        const json& ms = b2["messages"];
        CHECK(ms.size() == 8 && ms[4].value("role", "") == "assistant" && ms[4].contains("tool_calls") &&
                  ms[5].value("role", "") == "tool" && ms[6].value("role", "") == "assistant" &&
                  ms[6].value("content", "") == r.decir,
              "la historia lleva las herramientas del turno y lo que se contestó");
    }
    // --- Un dato que no valida: otra vuelta y el modelo repregunta ---
    {
        Conversacion c(totem(), "x");
        c.usarModelo(true);
        c.empezar({});
        c.oido("Jorge Ortega"); // el guion: «Anoté Jorge Ortega…»
        c.oido("sí, es correcto"); // ahora pregunta el correo
        AgenteRegistro a("{campos}", "local");
        a.empezarTurno(c, "mi correo es jorge arroba");
        auto v = a.alTerminarVuelta(c, turno("", {{"t1", "registro_poner", R"({"campo":"correo","valor":"jorge arroba"})"}}));
        CHECK(v.otraVuelta && v.rechazos, "otra vuelta con el resultado");
        const json& ms = v.cuerpo["messages"];
        CHECK(ms[ms.size() - 2].value("role", "") == "assistant" && ms.back().value("role", "") == "tool" &&
                  ms.back().value("tool_call_id", "") == "t1" && contiene(ms.back().value("content", ""), "error"),
              "la llamada y su error van al modelo");
        v = a.alTerminarVuelta(c, turno("Perdón, ¿me repetís el correo despacio?"));
        bool guion = true;
        auto r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(!v.otraVuelta && !guion && r.decir.empty(), "repreguntó el modelo: el guion no agrega nada");
        // Si el modelo no dice nada después del error, el reintento es del guion.
        a.empezarTurno(c, "jorge arroba");
        v = a.alTerminarVuelta(c, turno("", {poner("correo", "jorge arroba")}));
        v = a.alTerminarVuelta(c, turno("", {poner("correo", "jorge arroba")}));
        CHECK(!v.otraVuelta, "a lo sumo dos vueltas");
        r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(contiene(r.decir, "correo"), "el reintento del guion");
    }
    // --- El modelo no hace nada: lo entiende el guion ---
    {
        Conversacion c(totem(), "x");
        c.usarModelo(true);
        c.empezar({});
        AgenteRegistro a("{campos}", "local");
        a.empezarTurno(c, "Jorge Ortega");
        auto v = a.alTerminarVuelta(c, turno(""));
        bool guion = false;
        c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(guion, "sin texto ni herramientas: al guion");
        auto r = c.oido("Jorge Ortega");
        CHECK(contiene(r.decir, "Anoté Jorge Ortega"), "y el guion lo entiende solo");
    }
    // --- Confirmando por el guion (un dato), el modelo anota otro: se confirman los dos ---
    {
        Conversacion c(totem(), "x");
        c.usarModelo(true);
        c.empezar({});
        c.oido("Jorge Ortega"); // «Anoté Jorge Ortega. ¿Es correcto?»
        AgenteRegistro a("{campos}", "local");
        const json b = a.empezarTurno(c, "sí, y mi correo es ana arroba gmail punto com");
        CHECK(contiene(b["messages"].back()["content"].get<std::string>(), "Se está confirmando"), "sabe qué se confirma");
        auto v = a.alTerminarVuelta(c, turno("", {{"", "registro_confirmar", "{}"}, poner("correo", "ana arroba gmail punto com")}));
        bool guion = true;
        auto r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(c.datos().value("nombre", "") == "Jorge Ortega" && contiene(r.decir, "ana arroba gmail") && !contiene(r.decir, "Jorge"),
              "el nombre confirmado, el correo a confirmar");
    }
    // --- Sin modelo: un «no» a varios pendientes pregunta todo de nuevo ---
    {
        Conversacion c(totem(), "x");
        c.empezar({});
        std::string e;
        c.empezarTurnoModelo();
        c.proponer("nombre", "Jorge Ortega", e);
        c.proponer("correo", "jorge@gmail.com", e);
        bool guion = true;
        c.despuesDelModelo(false, false, guion);
        auto r = c.oido("no");
        CHECK(contiene(r.decir, "vamos de nuevo") && c.datos().empty(), "sin modelo: de nuevo, desde el primero");
    }
    // --- Lo que resuelve el guion solo, al instante ---
    {
        Conversacion c(totem(), "x");
        CHECK(c.paraElGuion("hola"), "sin empezar: el guion");
        c.empezar({});
        CHECK(c.paraElGuion("cancelar") && c.paraElGuion("¿cómo?"), "cancelar y repetir");
        CHECK(!c.paraElGuion("me llamo Jorge Ortega"), "una respuesta va al modelo");
        c.oido("Jorge Ortega");
        CHECK(c.paraElGuion("sí") && c.paraElGuion("no, es incorrecto") && !c.paraElGuion("no, me llamo Jorge Ortiz"),
              "confirmando: el sí o el no suelto, al guion; la corrección, al modelo");
    }
    // --- Confirmar apagado: lo del modelo se anota y el guion sigue ---
    {
        Totem t = totem();
        t.confirmar = false;
        Conversacion c(t, "x");
        c.usarModelo(true);
        c.empezar({});
        AgenteRegistro a("{campos}", "local");
        a.empezarTurno(c, "Jorge Ortega, jorge@gmail.com");
        auto v = a.alTerminarVuelta(c, turno("", {poner("nombre", "Jorge Ortega"), poner("correo", "jorge@gmail.com")}));
        bool guion = true;
        auto r = c.despuesDelModelo(v.dijoAlgo, v.rechazos, guion);
        CHECK(contiene(r.decir, "Perfecto") && contiene(r.decir, "teléfono") && c.datos().size() == 2, "sin confirmar, sigue");
    }
}

void pedido() {
    Totem t = totem();
    t.ficha.clear();
    const std::string s = sistemaRegistro("Sos de {evento}.\nCampos:\n{campos}\nFicha: {ficha}", t, "");
    CHECK(contiene(s, "Sos de el evento.") && contiene(s, "- nombre (Nombre, nombre, obligatorio)") &&
              contiene(s, "- telefono (Teléfono, telefono)") && contiene(s, "Solo su nombre"),
          "el prompt con los campos y sin ficha");
    const json h = herramientasRegistro(t);
    CHECK(h[0]["function"]["parameters"]["properties"]["campo"]["enum"] == json({"nombre", "correo", "telefono"}),
          "las claves del tótem en la herramienta");
}

} // namespace

int main() {
    frases();
    streaming();
    herramientas();
    turnos();
    pedido();
    std::printf(g_fallos ? "\n%d fallos\n" : "\ntodo ok\n", g_fallos);
    return g_fallos ? 1 : 0;
}
