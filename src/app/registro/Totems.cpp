// ============================================================================
// src/app/registro/Totems.cpp — ver Totems.hpp
// ============================================================================
#include "registro/Totems.hpp"

#include <algorithm>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace ba::registro {
namespace {

using nlohmann::json;

constexpr const char* kArchivo = "registro.json";

std::string textoDe(const json& j, const char* clave, const std::string& siNo = {}) {
    if (!j.is_object() || !j.contains(clave) || !j[clave].is_string()) return siNo;
    return j[clave].get<std::string>();
}

bool boolDe(const json& j, const char* clave, bool siNo) {
    if (!j.is_object() || !j.contains(clave) || !j[clave].is_boolean()) return siNo;
    return j[clave].get<bool>();
}

struct Tipo {
    TipoCampo tipo;
    const char* nombre;
    const char* etiqueta;
    const char* pregunta;
};
const Tipo kTipos[] = {
    {TipoCampo::Nombre, "nombre", "Nombre y apellido", "¿Cómo te llamás? Decime tu nombre y apellido."},
    {TipoCampo::Correo, "correo", "Correo electrónico", "¿Me decís tu correo electrónico?"},
    {TipoCampo::Telefono, "telefono", "Teléfono", "¿Me pasás tu número de teléfono?"},
    {TipoCampo::Numero, "numero", "Número", "¿Cuántos?"},
    {TipoCampo::SiNo, "sino", "Sí o no", "¿Sí o no?"},
    {TipoCampo::Opcion, "opcion", "Una de varias opciones", "¿Cuál elegís?"},
    {TipoCampo::Texto, "texto", "Texto libre", "Contame."},
};

const Tipo& tipo(TipoCampo t) {
    for (const Tipo& x : kTipos)
        if (x.tipo == t) return x;
    return kTipos[6];
}

/// Sin acentos y en minúsculas, para armar ids. Lo que no es letra ni número
/// queda como está (lo decide quien llama).
std::string sinAcentos(const std::string& s) {
    static const struct {
        const char* de;
        char a;
    } kAcentos[] = {{"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
                    {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'}};
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (static_cast<unsigned char>(s[i]) >= 0x80 && i + 1 < s.size()) {
            char c = ' ';
            for (const auto& a : kAcentos)
                if (s.compare(i, 2, a.de) == 0) c = a.a;
            out.push_back(c);
            i += 2;
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
        ++i;
    }
    return out;
}

void reemplazar(std::string& s, const std::string& de, const std::string& a) {
    for (std::size_t p = s.find(de); p != std::string::npos; p = s.find(de, p + a.size())) s.replace(p, de.size(), a);
}

} // namespace

// --- Tipos -------------------------------------------------------------------

const char* nombreDeTipo(TipoCampo t) { return tipo(t).nombre; }
const char* etiquetaDeTipo(TipoCampo t) { return tipo(t).etiqueta; }
std::string preguntaDeFabrica(TipoCampo t) { return tipo(t).pregunta; }

bool tipoDeNombre(const std::string& s, TipoCampo& out) {
    for (const Tipo& x : kTipos)
        if (s == x.nombre) {
            out = x.tipo;
            return true;
        }
    return false;
}

const std::vector<TipoCampo>& todosLosTipos() {
    static const std::vector<TipoCampo> v = [] {
        std::vector<TipoCampo> r;
        for (const Tipo& x : kTipos) r.push_back(x.tipo);
        return r;
    }();
    return v;
}

// --- Campo -------------------------------------------------------------------

json Campo::aJson() const {
    json j{{"clave", clave},
           {"etiqueta", etiqueta},
           {"pregunta", pregunta},
           {"tipo", nombreDeTipo(tipo)},
           {"obligatorio", obligatorio}};
    if (tipo == TipoCampo::Opcion) j["opciones"] = opciones;
    if (!ejemplo.empty()) j["ejemplo"] = ejemplo;
    return j;
}

Campo Campo::deJson(const json& j) {
    Campo c;
    c.clave = textoDe(j, "clave");
    c.etiqueta = textoDe(j, "etiqueta");
    c.pregunta = textoDe(j, "pregunta");
    // Un tipo que no se entiende queda en texto: se le pregunta igual y se
    // anota lo que diga, que es mejor que perder el campo.
    tipoDeNombre(textoDe(j, "tipo"), c.tipo);
    c.obligatorio = boolDe(j, "obligatorio", false);
    if (j.is_object() && j.contains("opciones") && j["opciones"].is_array())
        for (const json& o : j["opciones"])
            if (o.is_string() && !o.get<std::string>().empty()) c.opciones.push_back(o.get<std::string>());
    c.ejemplo = textoDe(j, "ejemplo");
    return c;
}

// --- Totem -------------------------------------------------------------------

json Totem::aJson() const {
    json cs = json::array();
    for (const Campo& c : campos) cs.push_back(c.aJson());
    return json{{"id", id},
                {"nombre", nombre},
                {"activo", activo},
                {"titulo", titulo},
                {"subtitulo", subtitulo},
                {"saludo", saludo},
                {"despedida", despedida},
                {"yaRegistrado", yaRegistrado},
                {"confirmar", confirmar},
                {"reconocer", reconocer},
                {"foto", foto},
                {"campos", cs}};
}

Totem Totem::deJson(const json& j) {
    // Lo que falta queda con el valor de fábrica de un tótem nuevo: un archivo
    // escrito a mano con solo {id, campos} tiene que andar.
    Totem t = totemNuevo(textoDe(j, "id"), textoDe(j, "nombre"));
    t.activo = boolDe(j, "activo", t.activo);
    t.titulo = textoDe(j, "titulo", t.titulo);
    t.subtitulo = textoDe(j, "subtitulo", t.subtitulo);
    t.saludo = textoDe(j, "saludo", t.saludo);
    t.despedida = textoDe(j, "despedida", t.despedida);
    t.yaRegistrado = textoDe(j, "yaRegistrado", t.yaRegistrado);
    t.confirmar = boolDe(j, "confirmar", t.confirmar);
    t.reconocer = boolDe(j, "reconocer", t.reconocer);
    t.foto = boolDe(j, "foto", t.foto);
    if (j.is_object() && j.contains("campos") && j["campos"].is_array()) {
        t.campos.clear();
        for (const json& c : j["campos"]) t.campos.push_back(Campo::deJson(c));
    }
    return t;
}

bool idValido(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool claveValida(const std::string& clave) {
    if (clave.empty() || clave.size() > 40) return false;
    return std::all_of(clave.begin(), clave.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::string idDesdeNombre(const std::string& nombre, const std::vector<std::string>& usados) {
    std::string base;
    for (char c : sinAcentos(nombre)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) base.push_back(c);
        else if (!base.empty() && base.back() != '-') base.push_back('-');
    }
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.size() > 28) base.resize(28);
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.empty()) base = "totem";
    const auto libre = [&usados](const std::string& s) { return std::find(usados.begin(), usados.end(), s) == usados.end(); };
    if (libre(base)) return base;
    for (int n = 2;; ++n)
        if (const std::string s = base + "-" + std::to_string(n); libre(s)) return s;
}

Totem totemNuevo(const std::string& id, const std::string& nombre) {
    Totem t;
    t.id = id;
    t.nombre = nombre.empty() ? std::string("Registro") : nombre;
    // Frases cortas y neutras: se dicen en voz alta, a gente que no se sabe
    // si es él o ella, y el evento cambia de nombre sin tocar esto ({evento}).
    t.titulo = "{evento}";
    t.subtitulo = "Registrate hablando: mirá a la cámara y contestá en voz alta.";
    t.saludo = "{hola}. Te damos la bienvenida a {evento}. Te voy a pedir unos datos para registrarte.";
    t.despedida = "Listo, ya quedó tu registro. ¡Que disfrutes {evento}!";
    t.yaRegistrado = "{hola}. Ya tenés tu registro en {evento}, no hace falta hacerlo de nuevo. ¡Que lo disfrutes!";
    Campo n;
    n.clave = "nombre";
    n.etiqueta = "Nombre";
    n.tipo = TipoCampo::Nombre;
    n.obligatorio = true;
    Campo c;
    c.clave = "correo";
    c.etiqueta = "Correo";
    c.tipo = TipoCampo::Correo;
    c.ejemplo = "juan punto perez arroba gmail punto com";
    t.campos = {n, c};
    return t;
}

std::string validar(const Totem& t) {
    if (!idValido(t.id)) return "El id del tótem va de 1 a 32 letras minúsculas, números o guiones (sin espacios ni tildes).";
    if (t.nombre.empty()) return "Ponele un nombre al tótem.";
    std::set<std::string> claves;
    for (const Campo& c : t.campos) {
        if (!claveValida(c.clave))
            return "La clave «" + c.clave + "» no sirve: van letras minúsculas, números y guion bajo (sin espacios ni tildes).";
        if (!claves.insert(c.clave).second) return "Hay dos campos con la clave «" + c.clave + "».";
        if (c.tipo == TipoCampo::Opcion && c.opciones.size() < 2)
            return "El campo «" + (c.etiqueta.empty() ? c.clave : c.etiqueta) + "» es de opciones: hacen falta al menos dos.";
    }
    return {};
}

std::string conDatos(const std::string& texto, const std::string& evento, const std::string& nombre) {
    std::string s = texto;
    // {hola} con el nombre de pila: «Hola Ana» suena a persona; «Hola Ana María
    // Pérez Gutiérrez», a sistema.
    const std::string pila = nombre.substr(0, nombre.find(' '));
    reemplazar(s, "{hola}", pila.empty() ? std::string("Hola") : "Hola " + pila);
    reemplazar(s, "{nombre}", nombre);
    reemplazar(s, "{evento}", evento.empty() ? std::string("el evento") : evento);
    // Un {nombre} vacío deja espacios y comas sueltas: se emprolija.
    std::string out;
    for (char c : s) {
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        if ((c == ',' || c == '.' || c == '!' || c == '?') && !out.empty() && out.back() == ' ') out.pop_back();
        if (c == ',' && !out.empty() && (out.back() == ',' || out.back() == '.')) continue;
        out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// --- La lista ----------------------------------------------------------------

const Totem* Totems::porId(const std::string& id) const {
    for (const Totem& t : lista_)
        if (t.id == id) return &t;
    return nullptr;
}

std::string Totems::agregar(Totem t) {
    std::vector<std::string> usados;
    for (const Totem& o : lista_) usados.push_back(o.id);
    if (t.id.empty()) t.id = idDesdeNombre(t.nombre, usados);
    if (porId(t.id) != nullptr) return "Ya hay un tótem con el id «" + t.id + "».";
    if (const std::string e = validar(t); !e.empty()) return e;
    lista_.push_back(std::move(t));
    return {};
}

std::string Totems::cambiar(const std::string& id, const json& cambios) {
    auto it = std::find_if(lista_.begin(), lista_.end(), [&id](const Totem& t) { return t.id == id; });
    if (it == lista_.end()) return "No hay ningún tótem «" + id + "».";
    if (!cambios.is_object()) return "Los cambios tienen que ser un objeto.";
    // Se arma el resultado entero y se valida ANTES de tocar nada: un cambio a
    // medias (el nombre sí, los campos no) deja un tótem que nadie pidió.
    json j = it->aJson();
    for (auto c = cambios.begin(); c != cambios.end(); ++c) {
        if (c.key() == "id") continue; // el id no cambia: es la url y el secreto
        if (!j.contains(c.key())) return "No conozco «" + c.key() + "» en un tótem.";
        j[c.key()] = c.value();
    }
    Totem nuevo = Totem::deJson(j);
    nuevo.id = it->id;
    if (const std::string e = validar(nuevo); !e.empty()) return e;
    *it = std::move(nuevo);
    return {};
}

bool Totems::quitar(const std::string& id) {
    const auto it = std::remove_if(lista_.begin(), lista_.end(), [&id](const Totem& t) { return t.id == id; });
    if (it == lista_.end()) return false;
    lista_.erase(it, lista_.end());
    return true;
}

json Totems::aJson() const {
    json ts = json::array();
    for (const Totem& t : lista_) ts.push_back(t.aJson());
    return json{{"totems", ts}};
}

Totems Totems::deJson(const json& j) {
    Totems out;
    if (!j.is_object() || !j.contains("totems") || !j["totems"].is_array()) return out;
    for (const json& jt : j["totems"]) {
        Totem t = Totem::deJson(jt);
        // Uno roto (sin id, repetido) no tira los demás.
        if (!idValido(t.id) || out.porId(t.id) != nullptr) continue;
        out.lista_.push_back(std::move(t));
    }
    return out;
}

// --- Disco -------------------------------------------------------------------

Totems cargarTotems(const fs::path& carpeta, bool* ok, std::string* err) {
    if (ok != nullptr) *ok = true;
    const fs::path archivo = carpeta / kArchivo;
    std::error_code ec;
    if (carpeta.empty() || !fs::exists(archivo, ec)) return {};
    std::ifstream in(archivo);
    const json j = json::parse(in, nullptr, false);
    if (!in.good() && !in.eof()) {
        if (ok != nullptr) *ok = false;
        if (err != nullptr) *err = "no se pudo leer " + archivo.string();
        return {};
    }
    if (j.is_discarded()) {
        if (ok != nullptr) *ok = false;
        if (err != nullptr) *err = "la configuración de registro está rota (JSON inválido): " + archivo.string();
        return {};
    }
    return Totems::deJson(j);
}

bool guardarTotems(const fs::path& carpeta, const Totems& t, std::string* err) {
    if (carpeta.empty()) {
        if (err != nullptr) *err = "no hay un evento abierto";
        return false;
    }
    std::error_code ec;
    fs::create_directories(carpeta, ec);
    const fs::path archivo = carpeta / kArchivo;
    const fs::path temporal = carpeta / "registro.json.tmp";
    {
        std::ofstream out(temporal, std::ios::trunc);
        if (!out) {
            if (err != nullptr) *err = "no se pudo escribir " + temporal.string();
            return false;
        }
        out << t.aJson().dump(2, ' ', false, json::error_handler_t::replace) << '\n';
        if (!out) {
            if (err != nullptr) *err = "no se pudo terminar de escribir " + temporal.string();
            return false;
        }
    }
    fs::rename(temporal, archivo, ec);
    if (ec) {
        if (err != nullptr) *err = "no se pudo reemplazar la configuración de registro: " + ec.message();
        return false;
    }
    return true;
}

} // namespace ba::registro
