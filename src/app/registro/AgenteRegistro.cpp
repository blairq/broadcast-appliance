// ============================================================================
// src/app/registro/AgenteRegistro.cpp — ver AgenteRegistro.hpp
// ============================================================================
#include "registro/AgenteRegistro.hpp"

#include <algorithm>
#include <cctype>

#include "i18n/I18n.hpp"
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

/// Lo que lee el modelo va en el idioma del evento (habla con el público). "" = el del operador.
ba::i18n::En enIdioma(const std::string& idioma) {
    return ba::i18n::En(idioma.empty() ? ba::i18n::actual().codigo : idioma);
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

std::string sistemaRegistro(const std::string& prompt, const Totem& t, const std::string& evento, const std::string& idioma) {
    const ba::i18n::En e = enIdioma(idioma);
    std::string campos;
    for (const Campo& c : t.campos) {
        // La clave y el tipo son API (lo que el modelo pone en registro_poner): no se traducen.
        campos += "- " + c.clave + " (" + (c.etiqueta.empty() ? c.clave : c.etiqueta) + ", " + nombreDeTipo(c.tipo);
        if (c.obligatorio) campos += ", " + e.traducir("asistente", "obligatorio");
        if (c.tipo == TipoCampo::Opcion && !c.opciones.empty()) {
            campos += ": ";
            for (std::size_t i = 0; i < c.opciones.size(); ++i) campos += (i ? " / " : "") + c.opciones[i];
        }
        if (c.tipo == TipoCampo::Documento)
            campos += ": " + e.traducir("asistente", "se lee con la cámara; si lo dice, es el número");
        campos += ")\n";
    }
    if (!campos.empty()) campos.pop_back();
    std::string s = prompt;
    reemplazar(s, "{evento}", evento.empty() ? e.traducir("asistente", "el evento") : evento);
    reemplazar(s, "{campos}", campos.empty() ? e.traducir("asistente", "(ninguno)") : campos);
    reemplazar(s, "{ficha}",
               recortar(t.ficha).empty() ? e.traducir("asistente", "Solo su nombre: nada más (ni horarios, ni lugar).")
                                         : recortar(t.ficha));
    return s;
}

json herramientasRegistro(const Totem& t, const std::string& idioma) {
    const ba::i18n::En e = enIdioma(idioma);
    json claves = json::array();
    for (const Campo& c : t.campos) claves.push_back(c.clave);
    const json poner{
        {"type", "function"},
        {"function",
         {{"name", "registro_poner"},
          // Con ejemplos del correo dictado en el idioma: va en un archivo por idioma.
          {"description", recortar(e.leer("prompts/totem/registro_poner.md"))},
          {"parameters",
           {{"type", "object"},
            {"properties",
             {{"campo", {{"type", "string"}, {"enum", claves}, {"description", e.traducir("asistente", "la clave del campo")}}},
              {"valor", {{"type", "string"}, {"description", e.traducir("asistente", "el dato ya armado, como se escribe")}}}}},
            {"required", {"campo", "valor"}}}}}}};
    const json confirmar{
        {"type", "function"},
        {"function",
         {{"name", "registro_confirmar"},
          {"description", e.traducir("asistente", "La persona dijo que sí, que está bien lo que se le estaba confirmando.")},
          {"parameters", {{"type", "object"}, {"properties", json::object()}}}}}};
    const json decir{
        {"type", "function"},
        {"function",
         {{"name", "registro_decir"},
          {"description",
           e.traducir("asistente", "Lo que le decís a la persona en voz alta: la respuesta a su pregunta, o pedirle de nuevo "
                                   "un dato que no se entendió. Si solo anotaste datos, no hace falta.")},
          {"parameters",
           {{"type", "object"},
            {"properties", {{"texto", {{"type", "string"}, {"description", e.traducir("asistente", "una o dos frases cortas, en rioplatense")}}}}},
            {"required", {"texto"}}}}}}};
    return json::array({poner, confirmar, decir});
}

json ejecutarHerramienta(Conversacion& conv, const LlamadaHerramienta& ll, bool& rechazo) {
    const ba::i18n::En e(conv.idioma());
    rechazo = false;
    const json a = json::parse(ll.argumentos.empty() ? std::string("{}") : ll.argumentos, nullptr, false);
    if (ll.nombre == "registro_poner") {
        if (!a.is_object()) {
            rechazo = true;
            return {{"error", e.traducir("asistente", "Los argumentos no son un JSON: {\"campo\": \"…\", \"valor\": \"…\"}.")}};
        }
        const std::string campo = texto(a, "campo");
        // Un número o un booleano también se acepta (el modelo a veces no lo pone entre comillas).
        std::string valor = texto(a, "valor");
        if (valor.empty() && a.contains("valor") && !a["valor"].is_null() && !a["valor"].is_string())
            valor = a["valor"].is_boolean() ? palabraSiNo(a["valor"].get<bool>(), conv.idioma()) : a["valor"].dump();
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
            return {{"error", e.traducir("asistente", "No había nada esperando confirmación.")}};
        }
        return {{"ok", true}};
    }
    if (ll.nombre == "registro_decir") {
        // Lo dice el tótem (AgenteRegistro::alTerminarVuelta lo junta en Vuelta::decir).
        if (!a.is_object() || recortar(texto(a, "texto")).empty()) {
            rechazo = true;
            return {{"error", e.traducir("asistente", "Falta el texto: {\"texto\": \"…\"}.")}};
        }
        return {{"ok", true}};
    }
    rechazo = true;
    return {{"error", ba::i18n::llenar(e.traducir("asistente", "No existe la herramienta «%1»: usá registro_poner, "
                                                               "registro_confirmar o registro_decir."),
                                       {ll.nombre})}};
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

void AgenteRegistro::conHerramientas(json& cuerpo, const Totem& t, const std::string& idioma) {
    cuerpo["tools"] = herramientasRegistro(t, idioma);
    // Siempre con herramientas, también para hablar (registro_decir): suelto, el modelo chico
    // contaba los datos en prosa («ya tengo tu correo…») sin anotarlos. Con «required» el
    // servidor lo obliga con una gramática a contestar con llamadas.
    cuerpo["tool_choice"] = "required";
}

json AgenteRegistro::precalentar(const Conversacion& conv) {
    idioma_ = conv.idioma();
    if (sistema_.empty()) sistema_ = sistemaRegistro(prompt_, conv.totem(), conv.evento(), idioma_);
    json b = cuerpo(json::array({{{"role", "user"}, {"content", ba::i18n::En(idioma_).traducir("asistente", "Hola")}}}));
    conHerramientas(b, conv.totem(), idioma_);
    b["max_tokens"] = 1;
    return b;
}

json AgenteRegistro::empezarTurno(Conversacion& conv, const std::string& dicho) {
    idioma_ = conv.idioma();
    const ba::i18n::En e(idioma_);
    if (sistema_.empty()) sistema_ = sistemaRegistro(prompt_, conv.totem(), conv.evento(), idioma_);
    conv.empezarTurnoModelo();
    vueltas_ = 0;
    dijoAlgo_ = false;
    rechazos_ = false;
    herramientas_ = json::array();
    const json est = conv.estadoParaModelo();
    const auto con = [](const std::string& plantilla, const std::string& dato) { return ba::i18n::llenar(plantilla, {dato}); };
    std::string ctx = con(e.traducir("asistente", "Paso del guion: %1."), est.value("paso", std::string()));
    if (est.contains("pregunta"))
        ctx += "\n" + con(e.traducir("asistente", "El guion le preguntó (y se lo vuelve a preguntar él, no vos): «%1»"),
                          est["pregunta"].get<std::string>());
    if (est.contains("seConfirma")) ctx += "\n" + con(e.traducir("asistente", "Se está confirmando: %1"), est["seConfirma"].dump());
    if (!est["anotados"].empty()) ctx += "\n" + con(e.traducir("asistente", "Anotado: %1"), est["anotados"].dump());
    if (!est["pendientes"].empty())
        ctx += "\n" + con(e.traducir("asistente", "Sin confirmar todavía: %1"), est["pendientes"].dump());
    if (est.value("corrigiendo", false)) ctx += "\n" + e.traducir("asistente", "Dijo que algún dato está mal: anotá el bueno.");
    if (!est["faltan"].empty()) ctx += "\n" + con(e.traducir("asistente", "Falta: %1"), est["faltan"].dump());
    ctx += "\n\n" + con(e.traducir("asistente", "La persona dijo: «%1»"), dicho);
    mensajes_ = json::array({{{"role", "user"}, {"content", ctx}}});
    json b = cuerpo(mensajes_);
    conHerramientas(b, conv.totem(), idioma_);
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
        conHerramientas(v.cuerpo, conv.totem(), conv.idioma());
        v.otraVuelta = true;
    }
    return v;
}

void AgenteRegistro::anotarIntercambio(const std::string& persona, const std::string& asistente) {
    json intercambio = json::array(
        {{{"role", "user"},
          {"content", ba::i18n::llenar(enIdioma(idioma_).traducir("asistente", "La persona dijo: «%1»"), {persona})}}});
    for (json& m : herramientas_) intercambio.push_back(std::move(m));
    herramientas_ = json::array();
    if (!asistente.empty()) intercambio.push_back({{"role", "assistant"}, {"content", asistente}});
    historia_.push_back(std::move(intercambio));
    while (historia_.size() > kHistoria) historia_.erase(historia_.begin());
}

} // namespace ba::registro
