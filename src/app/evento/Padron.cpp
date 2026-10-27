// ============================================================================
// src/app/evento/Padron.cpp — ver Padron.hpp
// ============================================================================
#include "evento/Padron.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>

namespace fs = std::filesystem;

namespace ba::evento {
namespace {

using nlohmann::json;

constexpr const char* kArchivo = "padron.json";

std::string textoDe(const json& j, const char* clave) {
    if (!j.contains(clave) || !j[clave].is_string()) return {};
    return j[clave].get<std::string>();
}

bool boolDe(const json& j, const char* clave, bool porDefecto = false) {
    if (!j.contains(clave) || !j[clave].is_boolean()) return porDefecto;
    return j[clave].get<bool>();
}

std::string nuevoId() {
    static int n = 0;
    return "a" + std::to_string(++n) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
}

/// Para comparar nombres tipeados a mano: sin mayúsculas, sin acentos y sin
/// espacios de más. «José Pérez», «jose perez» y « JOSÉ  PÉREZ » son el mismo
/// señor, y en un mostrador con cola nadie escribe el acento.
std::string aplanar(const std::string& s) {
    static const struct {
        const char* de;
        char a;
    } kAcentos[] = {
        {"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
        {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'},
    };
    std::string out;
    out.reserve(s.size());
    bool espacioPendiente = false;
    for (std::size_t i = 0; i < s.size();) {
        char c = 0;
        std::size_t paso = 1;
        // Los acentos son dos bytes en UTF-8: hay que mirar el par, no el byte.
        if (static_cast<unsigned char>(s[i]) >= 0x80 && i + 1 < s.size()) {
            for (const auto& a : kAcentos)
                if (s.compare(i, 2, a.de) == 0) {
                    c = a.a;
                    paso = 2;
                    break;
                }
            if (c == 0) paso = 2; // otro carácter multibyte: se descarta
        } else {
            c = s[i];
        }
        i += paso;
        if (c == 0) continue;
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!out.empty()) espacioPendiente = true;
            continue;
        }
        if (espacioPendiente) {
            out.push_back(' ');
            espacioPendiente = false;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

std::string nuevoCodigo() {
    // Sin O ni 0, sin I ni 1: al dictarlos en voz alta son la misma letra, y
    // este codigo se dicta. Con 32 simbolos y cuatro posiciones hay un millon
    // de combinaciones, de sobra para cualquier sala y corto de leer.
    static const char* kAlfabeto = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> d(0, 31);
    std::string c;
    for (int i = 0; i < 4; ++i)
        c.push_back(kAlfabeto[d(rng)]);
    return c;
}

// --- Los nombres de cada cosa -----------------------------------------------

const char* nombreDePapel(Papel p) {
    switch (p) {
        case Papel::Orador: return "orador";
        case Papel::Panelista: return "panelista";
        case Papel::Publico: return "publico";
        case Papel::Prensa: return "prensa";
        case Papel::Staff: return "staff";
    }
    return "publico";
}

const char* nombreDeCanal(Canal c) {
    switch (c) {
        case Canal::SinDefinir: return "sin_definir";
        case Canal::Presencial: return "presencial";
        case Canal::Online: return "online";
    }
    return "sin_definir";
}

const char* nombreDeEstado(Estado e) {
    switch (e) {
        case Estado::Invitado: return "invitado";
        case Estado::Confirmo: return "confirmo";
        case Estado::Llego: return "llego";
        case Estado::SeFue: return "se_fue";
    }
    return "invitado";
}

bool papelDeNombre(const std::string& s, Papel& out) {
    for (Papel p : {Papel::Orador, Papel::Panelista, Papel::Publico, Papel::Prensa, Papel::Staff})
        if (s == nombreDePapel(p)) {
            out = p;
            return true;
        }
    return false;
}

bool canalDeNombre(const std::string& s, Canal& out) {
    for (Canal c : {Canal::SinDefinir, Canal::Presencial, Canal::Online})
        if (s == nombreDeCanal(c)) {
            out = c;
            return true;
        }
    return false;
}

bool estadoDeNombre(const std::string& s, Estado& out) {
    for (Estado e : {Estado::Invitado, Estado::Confirmo, Estado::Llego, Estado::SeFue})
        if (s == nombreDeEstado(e)) {
            out = e;
            return true;
        }
    return false;
}

// --- Un asistente ------------------------------------------------------------

json Asistente::aJson() const {
    return json{{"id", id},
                {"personaId", personaId},
                {"nombre", nombre},
                {"papel", nombreDePapel(papel)},
                {"canal", nombreDeCanal(canal)},
                {"estado", nombreDeEstado(estado)},
                {"contacto", contacto},
                {"nota", nota},
                {"dejarPasarSolo", dejarPasarSolo},
                {"vota", vota},
                {"codigo", codigo},
                {"llegoMs", llegoMs},
                {"porDonde", porDonde},
                {"datos", datos.is_object() ? datos : json::object()},
                {"registro", registro}};
}

Asistente Asistente::deJson(const json& j) {
    Asistente a;
    if (!j.is_object()) return a;
    a.id = textoDe(j, "id");
    a.personaId = textoDe(j, "personaId");
    a.nombre = textoDe(j, "nombre");
    // Un papel, canal o estado que no se entiende NO tira la fila: se queda con
    // el valor de fábrica y la persona sigue en la lista. Perder a alguien del
    // padrón porque un archivo trae una palabra nueva es peor que mostrarlo mal.
    papelDeNombre(textoDe(j, "papel"), a.papel);
    canalDeNombre(textoDe(j, "canal"), a.canal);
    estadoDeNombre(textoDe(j, "estado"), a.estado);
    a.contacto = textoDe(j, "contacto");
    a.nota = textoDe(j, "nota");
    a.dejarPasarSolo = boolDe(j, "dejarPasarSolo");
    a.vota = boolDe(j, "vota");
    a.codigo = textoDe(j, "codigo");
    if (j.contains("llegoMs") && j["llegoMs"].is_number_integer())
        a.llegoMs = j["llegoMs"].get<std::int64_t>();
    a.porDonde = textoDe(j, "porDonde");
    // Los padrones escritos antes del registro por tótem no traen `datos`: se
    // cargan igual, con el objeto vacío. Uno que trae otra cosa (un texto, una
    // lista) tampoco tira la fila: se descarta solo ese dato.
    if (j.contains("datos") && j["datos"].is_object()) a.datos = j["datos"];
    a.registro = textoDe(j, "registro");
    // Sin ficha de rostro no se puede reconocer a nadie, así que dejar pasar
    // solo no significa nada: entraría por escribir un nombre, que es
    // justamente lo que no se quiere.
    if (a.personaId.empty()) a.dejarPasarSolo = false;
    return a;
}

// --- El padrón ---------------------------------------------------------------

Asistente* Padron::buscar(const std::string& id) {
    for (Asistente& a : gente_)
        if (a.id == id) return &a;
    return nullptr;
}

const Asistente* Padron::porId(const std::string& id) const {
    for (const Asistente& a : gente_)
        if (a.id == id) return &a;
    return nullptr;
}

const Asistente* Padron::porPersona(const std::string& personaId) const {
    if (personaId.empty()) return nullptr;
    for (const Asistente& a : gente_)
        if (a.personaId == personaId) return &a;
    return nullptr;
}

const Asistente* Padron::porCodigo(const std::string& codigo) const {
    if (codigo.empty()) return nullptr;
    for (const Asistente& a : gente_)
        if (a.codigo == codigo) return &a;
    return nullptr;
}

const Asistente* Padron::porNombre(const std::string& nombre) const {
    const std::string busco = aplanar(nombre);
    if (busco.empty()) return nullptr;
    for (const Asistente& a : gente_)
        if (aplanar(a.nombre) == busco) return &a;
    return nullptr;
}

std::string Padron::agregar(Asistente a) {
    if (a.id.empty()) a.id = nuevoId();
    // Todos llevan codigo aunque hoy no voten: se imprime con la invitacion, y
    // que alguien pase a tener voto el dia del evento no puede obligar a
    // reimprimir nada. Se reintenta si salio uno repetido.
    for (int intento = 0; a.codigo.empty() && intento < 20; ++intento) {
        const std::string c = nuevoCodigo();
        if (std::none_of(gente_.begin(), gente_.end(), [&c](const Asistente& o) { return o.codigo == c; }))
            a.codigo = c;
    }
    if (a.personaId.empty()) a.dejarPasarSolo = false;
    const std::string id = a.id;
    gente_.push_back(std::move(a));
    return id;
}

bool Padron::cambiar(const std::string& id, const Asistente& campos,
                     const std::vector<std::string>& queCampos) {
    Asistente* a = buscar(id);
    if (a == nullptr) return false;
    const auto pidieron = [&queCampos](const char* c) {
        return std::find(queCampos.begin(), queCampos.end(), c) != queCampos.end();
    };
    if (pidieron("personaId")) a->personaId = campos.personaId;
    if (pidieron("nombre")) a->nombre = campos.nombre;
    if (pidieron("papel")) a->papel = campos.papel;
    if (pidieron("canal")) a->canal = campos.canal;
    if (pidieron("estado")) a->estado = campos.estado;
    if (pidieron("contacto")) a->contacto = campos.contacto;
    if (pidieron("nota")) a->nota = campos.nota;
    if (pidieron("dejarPasarSolo")) a->dejarPasarSolo = campos.dejarPasarSolo;
    if (pidieron("vota")) a->vota = campos.vota;
    if (pidieron("registro")) a->registro = campos.registro;
    // Los datos se SUMAN, clave por clave: registrarse de nuevo por otro tótem
    // (o corregir un dato) no tiene por qué borrar lo que contestó antes. Para
    // sacar una clave se la manda en null.
    if (pidieron("datos") && campos.datos.is_object()) {
        if (!a->datos.is_object()) a->datos = json::object();
        for (auto it = campos.datos.begin(); it != campos.datos.end(); ++it) {
            if (it.value().is_null()) a->datos.erase(it.key());
            else a->datos[it.key()] = it.value();
        }
    }
    if (a->personaId.empty()) a->dejarPasarSolo = false;
    return true;
}

bool Padron::quitar(const std::string& id) {
    const auto it =
        std::remove_if(gente_.begin(), gente_.end(), [&id](const Asistente& a) { return a.id == id; });
    if (it == gente_.end()) return false;
    gente_.erase(it, gente_.end());
    return true;
}

bool Padron::acreditar(const std::string& id, const std::string& porDonde, std::int64_t nowMs) {
    Asistente* a = buscar(id);
    if (a == nullptr) return false;
    a->estado = Estado::Llego;
    // La hora de la PRIMERA vez. En la puerta se vuelve a escanear por las
    // dudas todo el tiempo, y pisar la hora convertiría «llegó 18:40» en «llegó
    // 20:15», que es justo el dato que después nadie puede reconstruir.
    if (a->llegoMs == 0) {
        a->llegoMs = nowMs;
        a->porDonde = porDonde;
    }
    return true;
}

bool Padron::despedir(const std::string& id) {
    Asistente* a = buscar(id);
    if (a == nullptr) return false;
    a->estado = Estado::SeFue;
    return true;
}

Padron::Votantes Padron::votantes() const {
    Votantes v;
    for (const Asistente& a : gente_) {
        if (!a.vota) continue;
        ++v.podian;
        if (a.estado == Estado::Llego) v.habilitados.push_back({a.codigo, a.nombre});
    }
    return v;
}

Resumen Padron::resumen() const {
    Resumen r;
    r.total = static_cast<int>(gente_.size());
    for (const Asistente& a : gente_) {
        if (a.vota) ++r.conVoto;
        if (a.estado == Estado::Confirmo) ++r.confirmados;
        if (a.estado == Estado::Invitado || a.estado == Estado::Confirmo) ++r.faltan;
        if (a.estado != Estado::Llego) continue;
        ++r.presentes;
        if (a.vota) ++r.votoPresente;
        // Sin canal previsto, lo dice por dónde se acreditó (el QR sirve en los dos lados).
        const bool aca = a.canal == Canal::Presencial ||
                         (a.canal == Canal::SinDefinir && (a.porDonde == "mostrador" || a.porDonde == "rostro" ||
                                                     a.porDonde == "registro"));
        const bool online = a.canal == Canal::Online || (a.canal == Canal::SinDefinir && a.porDonde == "lobby");
        if (aca)
            ++r.presenciales;
        else if (online)
            ++r.enLinea;
        else
            ++r.sinDato;
    }
    return r;
}

json Padron::aJson() const {
    json gs = json::array();
    for (const Asistente& a : gente_)
        gs.push_back(a.aJson());
    return json{{"gente", gs}};
}

Padron Padron::deJson(const json& j) {
    Padron p;
    if (!j.is_object() || !j.contains("gente") || !j["gente"].is_array()) return p;
    for (const json& ja : j["gente"]) {
        Asistente a = Asistente::deJson(ja);
        // Una fila sin id no se puede referenciar para nada: se le da uno en vez
        // de tirarla.
        if (a.id.empty()) a.id = nuevoId();
        // Y sin codigo no podria votar nunca. Pasa con los padrones escritos
        // antes de que el codigo existiera: el archivo es viejo, la persona no.
        // Se le da uno acá, cuidando que no choque con los que ya salieron.
        for (int intento = 0; a.codigo.empty() && intento < 20; ++intento) {
            const std::string c = nuevoCodigo();
            if (std::none_of(p.gente_.begin(), p.gente_.end(),
                             [&c](const Asistente& o) { return o.codigo == c; }))
                a.codigo = c;
        }
        p.gente_.push_back(std::move(a));
    }
    return p;
}

// --- Disco -------------------------------------------------------------------

Padron cargarPadron(const fs::path& carpeta, bool* ok, std::string* err) {
    if (ok != nullptr) *ok = true;
    const fs::path archivo = carpeta / kArchivo;
    std::error_code ec;
    // Que no exista es lo normal hasta que alguien anota al primero: no es un
    // error, es un padrón vacío.
    if (!fs::exists(archivo, ec)) return {};

    std::ifstream in(archivo);
    if (!in) {
        if (ok != nullptr) *ok = false;
        if (err != nullptr) *err = ba::i18n::llenar(ba::i18n::traducir("evento", "no se pudo abrir %1"), {archivo.string()});
        return {};
    }
    const json j = json::parse(in, nullptr, false);
    if (j.is_discarded()) {
        if (ok != nullptr) *ok = false;
        if (err != nullptr)
            *err = ba::i18n::llenar(ba::i18n::traducir("evento", "el padron esta roto (JSON invalido): %1"), {archivo.string()});
        return {};
    }
    return Padron::deJson(j);
}

bool guardarPadron(const fs::path& carpeta, const Padron& p, std::string* err) {
    std::error_code ec;
    fs::create_directories(carpeta, ec);
    const fs::path archivo = carpeta / kArchivo;
    // Se escribe al lado y se renombra: si se corta la luz en medio de una
    // acreditación, el padrón que estaba sigue entero. Un renombre es atómico;
    // escribir encima, no.
    const fs::path temporal = carpeta / "padron.json.tmp";
    {
        std::ofstream out(temporal, std::ios::trunc);
        if (!out) {
            if (err != nullptr) *err = ba::i18n::llenar(ba::i18n::traducir("evento", "no se pudo escribir %1"), {temporal.string()});
            return false;
        }
        out << p.aJson().dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
        if (!out) {
            if (err != nullptr)
                *err = ba::i18n::llenar(ba::i18n::traducir("evento", "no se pudo terminar de escribir %1"), {temporal.string()});
            return false;
        }
    }
    fs::rename(temporal, archivo, ec);
    if (ec) {
        if (err != nullptr)
            *err = ba::i18n::llenar(ba::i18n::traducir("evento", "no se pudo reemplazar el padron: %1"), {ec.message()});
        return false;
    }
    return true;
}

} // namespace ba::evento
