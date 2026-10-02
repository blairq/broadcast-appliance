// src/app/agent/ArgumentosLocales.cpp — ver ArgumentosLocales.hpp

#include "agent/ArgumentosLocales.hpp"

#include <QFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <regex>

namespace ba::app::agent::argumentos {

namespace {

/// Sin mayúsculas ni acentos ni espacios de los bordes: «Música» == «musica».
std::string normal(const std::string& s) {
    QString q = QString::fromStdString(s).normalized(QString::NormalizationForm_D).toLower().trimmed();
    QString out;
    for (const QChar c : q)
        if (c.category() != QChar::Mark_NonSpacing) out += c;
    return out.toStdString();
}

/// Parecido de 0 a 1 (Levenshtein normalizado).
double parecido(const std::string& a, const std::string& b) {
    const std::u32string x = QString::fromStdString(a).toStdU32String(),
                         y = QString::fromStdString(b).toStdU32String();
    if (x.empty() && y.empty()) return 1.0;
    std::vector<size_t> prev(y.size() + 1), cur(y.size() + 1);
    for (size_t j = 0; j <= y.size(); ++j)
        prev[j] = j;
    for (size_t i = 1; i <= x.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= y.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return 1.0 - static_cast<double>(prev[y.size()]) / static_cast<double>(std::max(x.size(), y.size()));
}

std::vector<std::string> lista(const json& estado, const char* clave, const char* campo = nullptr) {
    std::vector<std::string> out;
    for (const auto& v : estado.value(clave, json::array())) {
        if (campo == nullptr && v.is_string()) out.push_back(v.get<std::string>());
        if (campo != nullptr && v.is_object() && v.value(campo, json()).is_string())
            out.push_back(v.value(campo, std::string()));
    }
    return out;
}

bool empieza(const std::string& s, std::initializer_list<const char*> prefijos) {
    for (const char* p : prefijos)
        if (s.rfind(p, 0) == 0) return true;
    return false;
}

/// Los nombres que puede querer decir, según qué maneja la herramienta (una
/// herramienta de escenas no recibe el nombre de una fuente).
std::vector<std::string> candidatos(const std::string& herr, const std::string& clave, const json& estado) {
    const auto escenas = lista(estado, "escenas");
    const auto fuentes = lista(estado, "fuentes_de_la_escena_actual", "nombre");
    const auto audio = lista(estado, "audio", "nombre");
    const auto gente = lista(estado, "participantes", "nombre");
    const auto graficos = lista(estado, "graficos");
    std::string k = clave;
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    if (k.find("scene") != std::string::npos || k.find("escena") != std::string::npos ||
        (empieza(herr, {"scene_"}) && (k == "name" || k == "to" || k == "from" || k == "nombre")))
        return escenas;
    if (empieza(herr, {"audio", "mixer_"})) return audio;
    if (empieza(herr, {"source_", "capture_source"})) {
        auto v = fuentes;
        v.insert(v.end(), audio.begin(), audio.end());
        return v;
    }
    if (empieza(herr, {"grafico_", "rotulo_"})) return graficos;
    if (empieza(herr, {"laminas_"}) && k == "carrusel") return lista(estado, "carruseles");
    if (empieza(herr, {"participant_", "speaker_", "capture_person"})) return gente;
    std::vector<std::string> todo = escenas;
    for (const auto* l : {&fuentes, &audio, &gente, &graficos})
        todo.insert(todo.end(), l->begin(), l->end());
    return todo;
}

std::string escaparRegex(const std::string& s) {
    static const std::regex especiales(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(s, especiales, R"(\$&)");
}

bool esClaveDeNombre(const std::string& k) {
    static const char* claves[] = {"name",   "source", "scene",       "nombre",    "fuente",
                                   "escena", "target", "grafico",     "lamina",    "to",
                                   "from",   "id",     "participant", "sceneName", "sourceName"};
    for (const char* c : claves)
        if (k == c) return true;
    return k.size() > 4 && k.compare(k.size() - 4, 4, "Name") == 0;
}

/// El nombre que existe (y una nota si hubo que ajustarlo), o vacío.
std::string ajustar(const std::string& v, const std::vector<std::string>& cand, std::string& nota) {
    if (std::find(cand.begin(), cand.end(), v) != cand.end()) return v;
    const std::string nv = normal(v);
    for (const auto& c : cand)
        if (normal(c) == nv) {
            nota = "nombre normalizado: " + v + " -> " + c;
            return c;
        }
    std::vector<std::string> contiene;
    for (const auto& c : cand) {
        const std::string nc = normal(c);
        if (!nv.empty() && (nc.find(nv) != std::string::npos || nv.find(nc) != std::string::npos))
            contiene.push_back(c);
    }
    if (contiene.size() == 1) {
        nota = "nombre completado: " + v + " -> " + contiene[0];
        return contiene[0];
    }
    std::string mejor;
    double pm = 0.0;
    for (const auto& c : cand)
        if (const double pr = parecido(nv, normal(c)); pr > pm) {
            pm = pr;
            mejor = c;
        }
    if (pm >= 0.8) {
        nota = "nombre parecido: " + v + " -> " + mejor;
        return mejor;
    }
    return {};
}

} // namespace

std::string sistemaPorDefecto() {
    QFile f(":/voz/voz-argumentos.txt");
    if (f.open(QIODevice::ReadOnly)) return f.readAll().toStdString();
    return "Completás los argumentos de UNA herramienta. Respondé SOLO un objeto JSON en una línea.";
}

json estadoPara(const std::string& h, const json& estado) {
    const auto con = [&](std::initializer_list<const char*> claves) {
        json out = json::object();
        for (const char* k : claves)
            if (estado.contains(k)) out[k] = estado[k];
        return out;
    };
    if (empieza(h, {"scene_", "transition_"})) return con({"escena_actual", "escena_armada", "escenas"});
    if (empieza(h, {"audio", "mixer_"})) return con({"audio"});
    if (empieza(h, {"source_", "capture_source"}))
        return con({"escena_actual", "escenas", "fuentes_de_la_escena_actual", "audio"});
    if (empieza(h, {"participant_", "speaker_", "chat_", "capture_person"})) return con({"participantes"});
    if (empieza(h, {"grafico_", "rotulo_"})) return con({"graficos", "escena_actual"});
    if (empieza(h, {"laminas_"})) return con({"carruseles", "escenas", "escena_actual"});
    return estado;
}

std::string prompt(const std::string& sistema, const std::string& herramienta, const json& ficha,
                   const json& estadoCompleto, const std::string& frase) {
    const json estado = estadoPara(herramienta, estadoCompleto);
    std::string desc = ficha.value("description", std::string());
    if (desc.size() > 700) desc = desc.substr(0, 700);
    const std::string usuario =
        "Herramienta: " + herramienta + " — " + ficha.value("title", std::string()) + "\n" + desc +
        "\nEsquema de argumentos: " + ficha.value("inputSchema", json::object()).dump() +
        "\n\nEstado del aparato: " + estado.dump() + "\n\nPedido del operador: «" + frase + "»";
    return "<|im_start|>system\n" + sistema + "<|im_end|>\n<|im_start|>user\n" + usuario +
           "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n{";
}

namespace {

/// El primer objeto balanceado de `s` (el modelo a veces sigue escribiendo).
std::string primerObjeto(const std::string& s) {
    int nivel = 0;
    bool enTexto = false, escape = false;
    size_t inicio = std::string::npos;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (enTexto) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                enTexto = false;
            continue;
        }
        if (c == '"') enTexto = true;
        if (c == '{' && nivel++ == 0) inicio = i;
        if (c == '}' && nivel > 0 && --nivel == 0) return s.substr(inicio, i - inicio + 1);
    }
    return {};
}

} // namespace

json leer(const std::string& generado, const std::string& herramienta) {
    const std::string t = primerObjeto("{" + generado);
    if (t.empty()) return json();
    // Solo el nombre de la herramienta («{capture_program}»): sin argumentos.
    if (!herramienta.empty()) {
        const std::regex soloNombre("\\{\\s*" + herramienta + "[^{}\"]*\\}");
        if (std::regex_match(t, soloNombre)) return json::object();
    }
    // «{capture_ui, format: "jpeg"}»: el nombre de la herramienta y después las claves.
    std::string base = t;
    if (!herramienta.empty()) {
        const std::regex nombreAdelante("^\\{\\s*" + herramienta + "\\s*,");
        base = std::regex_replace(t, nombreAdelante, "{");
    }
    // Tal cual, o con las claves sin comillas entre comillas.
    static const std::regex sinComillas(R"(([{,]\s*)([A-Za-z_]\w*)\s*:)");
    for (const std::string& intento : {base, std::regex_replace(base, sinComillas, "$1\"$2\":")}) {
        json j = json::parse(intento, nullptr, false);
        if (!j.is_object()) continue;
        // Envuelto en el nombre de la herramienta.
        if (j.size() == 1 && !herramienta.empty() && j.contains(herramienta))
            j = j[herramienta].is_object() ? j[herramienta] : json::object();
        return j;
    }
    return json();
}

Resultado validar(const std::string& herr, const json& ficha, const json& args0, const json& estado) {
    Resultado r;
    const json esquema = ficha.value("inputSchema", json::object());
    const json props = esquema.value("properties", json::object());
    // Envuelto en una sola clave que no es del esquema ({"properties": {"name": …}}).
    json args = args0;
    if (args.is_object() && args.size() == 1 && !props.contains(args.begin().key()) &&
        args.begin().value().is_object()) {
        r.notas.push_back("desenvuelto de «" + args.begin().key() + "»");
        args = json(args.begin().value());
    }
    // Lo que el modelo copia en vez de un dato: un texto con forma de
    // herramienta («scene_take», «grafico_en_escena») o un comodín de la
    // descripción («guest:N»).
    static const std::regex formaDeHerramienta("^[a-z]+(_[a-z]+)+$");
    static const std::regex comodin(".*:[A-Z]$");
    std::map<std::string, double> volumen;
    for (const auto& a : estado.value("audio", json::array()))
        if (a.is_object()) volumen[a.value("nombre", std::string())] = a.value("volumen_db", 0.0);
    static const std::regex relativo(R"(\s*[+-]\d+(\.\d+)?\s*)");

    for (auto it = args.begin(); it != args.end(); ++it) {
        const std::string k = it.key();
        json v = it.value();
        if (!props.contains(k)) {
            r.notas.push_back("clave de más: " + k);
            continue;
        }
        const json& p = props[k];
        const std::string tipo = p.value("type", std::string());
        if (v.is_string() && v.get<std::string>().empty()) {
            r.notas.push_back("vacío: afuera " + k);
            continue;
        }
        if (v.is_string() &&
            (v.get<std::string>() == herr ||
             (std::regex_match(v.get<std::string>(), formaDeHerramienta) && !p.contains("enum")) ||
             std::regex_match(v.get<std::string>(), comodin))) {
            r.notas.push_back("«" + v.get<std::string>() + "» no es un dato: afuera " + k);
            continue;
        }

        // Un valor que la descripción del argumento nombra ("all", "operator"…).
        const std::string desc = p.value("description", std::string());
        const bool especial =
            v.is_string() && !v.get<std::string>().empty() &&
            std::regex_search(desc,
                              std::regex("(^|[^\\w])" + escaparRegex(v.get<std::string>()) + "([^\\w]|$)"));
        // Lo que se crea todavía no existe: su nombre es el que dijo el operador.
        static const std::regex crea("_(create|add|copy|rename|folder|define)$");
        const bool nuevo = std::regex_search(herr, crea) && (k == "name" || k == "newName" || k == "nuevo" ||
                                                             k == "nombre" || k == "to" || k == "folder");
        if (tipo == "string" && v.is_string() && !p.contains("enum") && !especial && !nuevo &&
            esClaveDeNombre(k)) {
            std::string nota;
            const std::string n = ajustar(v.get<std::string>(), candidatos(herr, k, estado), nota);
            if (n.empty()) {
                r.bloqueos.push_back("no existe: " + v.get<std::string>());
                r.args[k] = v;
                continue;
            }
            if (!nota.empty()) r.notas.push_back(nota);
            v = n;
        }
        if ((tipo == "number" || tipo == "integer") && v.is_string() &&
            std::regex_match(v.get<std::string>(), relativo)) {
            const double d = std::stod(v.get<std::string>());
            const std::string fuente = r.args.value(
                "name", args.value("name", r.args.value("source", args.value("source", std::string()))));
            std::string kl = k;
            std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
            if (kl.rfind("volume", 0) == 0 && volumen.count(fuente) > 0) {
                v = std::round((volumen[fuente] + d) * 10.0) / 10.0;
                r.notas.push_back("relativo " + std::string(d > 0 ? "+" : "") + json(d).dump() +
                                  " dB sobre " + json(volumen[fuente]).dump());
            } else {
                // Sin valor actual que conozcamos: el número con su signo (un
                // corrimiento, como el de sincronía, es así).
                r.notas.push_back("con signo, sin valor actual: " + k + "=" + v.get<std::string>());
                v = d;
            }
        }
        if (p.contains("enum") && std::find(p["enum"].begin(), p["enum"].end(), v) == p["enum"].end())
            r.bloqueos.push_back("valor fuera de los permitidos: " + k + "=" + v.dump());
        if (p.contains("minimum") && v.is_number() && v.get<double>() < p["minimum"].get<double>())
            v = p["minimum"];
        if (p.contains("maximum") && v.is_number() && v.get<double>() > p["maximum"].get<double>())
            v = p["maximum"];
        r.args[k] = v;
    }
    for (const auto& req : esquema.value("required", json::array()))
        if (req.is_string() && !r.args.contains(req.get<std::string>()))
            r.bloqueos.push_back("falta: " + req.get<std::string>());
    return r;
}

LlamadaTc leerTc(const std::string& generado) {
    LlamadaTc l;
    static const std::regex llamada(R"(<tool_call>\s*<function=([^>\n]+)>([\s\S]*?)</function>\s*</tool_call>)");
    static const std::regex parametro(R"(<parameter=([^>\n]+)>\n?([\s\S]*?)\n?</parameter>)");
    std::smatch m;
    if (!std::regex_search(generado, m, llamada)) return l;
    l.hay = true;
    l.funcion = m[1].str();
    const std::string cuerpo = m[2].str();
    for (std::sregex_iterator it(cuerpo.begin(), cuerpo.end(), parametro), fin; it != fin; ++it)
        l.parametros.emplace_back((*it)[1].str(), (*it)[2].str());
    return l;
}

json argsDesdeTc(const LlamadaTc& llamada, const json& ficha) {
    const json props = ficha.value("inputSchema", json::object()).value("properties", json::object());
    static const std::regex numero(R"(\s*-?\d+(\.\d+)?\s*)");
    static const std::regex conSigno(R"(\s*[+-]\d+(\.\d+)?\s*)");
    json args = json::object();
    for (const auto& [k, crudo] : llamada.parametros) {
        std::string v = crudo;
        // Sin los blancos de los bordes (el formato pone saltos de línea).
        const auto a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
        v = a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
        const json p = props.value(k, json::object());
        const std::string tipo = p.value("type", std::string());
        if (tipo == "number" || tipo == "integer") {
            if (!v.empty() && v[0] == '=' && std::regex_match(v.substr(1), numero)) {
                const double x = std::stod(v.substr(1));
                args[k] = tipo == "integer" ? json(static_cast<long long>(std::llround(x))) : json(x);
            } else if (std::regex_match(v, conSigno)) {
                args[k] = v; // cambio: validar lo suma al valor actual
            } else if (std::regex_match(v, numero)) {
                const double x = std::stod(v);
                args[k] = tipo == "integer" ? json(static_cast<long long>(std::llround(x))) : json(x);
            } else {
                args[k] = v;
            }
        } else if (tipo == "boolean") {
            args[k] = v == "true" || v == "verdadero" || v == "sí" || v == "si" || v == "1";
        } else if (tipo == "array" || tipo == "object") {
            const json j = json::parse(v, nullptr, false);
            if (!j.is_discarded()) args[k] = j;
            else if (tipo == "array") args[k] = json::array({v});
            else args[k] = v;
        } else {
            // Texto (y enums): tal cual; un «=» adelante de un texto no significa nada.
            args[k] = v;
        }
    }
    return args;
}

std::string resumen(const json& args) {
    std::string out;
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!out.empty()) out += " · ";
        out += it.key() + ": " + (it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
    }
    return out;
}

} // namespace ba::app::agent::argumentos
