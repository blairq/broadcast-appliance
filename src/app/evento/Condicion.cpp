// ============================================================================
// src/app/evento/Condicion.cpp — ver Condicion.hpp
// ============================================================================
#include "evento/Condicion.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>

namespace ba::evento::linea {

using nlohmann::json;

namespace {

constexpr int64_t kDiaMs = 24LL * 3600 * 1000;

bool empieza(const std::string& s, const char* pre) { return s.rfind(pre, 0) == 0; }

/// ¿Es (o empieza a ser) un pulso? Ahí el «+» es de la tecla («tecla:Ctrl+F5»), no la
/// suma de tiempo (que solo va después de «pieza:<id>.fin»).
bool esPulso(const std::string& w) { return empieza(w, "tecla:") || empieza(w, "botonera:") || empieza(w, "web:"); }

// --- Leer -------------------------------------------------------------------------------

/// Separa en palabras: los operadores y los paréntesis sueltos, lo citado entero.
std::vector<std::string> palabras(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    const auto cortar = [&]() {
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            // Lo citado va entero, con sus comillas (así se sabe que es texto).
            const std::size_t fin = s.find(c, i + 1);
            const std::size_t hasta = fin == std::string::npos ? s.size() : fin + 1;
            cur += s.substr(i, hasta - i);
            i = hasta - 1;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            cortar();
            continue;
        }
        const std::string dos = s.substr(i, 2);
        if (dos == ">=" || dos == "<=" || dos == "==" || dos == "!=" || dos == "&&" || dos == "||") {
            cortar();
            out.push_back(dos);
            ++i;
            continue;
        }
        if (c == '+' && esPulso(cur)) {
            cur += c;
            continue;
        }
        if (c == '>' || c == '<' || c == '(' || c == ')' || c == '!' || c == '+') {
            cortar();
            out.push_back(std::string(1, c));
            continue;
        }
        cur += c;
    }
    cortar();
    return out;
}

bool esOperador(const std::string& t) { return t == "==" || t == "!=" || t == ">=" || t == "<=" || t == ">" || t == "<"; }

/// "HH:MM" o "H:MM" → minutos; -1 si no es una hora.
int minutosDe(const std::string& t) {
    const std::size_t dp = t.find(':');
    if (dp == std::string::npos || dp == 0 || dp > 2 || t.size() != dp + 3) return -1;
    for (std::size_t i = 0; i < t.size(); ++i)
        if (i != dp && !std::isdigit(static_cast<unsigned char>(t[i]))) return -1;
    const int h = std::stoi(t.substr(0, dp)), m = std::stoi(t.substr(dp + 1));
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

/// "2s", "500ms", "1m" → ms; -1 si no.
int64_t duracionDe(const std::string& t) {
    std::size_t i = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i])))
        ++i;
    if (i == 0) return -1;
    const int64_t n = std::stoll(t.substr(0, i));
    const std::string u = t.substr(i);
    if (u == "ms") return n;
    if (u == "s" || u.empty()) return n * 1000;
    if (u == "m" || u == "min") return n * 60000;
    return -1;
}

/// Lo de la derecha de una comparación: 'texto', un número, true/false o una palabra.
json valorDe(const std::string& t) {
    if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) return t.substr(1, t.size() - 2);
    if (t == "true" || t == "verdadero") return true;
    if (t == "false" || t == "falso") return false;
    char* fin = nullptr;
    const double d = std::strtod(t.c_str(), &fin);
    if (fin != nullptr && *fin == '\0' && !t.empty()) return d;
    return t;
}

struct Atomo {
    enum class Clase { Bus, Medio, Hora, Pieza, Pulso };
    Clase clase = Clase::Pulso;
    std::string ref;                 ///< el tema, la pieza o la clave del pulso
    std::vector<std::string> campos; ///< bus: lo que se mira adentro del valor
    std::string op;                  ///< vacío = «es verdadero»
    json valor;
    int minutos = 0; ///< hora
    int64_t masMs = 0; ///< pieza: «+ 2s»
    std::string clave; ///< pulso: con qué se compara (clavePulso de ref)
};

struct Nodo {
    enum class Tipo { Y, O, No, Hoja };
    Tipo tipo = Tipo::Hoja;
    std::vector<Nodo> hijos;
    Atomo atomo;
};

class Lector {
   public:
    explicit Lector(const std::string& s) : t_(palabras(s)) {}

    bool leer(Nodo& out, std::string& error) {
        if (t_.empty()) {
            error = ba::i18n::traducir("linea", "la condición está vacía");
            return false;
        }
        if (!expr(out, error)) return false;
        if (i_ < t_.size()) {
            error = ba::i18n::llenar(ba::i18n::traducir("linea", "no entiendo «%1» en la condición"), {t_[i_]});
            return false;
        }
        return true;
    }

   private:
    bool hay(const char* a, const char* b = nullptr) const { return i_ < t_.size() && (t_[i_] == a || (b && t_[i_] == b)); }

    bool expr(Nodo& out, std::string& e) {
        Nodo a;
        if (!term(a, e)) return false;
        if (!hay("o", "||")) {
            out = std::move(a);
            return true;
        }
        out = Nodo{Nodo::Tipo::O, {std::move(a)}, {}};
        while (hay("o", "||")) {
            ++i_;
            Nodo b;
            if (!term(b, e)) return false;
            out.hijos.push_back(std::move(b));
        }
        return true;
    }

    bool term(Nodo& out, std::string& e) {
        Nodo a;
        if (!factor(a, e)) return false;
        if (!hay("y", "&&")) {
            out = std::move(a);
            return true;
        }
        out = Nodo{Nodo::Tipo::Y, {std::move(a)}, {}};
        while (hay("y", "&&")) {
            ++i_;
            Nodo b;
            if (!factor(b, e)) return false;
            out.hijos.push_back(std::move(b));
        }
        return true;
    }

    bool factor(Nodo& out, std::string& e) {
        if (hay("no", "!")) {
            ++i_;
            Nodo a;
            if (!factor(a, e)) return false;
            out = Nodo{Nodo::Tipo::No, {std::move(a)}, {}};
            return true;
        }
        if (hay("(")) {
            ++i_;
            if (!expr(out, e)) return false;
            if (!hay(")")) {
                e = ba::i18n::traducir("linea", "falta cerrar un paréntesis en la condición");
                return false;
            }
            ++i_;
            return true;
        }
        return atomo(out, e);
    }

    bool atomo(Nodo& out, std::string& e) {
        if (i_ >= t_.size()) {
            e = ba::i18n::traducir("linea", "la condición termina a la mitad");
            return false;
        }
        const std::string w = t_[i_++];
        Atomo a;
        const auto comparacion = [&]() -> bool {
            if (i_ < t_.size() && esOperador(t_[i_])) {
                a.op = t_[i_++];
                if (i_ >= t_.size()) {
                    e = ba::i18n::llenar(ba::i18n::traducir("linea", "falta el valor después de «%1»"), {a.op});
                    return false;
                }
                a.valor = valorDe(t_[i_++]);
            }
            return true;
        };
        if (empieza(w, "bus:") && w.size() > 4) {
            a.clase = Atomo::Clase::Bus;
            std::string resto = w.substr(4);
            const std::size_t punto = resto.find('.');
            a.ref = resto.substr(0, punto);
            for (std::size_t p = punto; p != std::string::npos;) {
                const std::size_t q = resto.find('.', p + 1);
                a.campos.push_back(resto.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1));
                p = q;
            }
            if (!comparacion()) return false;
        } else if (empieza(w, "medio:") && w.size() > 14 && w.substr(w.size() - 8) == ".termino") {
            a.clase = Atomo::Clase::Medio;
            a.ref = w.substr(6, w.size() - 14);
        } else if (w == "hora") {
            a.clase = Atomo::Clase::Hora;
            if (i_ + 1 >= t_.size() || !esOperador(t_[i_])) {
                e = ba::i18n::traducir("linea", "la hora va con una comparación: «hora>=19:30»");
                return false;
            }
            a.op = t_[i_++];
            a.minutos = minutosDe(t_[i_++]);
            if (a.minutos < 0) {
                e = ba::i18n::llenar(ba::i18n::traducir("linea", "«%1» no es una hora (HH:MM)"), {t_[i_ - 1]});
                return false;
            }
        } else if (empieza(w, "pieza:") && w.size() > 10 && w.substr(w.size() - 4) == ".fin") {
            a.clase = Atomo::Clase::Pieza;
            a.ref = w.substr(6, w.size() - 10);
            if (hay("+")) {
                ++i_;
                a.masMs = i_ < t_.size() ? duracionDe(t_[i_++]) : -1;
                if (a.masMs < 0) {
                    e = ba::i18n::traducir("linea", "después de «+» va un tiempo: «+ 2s» o «+ 500ms»");
                    return false;
                }
            }
        } else if ((empieza(w, "tecla:") && w.size() > 6) || (empieza(w, "botonera:") && w.size() > 9) ||
                   (empieza(w, "web:") && w.size() > 4)) {
            a.clase = Atomo::Clase::Pulso;
            a.ref = w;
            a.clave = clavePulso(w);
        } else {
            e = ba::i18n::llenar(ba::i18n::traducir("linea", "«%1» no es una condición que conozca (bus:, medio:, hora, "
                                                              "pieza:, tecla:, botonera:, web:)"),
                                 {w});
            return false;
        }
        out = Nodo{Nodo::Tipo::Hoja, {}, std::move(a)};
        return true;
    }

    std::vector<std::string> t_;
    std::size_t i_ = 0;
};

// --- Evaluar ------------------------------------------------------------------------------

bool verdadero(const json& v) {
    if (v.is_null()) return false;
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0;
    if (v.is_string()) return !v.get<std::string>().empty();
    return !v.empty();
}

template <typename T>
bool comparar(const T& a, const std::string& op, const T& b) {
    if (op == "==") return a == b;
    if (op == "!=") return a != b;
    if (op == ">=") return a >= b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    return a < b;
}

bool compararValor(const json& izq, const std::string& op, const json& der) {
    if (der.is_number()) {
        double x = 0;
        if (izq.is_number()) x = izq.get<double>();
        else if (izq.is_boolean()) x = izq.get<bool>() ? 1 : 0;
        else if (izq.is_string()) {
            char* fin = nullptr;
            const std::string s = izq.get<std::string>();
            x = std::strtod(s.c_str(), &fin);
            if (s.empty() || fin == nullptr || *fin != '\0') return op == "!=";
        } else {
            return op == "!=";
        }
        return comparar(x, op, der.get<double>());
    }
    if (der.is_boolean()) {
        const bool b = verdadero(izq) == der.get<bool>();
        return op == "==" ? b : op == "!=" ? !b : false;
    }
    const std::string d = der.is_string() ? der.get<std::string>() : der.dump();
    const std::string i = izq.is_string() ? izq.get<std::string>() : izq.is_null() ? std::string() : izq.dump();
    return comparar(i, op, d);
}

Evaluacion evaluarAtomo(const Atomo& a, const Mundo& m) {
    Evaluacion ev;
    switch (a.clase) {
        case Atomo::Clase::Bus: {
            const auto it = m.bus.find(a.ref);
            json v = it == m.bus.end() ? json() : it->second;
            for (const std::string& c : a.campos)
                v = v.is_object() && v.contains(c) ? json(v[c]) : json();
            ev.cumple = a.op.empty() ? verdadero(v) : compararValor(v, a.op, a.valor);
            return ev;
        }
        case Atomo::Clase::Medio: {
            const auto it = m.mediosTerminados.find(a.ref);
            ev.cumple = it != m.mediosTerminados.end();
            if (ev.cumple) ev.desdeMs = it->second;
            return ev;
        }
        case Atomo::Clase::Hora: {
            int64_t h = m.medianocheMs + int64_t{a.minutos} * 60000;
            if (m.inicioPlaneadoMs > 0 && h < m.inicioPlaneadoMs) h += kDiaMs;
            // Dónde cambia: «==» vale el minuto entero.
            int64_t desde = h, hasta = kNunca;
            if (a.op == ">" || a.op == "<=") desde = h + 60000;
            if (a.op == "==" || a.op == "!=") hasta = h + 60000;
            const bool adentro = m.ahoraMs >= desde && m.ahoraMs < hasta;
            const bool invertida = a.op == "<" || a.op == "<=" || a.op == "!=";
            ev.cumple = invertida ? !adentro : adentro;
            if (m.ahoraMs < desde) {
                ev.cambiaMs = desde;
            } else if (m.ahoraMs < hasta) {
                ev.desdeMs = desde;
                ev.cambiaMs = hasta;
            } else {
                ev.desdeMs = hasta;
            }
            return ev;
        }
        case Atomo::Clase::Pieza: {
            const auto it = m.finesDePieza.find(a.ref);
            if (it == m.finesDePieza.end()) return ev;
            const int64_t t = it->second + a.masMs;
            ev.cumple = m.ahoraMs >= t;
            if (ev.cumple) ev.desdeMs = t;
            else ev.cambiaMs = t;
            return ev;
        }
        case Atomo::Clase::Pulso:
            ev.cumple = std::any_of(m.pulsos.begin(), m.pulsos.end(), [&](const std::string& p) { return clavePulso(p) == a.clave; });
            if (ev.cumple) ev.desdeMs = m.ahoraMs;
            return ev;
    }
    return ev;
}

Evaluacion evaluarNodo(const Nodo& n, const Mundo& m) {
    if (n.tipo == Nodo::Tipo::Hoja) return evaluarAtomo(n.atomo, m);
    if (n.tipo == Nodo::Tipo::No) {
        Evaluacion ev = evaluarNodo(n.hijos.front(), m);
        ev.cumple = !ev.cumple;
        return ev;
    }
    // y / o: el resultado y desde cuándo (si lo saben todos los que deciden).
    const bool esY = n.tipo == Nodo::Tipo::Y;
    std::vector<Evaluacion> hs;
    Evaluacion ev;
    ev.cumple = esY;
    for (const Nodo& h : n.hijos) {
        hs.push_back(evaluarNodo(h, m));
        ev.cambiaMs = std::min(ev.cambiaMs, hs.back().cambiaMs);
        ev.cumple = esY ? ev.cumple && hs.back().cumple : ev.cumple || hs.back().cumple;
    }
    // Un «y» que se cumple arrancó con el último que se cumplió; un «o», con el primero.
    // Que no se cumple: al revés.
    const bool tomarMax = esY == ev.cumple;
    for (const Evaluacion& h : hs) {
        if (h.cumple != ev.cumple) continue;
        if (h.desdeMs < 0) return Evaluacion{ev.cumple, -1, ev.cambiaMs};
        ev.desdeMs = ev.desdeMs < 0 ? h.desdeMs : tomarMax ? std::max(ev.desdeMs, h.desdeMs) : std::min(ev.desdeMs, h.desdeMs);
    }
    return ev;
}

/// Las referencias de una clase de átomo (los temas del bus, los pulsos), sin repetir.
void refsDeNodo(const Nodo& n, Atomo::Clase clase, std::vector<std::string>& out) {
    if (n.tipo == Nodo::Tipo::Hoja) {
        if (n.atomo.clase == clase && std::find(out.begin(), out.end(), n.atomo.ref) == out.end()) out.push_back(n.atomo.ref);
        return;
    }
    for (const Nodo& h : n.hijos)
        refsDeNodo(h, clase, out);
}

/// Leída una sola vez por texto (se evalúa en cada tick). Nunca se borra (las
/// condiciones de un evento son unas pocas): así el nodo devuelto no se mueve, y el
/// candado es por si pregunta otro hilo (el MCP).
const Nodo* leida(const Condicion& c) {
    static std::mutex candado;
    static std::map<std::string, std::unique_ptr<Nodo>> cache;
    const std::lock_guard<std::mutex> l(candado);
    auto it = cache.find(c);
    if (it == cache.end()) {
        auto n = std::make_unique<Nodo>();
        std::string e;
        if (!Lector(c).leer(*n, e)) n.reset();
        it = cache.emplace(c, std::move(n)).first;
    }
    return it->second.get();
}

} // namespace

Evaluacion evaluar(const Condicion& c, const Mundo& m) {
    const Nodo* n = leida(c);
    return n ? evaluarNodo(*n, m) : Evaluacion{};
}

std::string errorDeCondicion(const Condicion& c) {
    Nodo n;
    std::string e;
    Lector(c).leer(n, e);
    return e;
}

std::vector<std::string> temasDe(const Condicion& c) {
    std::vector<std::string> out;
    if (const Nodo* n = leida(c)) refsDeNodo(*n, Atomo::Clase::Bus, out);
    return out;
}

std::vector<std::string> pulsosDe(const Linea& l) {
    std::vector<std::string> todos;
    for (const Condicion& c : condicionesDe(l))
        if (const Nodo* n = leida(c)) refsDeNodo(*n, Atomo::Clase::Pulso, todos);
    // La misma tecla escrita de dos maneras («ctrl+f5» y «Ctrl+F5») es un solo pulso.
    std::vector<std::string> out;
    std::set<std::string> claves;
    for (std::string& p : todos)
        if (claves.insert(clavePulso(p)).second) out.push_back(std::move(p));
    return out;
}

std::string clavePulso(const std::string& pulso) {
    if (!empieza(pulso, "tecla:")) return pulso;
    std::string t;
    for (const char c : pulso.substr(6))
        t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // La tecla es lo último; un «+» al final que sigue a otro (o solo) es la tecla «+».
    std::string tecla;
    if (t == "+" || (t.size() >= 2 && t.compare(t.size() - 2, 2, "++") == 0)) {
        tecla = "+";
        t.resize(t.size() >= 2 ? t.size() - 2 : 0);
    } else {
        const std::size_t u = t.rfind('+');
        tecla = u == std::string::npos ? t : t.substr(u + 1);
        t.resize(u == std::string::npos ? 0 : u);
    }
    // Los modificadores, como conjunto y con un solo nombre cada uno.
    std::set<std::string> mods;
    for (std::size_t i = 0; i < t.size();) {
        const std::size_t j = std::min(t.find('+', i), t.size());
        std::string m = t.substr(i, j - i);
        if (m == "control") m = "ctrl";
        else if (m == "cmd" || m == "command" || m == "meta" || m == "win") m = "meta";
        else if (m == "option" || m == "opt") m = "alt";
        if (!m.empty()) mods.insert(m);
        i = j + 1;
    }
    std::string out = "tecla:";
    for (const std::string& m : mods)
        out += m + "+";
    return out + tecla;
}

std::set<Condicion> condicionesDe(const Linea& l) {
    std::set<Condicion> out;
    const auto deInicio = [&](const Inicio& i) {
        if (i.modo == ModoInicio::Cuando && !i.condicion.empty()) out.insert(i.condicion);
    };
    for (const Bloque& b : l.bloques) {
        if (b.inicio) deInicio(*b.inicio);
        for (const Paso& p : b.pasos) {
            deInicio(p.inicio);
            if (p.fin.modo == ModoFin::Mientras && !p.fin.mientras.empty()) out.insert(p.fin.mientras);
            for (const Pieza& z : p.piezas) {
                if (z.desde.modo == Ancla::Modo::Cuando && !z.desde.cuando.empty()) out.insert(z.desde.cuando);
                // Lo que tiene medio: el resolvedor pregunta por su fin con este nombre.
                if (z.tipo == "video" || z.tipo == "musica" || z.tipo == "medios" || z.tipo == "playlist" ||
                    z.dura.modo == Dura::Modo::AlTerminar)
                    out.insert("medio:" + z.id + ".termino");
            }
        }
    }
    for (const Disparador& d : l.disparadores)
        if (!d.cuando.empty()) out.insert(d.cuando);
    return out;
}

// --- Seguimiento ----------------------------------------------------------------------------

bool Seguimiento::actualizar(const std::set<Condicion>& conds, const Mundo& m) {
    bool cambio = false;
    proximoMs_ = kNunca;
    for (auto it = estados_.begin(); it != estados_.end();)
        it = conds.count(it->first) ? std::next(it) : estados_.erase(it);
    for (const Condicion& c : conds) {
        const Evaluacion ev = evaluar(c, m);
        proximoMs_ = std::min(proximoMs_, ev.cambiaMs > m.ahoraMs ? ev.cambiaMs : kNunca);
        const int64_t desde = ev.desdeMs >= 0 ? ev.desdeMs : m.ahoraMs;
        auto it = estados_.find(c);
        if (it == estados_.end()) {
            // La primera vez, sin hora exacta: vale así «desde siempre» (0). Si no, un
            // «mientras» que todavía no empezó a cumplirse terminaría al entrar.
            estados_[c] = {ev.cumple, ev.desdeMs >= 0 ? ev.desdeMs : 0};
            cambio = cambio || ev.cumple;
            continue;
        }
        // Cambió, o sigue igual pero se sabe que volvió a pasar (el medio terminó otra vez).
        if (it->second.cumple != ev.cumple || (ev.desdeMs >= 0 && ev.desdeMs != it->second.desdeMs)) {
            it->second = {ev.cumple, desde};
            cambio = true;
        }
    }
    return cambio;
}

bool Seguimiento::cumple(const Condicion& c) const {
    const auto it = estados_.find(c);
    return it != estados_.end() && it->second.cumple;
}

} // namespace ba::evento::linea
