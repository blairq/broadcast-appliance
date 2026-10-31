// src/app/emision/SalidasConfig.cpp — las salidas guardadas y su aire (ver el .hpp)

#include "emision/SalidasConfig.hpp"

#include "i18n/I18n.hpp"
#include "ipc/CanalesSalida.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace ba::app::salidas {

using json = nlohmann::json;

namespace {
bool colorValido(const std::string& c) {
    if (c.size() != 7 || c[0] != '#') return false;
    return std::all_of(c.begin() + 1, c.end(), [](char x) { return std::isxdigit(static_cast<unsigned char>(x)) != 0; });
}
} // namespace

const Salida* Lista::buscar(const std::string& id) const {
    const std::string& b = id.empty() ? std::string(kPorOmision) : id;
    for (const auto& s : salidas)
        if (s.id == b) return &s;
    return nullptr;
}

std::string colorDeFabrica(int n) {
    // La por omisión en rojo (el tally de siempre); las demás, fáciles de distinguir entre sí.
    static const char* kColores[] = {"#E53935", "#1E88E5", "#43A047", "#FB8C00", "#8E24AA", "#00ACC1", "#FDD835", "#6D4C41"};
    return kColores[static_cast<std::size_t>(std::max(0, n)) % (sizeof kColores / sizeof *kColores)];
}

std::string validarId(const std::string& id) {
    // La regla es la de los canales del gateway (ipc/CanalesSalida.hpp): acá, el porqué.
    if (ba::ipc::idDeSalidaValido(id)) return {};
    if (id.empty()) return ba::i18n::traducir("salidas", "falta el id de la salida");
    if (id.size() > ba::ipc::kLargoIdSalida) return ba::i18n::traducir("salidas", "el id de la salida es muy largo (hasta 32)");
    return ba::i18n::traducir("salidas", "el id de la salida va con letras, números, «-» y «_»");
}

Lista leer(const json& j) {
    Lista l;
    if (j.is_object()) {
        l.tope = std::clamp(j.value("tope", kTopeDeFabrica), 1, kTopeMaximo);
        const json lista = j.value("salidas", json::array()); // con nombre: iterar un temporal es UB
        for (const auto& x : lista) {
            if (!x.is_object()) continue;
            Salida s;
            s.id = x.value("id", std::string());
            if (!validarId(s.id).empty() || l.buscar(s.id) != nullptr) continue; // rota o repetida: afuera
            s.nombre = x.value("nombre", s.id);
            s.color = x.value("color", std::string());
            s.orden = x.value("orden", static_cast<int>(l.salidas.size()));
            if (s.id != kPorOmision) {
                s.codificador = x.value("codificador", std::string());
                const json mezcla = x.value("mezcla", json::object());
                if (mezcla.is_object())
                    for (const auto& [f, m] : mezcla.items())
                        if (m.is_object() && m.value("volumen", json()).is_number())
                            s.mezcla[f] = {std::clamp(m["volumen"].get<double>(), 0.0, 4.0), m.value("mute", false)};
                const json grupos = x.value("grupos", json::object());
                if (grupos.is_object())
                    for (const auto& [g, v] : grupos.items())
                        if (v.is_number()) s.grupos[g] = std::clamp(v.get<double>(), 0.0, 4.0);
            }
            l.salidas.push_back(std::move(s));
        }
    }
    // La por omisión existe siempre (un archivo viejo o borrado a mano no la saca).
    if (l.buscar(kPorOmision) == nullptr) {
        Salida p{kPorOmision, kPorOmision, {}, -1, {}, {}};
        l.salidas.insert(l.salidas.begin(), std::move(p));
    }
    std::stable_sort(l.salidas.begin(), l.salidas.end(), [](const Salida& a, const Salida& b) {
        // La por omisión primera: es la que ven todos los que no eligen.
        if ((a.id == kPorOmision) != (b.id == kPorOmision)) return a.id == kPorOmision;
        return a.orden < b.orden;
    });
    for (std::size_t i = 0; i < l.salidas.size(); ++i) {
        l.salidas[i].orden = static_cast<int>(i);
        if (!colorValido(l.salidas[i].color)) l.salidas[i].color = colorDeFabrica(static_cast<int>(i));
        if (l.salidas[i].nombre.empty()) l.salidas[i].nombre = l.salidas[i].id;
    }
    return l;
}

json aJson(const Lista& l) {
    json arr = json::array();
    for (const auto& s : l.salidas) {
        json x{{"id", s.id}, {"nombre", s.nombre}, {"color", s.color}, {"orden", s.orden}};
        if (!s.codificador.empty()) x["codificador"] = s.codificador;
        if (!s.mezcla.empty()) {
            json m = json::object();
            for (const auto& [f, k] : s.mezcla) m[f] = {{"volumen", k.volumen}, {"mute", k.mute}};
            x["mezcla"] = m;
        }
        if (!s.grupos.empty()) x["grupos"] = s.grupos;
        arr.push_back(std::move(x));
    }
    return {{"tope", l.tope}, {"salidas", arr}};
}

std::string agregar(Lista& l, Salida s) {
    if (std::string e = validarId(s.id); !e.empty()) return e;
    if (l.buscar(s.id) != nullptr) return ba::i18n::llenar(ba::i18n::traducir("salidas", "ya hay una salida «%1»"), {s.id});
    if (static_cast<int>(l.salidas.size()) >= l.tope)
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "ya hay %1 salidas, que es el tope (se cambia en Config/salidas.json)"),
                                {std::to_string(l.tope)});
    if (s.color.empty() || !colorValido(s.color)) s.color = colorDeFabrica(static_cast<int>(l.salidas.size()));
    if (s.nombre.empty()) s.nombre = s.id;
    s.orden = static_cast<int>(l.salidas.size());
    l.salidas.push_back(std::move(s));
    return {};
}

std::string quitar(Lista& l, const std::string& id) {
    if (id.empty() || id == kPorOmision) return ba::i18n::traducir("salidas", "la salida por omisión no se puede borrar");
    const auto it = std::find_if(l.salidas.begin(), l.salidas.end(), [&](const Salida& s) { return s.id == id; });
    if (it == l.salidas.end()) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {id});
    l.salidas.erase(it);
    for (std::size_t i = 0; i < l.salidas.size(); ++i) l.salidas[i].orden = static_cast<int>(i);
    return {};
}

std::string editar(Lista& l, const std::string& id, const json& cambios) {
    const auto it = std::find_if(l.salidas.begin(), l.salidas.end(), [&](const Salida& x) { return x.id == (id.empty() ? kPorOmision : id); });
    Salida* s = it == l.salidas.end() ? nullptr : &*it;
    if (s == nullptr) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {id});
    if (cambios.contains("color")) {
        const std::string c = cambios.value("color", std::string());
        if (!colorValido(c)) return ba::i18n::traducir("salidas", "`color`: #RRGGBB");
        s->color = c;
    }
    if (cambios.contains("codificador")) {
        const std::string c = cambios.value("codificador", std::string());
        static const std::set<std::string> kAlias{"", "auto", "nvenc", "qsv", "amf", "vaapi", "videotoolbox", "x264"};
        if (!kAlias.count(c))
            return ba::i18n::traducir("salidas", "`codificador`: auto, nvenc, qsv, amf, vaapi, videotoolbox o x264 (vacío: el de la configuración)");
        if (s->id == kPorOmision && !c.empty())
            return ba::i18n::traducir("salidas", "el codificador de la salida por omisión es el de la configuración");
        s->codificador = c;
    }
    if (cambios.contains("nombre")) {
        const std::string n = cambios.value("nombre", std::string());
        s->nombre = n.empty() ? s->id : n;
    }
    return {};
}

bool ponerMando(Lista& l, const std::string& id, const std::string& fuente, const Mando& m) {
    if (id.empty() || id == kPorOmision || fuente.empty()) return false;
    for (auto& s : l.salidas)
        if (s.id == id) {
            const auto it = s.mezcla.find(fuente);
            if (it != s.mezcla.end() && it->second == m) return false;
            s.mezcla[fuente] = m;
            return true;
        }
    return false;
}

bool ponerGrupo(Lista& l, const std::string& id, const std::string& grupo, double ganancia) {
    if (id.empty() || id == kPorOmision || grupo.empty()) return false;
    for (auto& s : l.salidas)
        if (s.id == id) {
            const auto it = s.grupos.find(grupo);
            const bool uno = std::abs(ganancia - 1.0) < 1e-6;
            if (uno ? it == s.grupos.end() : it != s.grupos.end() && std::abs(it->second - ganancia) < 1e-6) return false;
            if (uno) s.grupos.erase(grupo);
            else s.grupos[grupo] = ganancia;
            return true;
        }
    return false;
}

std::string salidaDeDestino(const json& destino) {
    const std::string s = destino.is_object() ? destino.value("salida", std::string()) : std::string();
    return s.empty() ? std::string(kPorOmision) : s;
}

int destinosDe(const json& destinos, const std::string& salida) {
    int n = 0;
    if (destinos.is_array())
        for (const auto& d : destinos)
            if (salidaDeDestino(d) == salida) ++n;
    return n;
}

std::string destinosDeBorrada(json& destinos, const Lista& l, const std::string& salida, const std::string& aDonde,
                              std::vector<std::string>* borrados) {
    const int n = destinosDe(destinos, salida);
    if (n == 0) return {};
    if (aDonde.empty())
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "«%1» tiene %2 destinos guardados: decí si pasan a otra salida "
                                                              "(su id) o se borran («borrar»)"),
                                {salida, std::to_string(n)});
    const bool borrar = aDonde == "borrar";
    if (!borrar && (aDonde == salida || l.buscar(aDonde) == nullptr))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "los destinos no pueden pasar a «%1»: tiene que ser otra salida"),
                                {aDonde});
    json quedan = json::array();
    for (auto d : destinos) {
        if (salidaDeDestino(d) != salida) {
            quedan.push_back(std::move(d));
        } else if (borrar) {
            if (borrados != nullptr) borrados->push_back(d.value("id", std::string()));
        } else {
            if (aDonde == kPorOmision) d.erase("salida");
            else d["salida"] = aDonde;
            quedan.push_back(std::move(d));
        }
    }
    destinos = std::move(quedan);
    return {};
}

const Aire& AlAire::de(const std::string& id) const {
    static const Aire kNada;
    const auto it = salidas.find(id.empty() ? std::string(kPorOmision) : id);
    return it == salidas.end() ? kNada : it->second;
}

Placa placaDeJson(const json& j) {
    Placa p;
    if (!j.is_object()) return p;
    // Un campo de otro tipo no tira: cuenta como apagado (es un archivo que vuelve de un corte).
    const auto si = [&j](const char* k) {
        const auto c = j.find(k);
        return c != j.end() && c->is_boolean() && c->get<bool>();
    };
    const auto texto = [&j](const char* k) {
        const auto c = j.find(k);
        return c != j.end() && c->is_string() ? c->get<std::string>() : std::string();
    };
    p.ftb = si("ftb");
    p.ftbAudio = si("ftb_audio");
    p.placa = si("placa");
    p.texto = texto("placa_texto");
    p.imagen = texto("placa_imagen");
    return p;
}

json aJson(const Placa& p) {
    return {{"ftb", p.ftb}, {"ftb_audio", p.ftbAudio}, {"placa", p.placa}, {"placa_texto", p.texto}, {"placa_imagen", p.imagen}};
}

AlAire leerAlAire(const json& j) {
    AlAire a;
    if (!j.is_object()) return a;
    a.version = j.value("version", 1);
    // Un archivo de antes de «cerrado» cuenta como cierre ordenado.
    a.cerrado = j.value("cerrado", true);
    if (const auto l = j.find("linea"); l != j.end() && l->is_object()) a.linea = *l;
    Aire& p = a.salidas[kPorOmision];
    p.escena = j.value("escena", std::string());
    p.transmitiendo = j.value("streaming", false);
    p.grabando = j.value("recording", false);
    const json todas = j.value("salidas", json::object());
    for (const auto& [id, x] : todas.items()) {
        if (!x.is_object() || !validarId(id).empty()) continue;
        Aire& s = a.salidas[id];
        s.escena = x.value("escena", s.escena);
        s.transmitiendo = x.value("streaming", s.transmitiendo);
        s.grabando = x.value("recording", s.grabando);
        if (const json d = x.value("destinos", json::array()); d.is_array())
            for (const json& id : d)
                if (id.is_string()) s.destinos.push_back(id.get<std::string>());
        if (const auto pl = x.find("placa"); pl != x.end()) s.placa = placaDeJson(*pl);
    }
    return a;
}

json aJson(const AlAire& a) {
    const Aire& p = a.de(kPorOmision);
    json todas = json::object();
    for (const auto& [id, s] : a.salidas)
        todas[id] = {{"escena", s.escena}, {"streaming", s.transmitiendo}, {"recording", s.grabando}, {"destinos", s.destinos},
                     {"placa", aJson(s.placa)}};
    json j{{"version", kVersionAire}, {"streaming", p.transmitiendo}, {"recording", p.grabando}, {"escena", p.escena},
           {"cerrado", a.cerrado}, {"salidas", todas}};
    if (a.linea.is_object()) j["linea"] = a.linea;
    return j;
}

void sumarSeccionesViejas(AlAire& a, const json& placa, const json& linea) {
    if (placa.is_object()) {
        // La por omisión en los campos sueltos; las demás, en `salidas` (si son ids válidos).
        a.salidas[kPorOmision].placa = placaDeJson(placa);
        const json otras = placa.value("salidas", json::object());
        if (otras.is_object())
            for (const auto& [id, x] : otras.items())
                if (validarId(id).empty()) a.salidas[id].placa = placaDeJson(x);
    }
    if (linea.is_object() && !a.linea.is_object()) a.linea = linea;
}

} // namespace ba::app::salidas
