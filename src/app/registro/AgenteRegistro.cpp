// ============================================================================
// src/app/registro/AgenteRegistro.cpp — ver AgenteRegistro.hpp
// ============================================================================
#include "registro/AgenteRegistro.hpp"

#include <algorithm>
#include <cctype>

#include "registro/Conversacion.hpp"

namespace ba::registro {

using nlohmann::json;

namespace {

void reemplazar(std::string& s, const std::string& de, const std::string& a) {
    for (std::size_t p = s.find(de); p != std::string::npos; p = s.find(de, p + a.size())) s.replace(p, de.size(), a);
}

std::string recortar(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

/// Cuántos bytes ocupa el signo que cierra una frase en `s[i]` (0 = no es uno): . ? ! ; : y
/// los puntos suspensivos «…» (tres bytes en UTF-8).
std::size_t cierre(const std::string& s, std::size_t i) {
    const char c = s[i];
    if (c == '.' || c == '?' || c == '!' || c == ';' || c == ':') return 1;
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
        static_cast<unsigned char>(s[i + 2]) == 0xA6)
        return 3;
    return 0;
}

/// Lo que puede venir pegado DESPUÉS del cierre y es de la misma frase: comillas y paréntesis
/// que cierran («Hola.» y), y más signos («¿¡En serio?!»). Devuelve cuántos bytes.
std::size_t pegados(const std::string& s, std::size_t i) {
    std::size_t n = 0;
    while (i + n < s.size()) {
        const auto c = static_cast<unsigned char>(s[i + n]);
        if (c == '"' || c == ')' || c == '\'' || c == '.' || c == '?' || c == '!') {
            ++n;
        } else if (c == 0xC2 && i + n + 1 < s.size() && static_cast<unsigned char>(s[i + n + 1]) == 0xBB) { // »
            n += 2;
        } else if (c == 0xE2 && i + n + 2 < s.size() && static_cast<unsigned char>(s[i + n + 1]) == 0x80 &&
                   (static_cast<unsigned char>(s[i + n + 2]) == 0x9D || static_cast<unsigned char>(s[i + n + 2]) == 0x99)) {
            n += 3; // ” ’
        } else {
            break;
        }
    }
    return n;
}

/// Una frase de este largo sin un punto ya se dice, cortada en la última coma: la voz no
/// espera a que termine un párrafo.
constexpr std::size_t kFraseLarga = 180;

std::string texto(const json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}

} // namespace

// --- Frases ---------------------------------------------------------------------------------

std::vector<std::string> TrozadorFrases::agregar(const std::string& delta) {
    buf_ += delta;
    std::vector<std::string> out;
    std::size_t desde = 0;
    for (std::size_t i = 0; i < buf_.size();) {
        if (buf_[i] == '\n') {
            const std::string f = recortar(buf_.substr(desde, i - desde));
            if (!f.empty()) out.push_back(f);
            desde = ++i;
            continue;
        }
        const std::size_t n = cierre(buf_, i);
        if (n == 0) {
            ++i;
            continue;
        }
        const std::size_t fin = i + n + pegados(buf_, i + n);
        // Hace falta ver qué viene después: «gmail.com» no es un final; si todavía no llegó,
        // se espera al próximo pedazo.
        if (fin >= buf_.size()) break;
        if (!std::isspace(static_cast<unsigned char>(buf_[fin]))) {
            i = fin;
            continue;
        }
        const std::string f = recortar(buf_.substr(desde, fin - desde));
        if (!f.empty()) out.push_back(f);
        desde = i = fin;
    }
    buf_.erase(0, desde);
    if (buf_.size() > kFraseLarga) {
        const std::size_t coma = buf_.rfind(", ");
        if (coma != std::string::npos && coma > 20) {
            const std::string f = recortar(buf_.substr(0, coma + 1));
            if (!f.empty()) out.push_back(f);
            buf_.erase(0, coma + 2);
        }
    }
    return out;
}

std::string TrozadorFrases::terminar() {
    std::string f = recortar(buf_);
    buf_.clear();
    return f;
}

std::string paraHablar(const std::string& t) {
    std::string out;
    for (char c : t) {
        if (c == '*' || c == '#' || c == '`' || c == '~') continue;
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return recortar(out);
}

// --- El streaming ---------------------------------------------------------------------------

std::string LectorSse::agregar(const std::string& bytes) {
    if (crudo_.size() < 1 << 20) crudo_ += bytes;
    pend_ += bytes;
    std::string nuevo;
    for (std::size_t eol; (eol = pend_.find('\n')) != std::string::npos;) {
        std::string linea = pend_.substr(0, eol);
        pend_.erase(0, eol + 1);
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        nuevo += evento(linea);
    }
    return nuevo;
}

std::string LectorSse::evento(const std::string& linea) {
    if (linea.rfind("data:", 0) != 0) return {}; // comentarios («: ping»), «event:», líneas vacías
    vioData_ = true;
    const std::string datos = recortar(linea.substr(5));
    if (datos == "[DONE]") {
        fin_ = true;
        return {};
    }
    const json j = json::parse(datos, nullptr, false);
    if (!j.is_object()) return {};
    if (j.contains("error")) {
        t_.error = j["error"].is_object() ? texto(j["error"], "message") : j["error"].dump();
        if (t_.error.empty()) t_.error = j["error"].dump();
        return {};
    }
    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) return {};
    const json& ch = j["choices"][0];
    if (ch.contains("finish_reason") && ch["finish_reason"].is_string()) t_.fin = ch["finish_reason"].get<std::string>();
    if (!ch.contains("delta") || !ch["delta"].is_object()) return {};
    const json& d = ch["delta"];
    std::string nuevo;
    if (d.contains("content") && d["content"].is_string()) {
        nuevo = d["content"].get<std::string>();
        t_.texto += nuevo;
    }
    // Las herramientas llegan de a pedazos, por índice: el primero trae id y nombre, los
    // siguientes van agregando los argumentos.
    if (d.contains("tool_calls") && d["tool_calls"].is_array())
        for (const json& tc : d["tool_calls"]) {
            const std::size_t k = tc.contains("index") && tc["index"].is_number_unsigned()
                                      ? tc["index"].get<std::size_t>()
                                      : t_.llamadas.empty() ? 0 : t_.llamadas.size() - 1;
            if (k > 64) continue;
            if (t_.llamadas.size() <= k) t_.llamadas.resize(k + 1);
            LlamadaHerramienta& ll = t_.llamadas[k];
            if (!texto(tc, "id").empty()) ll.id = texto(tc, "id");
            if (tc.contains("function") && tc["function"].is_object()) {
                ll.nombre += texto(tc["function"], "name");
                ll.argumentos += texto(tc["function"], "arguments");
            }
        }
    return nuevo;
}

void LectorSse::terminar() {
    if (!pend_.empty()) {
        agregar("\n");
    }
    // No era streaming: el servidor contestó el JSON entero (o un error).
    if (!vioData_) {
        const json j = json::parse(crudo_, nullptr, false);
        if (j.is_object()) {
            if (j.contains("error"))
                t_.error = j["error"].is_object() ? texto(j["error"], "message") : j["error"].dump();
            else
                t_ = deRespuesta(j);
        }
    }
    fin_ = true;
}

TurnoModelo LectorSse::deRespuesta(const json& j) {
    TurnoModelo t;
    if (!j.is_object() || !j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) return t;
    const json& ch = j["choices"][0];
    t.fin = texto(ch, "finish_reason");
    const json m = ch.value("message", json::object());
    t.texto = texto(m, "content");
    if (m.contains("tool_calls") && m["tool_calls"].is_array())
        for (const json& tc : m["tool_calls"]) {
            LlamadaHerramienta ll;
            ll.id = texto(tc, "id");
            const json f = tc.value("function", json::object());
            ll.nombre = texto(f, "name");
            ll.argumentos = f.contains("arguments") && f["arguments"].is_string() ? f["arguments"].get<std::string>()
                                                                                  : f.value("arguments", json::object()).dump();
            t.llamadas.push_back(std::move(ll));
        }
    return t;
}

// --- El pedido ------------------------------------------------------------------------------

std::string sistemaRegistro(const std::string& prompt, const Totem& t, const std::string& evento) {
    std::string campos;
    for (const Campo& c : t.campos) {
        campos += "- " + c.clave + " (" + (c.etiqueta.empty() ? c.clave : c.etiqueta) + ", " + nombreDeTipo(c.tipo);
        if (c.obligatorio) campos += ", obligatorio";
        if (c.tipo == TipoCampo::Opcion && !c.opciones.empty()) {
            campos += ": ";
            for (std::size_t i = 0; i < c.opciones.size(); ++i) campos += (i ? " / " : "") + c.opciones[i];
        }
        if (c.tipo == TipoCampo::Documento) campos += ": se lee con la cámara; si lo dice, es el número";
        campos += ")\n";
    }
    if (!campos.empty()) campos.pop_back();
    std::string s = prompt;
    reemplazar(s, "{evento}", evento.empty() ? std::string("el evento") : evento);
    reemplazar(s, "{campos}", campos.empty() ? std::string("(ninguno)") : campos);
    reemplazar(s, "{ficha}", recortar(t.ficha).empty() ? std::string("Solo su nombre: nada más (ni horarios, ni lugar).")
                                                       : recortar(t.ficha));
    return s;
}

json herramientasRegistro(const Totem& t) {
    json claves = json::array();
    for (const Campo& c : t.campos) claves.push_back(c.clave);
    const json poner{
        {"type", "function"},
        {"function",
         {{"name", "registro_poner"},
          {"description",
           "Anota UN dato que dijo la persona (llamala una vez por cada dato, aunque sean varios en el mismo turno). "
           "El valor ya armado, como se escribe: un correo como correo (jorge.ortega@gmail.com: «arroba» es @, "
           "«punto» es .), un teléfono en dígitos (1145678901), un nombre con mayúsculas. Lo que dijo llega de un "
           "reconocedor de voz que a veces escribe mal («P. Ortega» o «Portega» por «punto ortega», «ciclo» por "
           "«cinco»): interpretalo con criterio. No inventes lo que no dijo; si no se entiende, no lo anotes y "
           "pedíselo de nuevo. Se le va a confirmar en voz alta. Si no tiene el formato, devuelve el error."},
          {"parameters",
           {{"type", "object"},
            {"properties",
             {{"campo", {{"type", "string"}, {"enum", claves}, {"description", "la clave del campo"}}},
              {"valor", {{"type", "string"}, {"description", "el dato ya armado, como se escribe"}}}}},
            {"required", {"campo", "valor"}}}}}}};
    const json confirmar{
        {"type", "function"},
        {"function",
         {{"name", "registro_confirmar"},
          {"description", "La persona dijo que sí, que está bien lo que se le estaba confirmando."},
          {"parameters", {{"type", "object"}, {"properties", json::object()}}}}}};
    const json decir{
        {"type", "function"},
        {"function",
         {{"name", "registro_decir"},
          {"description",
           "Lo que le decís a la persona en voz alta: la respuesta a su pregunta, o pedirle de nuevo un dato que no "
           "se entendió. Si solo anotaste datos, no hace falta."},
          {"parameters",
           {{"type", "object"},
            {"properties", {{"texto", {{"type", "string"}, {"description", "una o dos frases cortas, en rioplatense"}}}}},
            {"required", {"texto"}}}}}}};
    return json::array({poner, confirmar, decir});
}

json ejecutarHerramienta(Conversacion& conv, const LlamadaHerramienta& ll, bool& rechazo) {
    rechazo = false;
    const json a = json::parse(ll.argumentos.empty() ? std::string("{}") : ll.argumentos, nullptr, false);
    if (ll.nombre == "registro_poner") {
        if (!a.is_object()) {
            rechazo = true;
            return {{"error", "Los argumentos no son un JSON: {\"campo\": \"…\", \"valor\": \"…\"}."}};
        }
        const std::string campo = texto(a, "campo");
        // Un número o un booleano también se acepta (el modelo a veces no lo pone entre comillas).
        std::string valor = texto(a, "valor");
        if (valor.empty() && a.contains("valor") && !a["valor"].is_null() && !a["valor"].is_string())
            valor = a["valor"].is_boolean() ? (a["valor"].get<bool>() ? "sí" : "no") : a["valor"].dump();
        std::string error, quedo;
        if (!conv.proponer(campo, valor, error, &quedo)) {
            rechazo = true;
            return {{"error", error}, {"campo", campo}};
        }
        return {{"ok", true}, {"campo", campo}, {"anotado", quedo}};
    }
    if (ll.nombre == "registro_confirmar") {
        if (!conv.confirmarPendientes()) {
            rechazo = true;
            return {{"error", "No había nada esperando confirmación."}};
        }
        return {{"ok", true}};
    }
    if (ll.nombre == "registro_decir") {
        // Lo dice el tótem (AgenteRegistro::alTerminarVuelta lo junta en Vuelta::decir).
        if (!a.is_object() || recortar(texto(a, "texto")).empty()) {
            rechazo = true;
            return {{"error", "Falta el texto: {\"texto\": \"…\"}."}};
        }
        return {{"ok", true}};
    }
    rechazo = true;
    return {{"error", "No existe la herramienta «" + ll.nombre + "»: usá registro_poner, registro_confirmar o registro_decir."}};
}

// --- Los turnos -----------------------------------------------------------------------------

namespace {
/// La historia que va en cada turno: los últimos intercambios, no la charla entera (cada
/// palabra de más es prefill que paga la primera palabra de la respuesta).
constexpr std::size_t kHistoria = 4;
/// Una vuelta para anotar y, si algo no validó, otra para repreguntar. No más: la persona está
/// esperando frente a la pantalla.
constexpr int kVueltasMax = 2;
} // namespace

AgenteRegistro::AgenteRegistro(std::string prompt, std::string modelo)
    : prompt_(std::move(prompt)), modelo_(std::move(modelo)) {}

json AgenteRegistro::cuerpo(const json& mensajes) const {
    json todos = json::array();
    todos.push_back({{"role", "system"}, {"content", sistema_}});
    for (const json& intercambio : historia_)
        for (const json& m : intercambio) todos.push_back(m);
    for (const json& m : mensajes) todos.push_back(m);
    json b{{"model", modelo_}, {"messages", todos}, {"stream", true}, {"temperature", 0.2}, {"max_tokens", 300}};
    // Sin pensar: en una charla hablada lo que importa es la primera palabra.
    b["chat_template_kwargs"] = json{{"enable_thinking", false}};
    return b;
}

void AgenteRegistro::conHerramientas(json& cuerpo, const Totem& t) {
    cuerpo["tools"] = herramientasRegistro(t);
    // Siempre con herramientas, también para hablar (registro_decir): suelto, el modelo chico
    // contaba los datos en prosa («ya tengo tu correo…») sin anotarlos. Con «required» el
    // servidor lo obliga con una gramática a contestar con llamadas.
    cuerpo["tool_choice"] = "required";
}

json AgenteRegistro::precalentar(const Conversacion& conv) {
    if (sistema_.empty()) sistema_ = sistemaRegistro(prompt_, conv.totem(), conv.evento());
    json b = cuerpo(json::array({{{"role", "user"}, {"content", "Hola"}}}));
    conHerramientas(b, conv.totem());
    b["max_tokens"] = 1;
    return b;
}

json AgenteRegistro::empezarTurno(Conversacion& conv, const std::string& dicho) {
    if (sistema_.empty()) sistema_ = sistemaRegistro(prompt_, conv.totem(), conv.evento());
    conv.empezarTurnoModelo();
    vueltas_ = 0;
    dijoAlgo_ = false;
    rechazos_ = false;
    herramientas_ = json::array();
    const json e = conv.estadoParaModelo();
    std::string ctx = "Paso del guion: " + e.value("paso", std::string()) + ".";
    if (e.contains("pregunta"))
        ctx += "\nEl guion le preguntó (y se lo vuelve a preguntar él, no vos): «" + e["pregunta"].get<std::string>() + "»";
    if (e.contains("seConfirma")) ctx += "\nSe está confirmando: " + e["seConfirma"].dump();
    if (!e["anotados"].empty()) ctx += "\nAnotado: " + e["anotados"].dump();
    if (!e["pendientes"].empty()) ctx += "\nSin confirmar todavía: " + e["pendientes"].dump();
    if (e.value("corrigiendo", false)) ctx += "\nDijo que algún dato está mal: anotá el bueno.";
    if (!e["faltan"].empty()) ctx += "\nFalta: " + e["faltan"].dump();
    ctx += "\n\nLa persona dijo: «" + dicho + "»";
    mensajes_ = json::array({{{"role", "user"}, {"content", ctx}}});
    json b = cuerpo(mensajes_);
    conHerramientas(b, conv.totem());
    return b;
}

AgenteRegistro::Vuelta AgenteRegistro::alTerminarVuelta(Conversacion& conv, const TurnoModelo& t) {
    Vuelta v;
    ++vueltas_;
    if (!recortar(t.texto).empty()) dijoAlgo_ = true;
    bool rechazoVuelta = false;
    json llamadas = json::array();
    for (std::size_t i = 0; i < t.llamadas.size(); ++i) {
        const LlamadaHerramienta& ll = t.llamadas[i];
        bool rechazo = false;
        json r = ejecutarHerramienta(conv, ll, rechazo);
        if (ll.nombre == "registro_decir" && !rechazo) {
            const json a = json::parse(ll.argumentos, nullptr, false);
            v.decir += (v.decir.empty() ? "" : " ") + recortar(texto(a, "texto"));
            dijoAlgo_ = true;
        }
        rechazoVuelta = rechazoVuelta || rechazo;
        const std::string id = ll.id.empty() ? "llamada-" + std::to_string(vueltas_) + "-" + std::to_string(i) : ll.id;
        llamadas.push_back({{"id", id}, {"type", "function"}, {"function", {{"name", ll.nombre}, {"arguments", ll.argumentos}}}});
        v.resultados.push_back({{"role", "tool"}, {"tool_call_id", id}, {"content", r.dump()}});
    }
    rechazos_ = rechazos_ || rechazoVuelta;
    // Para la historia: lo que dijo va al final, una sola vez (con lo que agregue el guion).
    if (!llamadas.empty()) {
        herramientas_.push_back({{"role", "assistant"}, {"content", ""}, {"tool_calls", llamadas}});
        for (const json& r : v.resultados) herramientas_.push_back(r);
    }
    v.dijoAlgo = dijoAlgo_;
    v.rechazos = rechazos_;
    // Algo no validó y no lo repreguntó: una vuelta más con los resultados, para que lo pida
    // de nuevo con sus palabras (o lo anote bien).
    if (rechazoVuelta && recortar(t.texto).empty() && v.decir.empty() && vueltas_ < kVueltasMax) {
        json asistente{{"role", "assistant"}, {"content", t.texto}, {"tool_calls", llamadas}};
        mensajes_.push_back(asistente);
        for (const json& r : v.resultados) mensajes_.push_back(r);
        v.cuerpo = cuerpo(mensajes_);
        conHerramientas(v.cuerpo, conv.totem());
        v.otraVuelta = true;
    }
    return v;
}

void AgenteRegistro::anotarIntercambio(const std::string& persona, const std::string& asistente) {
    json intercambio = json::array({{{"role", "user"}, {"content", "La persona dijo: «" + persona + "»"}}});
    for (json& m : herramientas_) intercambio.push_back(std::move(m));
    herramientas_ = json::array();
    if (!asistente.empty()) intercambio.push_back({{"role", "assistant"}, {"content", asistente}});
    historia_.push_back(std::move(intercambio));
    while (historia_.size() > kHistoria) historia_.erase(historia_.begin());
}

} // namespace ba::registro
