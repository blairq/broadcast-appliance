// ============================================================================
// src/app/registro/Totems.cpp — ver Totems.hpp
// ============================================================================
#include "registro/Totems.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <fstream>
#include <set>

#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(contexto, texto) texto
#endif

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
    const char* nombre;   ///< la clave (registro.json): no se traduce
    const char* etiqueta; ///< se traduce al mostrarla (contexto «totem»)
    const char* pregunta; ///< se traduce al decirla, en el idioma del evento
};
const Tipo kTipos[] = {
    {TipoCampo::Nombre, "nombre", QT_TRANSLATE_NOOP("totem", "Nombre y apellido"),
     QT_TRANSLATE_NOOP("totem", "¿Cómo te llamás? Decime tu nombre y apellido.")},
    {TipoCampo::Correo, "correo", QT_TRANSLATE_NOOP("totem", "Correo electrónico"),
     QT_TRANSLATE_NOOP("totem", "¿Me decís tu correo electrónico?")},
    {TipoCampo::Telefono, "telefono", QT_TRANSLATE_NOOP("totem", "Teléfono"),
     QT_TRANSLATE_NOOP("totem", "¿Me pasás tu número de teléfono?")},
    {TipoCampo::Numero, "numero", QT_TRANSLATE_NOOP("totem", "Número"), QT_TRANSLATE_NOOP("totem", "¿Cuántos?")},
    {TipoCampo::SiNo, "sino", QT_TRANSLATE_NOOP("totem", "Sí o no"), QT_TRANSLATE_NOOP("totem", "¿Sí o no?")},
    {TipoCampo::Opcion, "opcion", QT_TRANSLATE_NOOP("totem", "Una de varias opciones"),
     QT_TRANSLATE_NOOP("totem", "¿Cuál elegís?")},
    {TipoCampo::Texto, "texto", QT_TRANSLATE_NOOP("totem", "Texto libre"), QT_TRANSLATE_NOOP("totem", "Contame.")},
    // Al final: `tipo()` cae en el de índice 6 (texto) con uno que no conoce.
    {TipoCampo::Documento, "documento", QT_TRANSLATE_NOOP("totem", "Documento (DNI por su código)"),
     QT_TRANSLATE_NOOP("totem", "Mostrame tu DNI a la cámara, del lado del código de barras.")},
};

/// "" = el idioma del operador.
ba::i18n::En enIdioma(const std::string& idioma) {
    return ba::i18n::En(idioma.empty() ? ba::i18n::actual().codigo : idioma);
}

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
std::string etiquetaDeTipo(TipoCampo t, const std::string& idioma) { return enIdioma(idioma).traducir("totem", tipo(t).etiqueta); }
std::string preguntaDeFabrica(TipoCampo t, const std::string& idioma) {
    return enIdioma(idioma).traducir("totem", tipo(t).pregunta);
}

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
    if (tipo == TipoCampo::Documento) j["documentos"] = documentos;
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
    if (j.is_object() && j.contains("documentos") && j["documentos"].is_array())
        for (const json& d : j["documentos"])
            if (d.is_string() && !d.get<std::string>().empty()) c.documentos.push_back(d.get<std::string>());
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
                {"ficha", ficha},
                {"conModelo", conModelo},
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
    t.ficha = textoDe(j, "ficha", t.ficha);
    t.conModelo = boolDe(j, "conModelo", t.conModelo);
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

Totem totemNuevo(const std::string& id, const std::string& nombre, const std::string& idioma) {
    const ba::i18n::En e = enIdioma(idioma);
    Totem t;
    t.id = id;
    // Para el operador: en su idioma.
    t.nombre = nombre.empty() ? ba::i18n::traducir("registro", "Registro") : nombre;
    // Lo demás lo ve y lo oye el público: en el idioma del evento. Frases cortas y neutras: se
    // dicen en voz alta, a gente que no se sabe si es él o ella, y el evento cambia de nombre
    // sin tocar esto ({evento}). {hola}, {nombre} y {evento} no se traducen.
    t.titulo = "{evento}";
    t.subtitulo = e.traducir("totem", "Registrate hablando: mirá a la cámara y contestá en voz alta.");
    t.saludo = e.traducir("totem", "{hola}. Te damos la bienvenida a {evento}. Te voy a pedir unos datos para registrarte.");
    t.despedida = e.traducir("totem", "Listo, ya quedó tu registro. ¡Que disfrutes {evento}!");
    t.yaRegistrado =
        e.traducir("totem", "{hola}. Ya tenés tu registro en {evento}, no hace falta hacerlo de nuevo. ¡Que lo disfrutes!");
    Campo n;
    n.clave = "nombre";
    n.etiqueta = e.traducir("totem", "Nombre");
    n.tipo = TipoCampo::Nombre;
    n.obligatorio = true;
    Campo c;
    c.clave = "correo";
    c.etiqueta = e.traducir("totem", "Correo");
    c.tipo = TipoCampo::Correo;
    //: un correo dictado como se lo dice en el idioma («at», «dot»): lo tiene que entender totem.json
    c.ejemplo = e.traducir("totem", "juan punto perez arroba gmail punto com");
    t.campos = {n, c};
    return t;
}

std::string validar(const Totem& t) {
    if (!idValido(t.id))
        return ba::i18n::traducir("registro", "El id del tótem va de 1 a 32 letras minúsculas, números o guiones (sin espacios "
                                              "ni tildes).");
    if (t.nombre.empty()) return ba::i18n::traducir("registro", "Ponele un nombre al tótem.");
    std::set<std::string> claves;
    for (const Campo& c : t.campos) {
        if (!claveValida(c.clave))
            return ba::i18n::llenar(ba::i18n::traducir("registro", "La clave «%1» no sirve: van letras minúsculas, números y "
                                                                   "guion bajo (sin espacios ni tildes)."),
                                    {c.clave});
        if (!claves.insert(c.clave).second)
            return ba::i18n::llenar(ba::i18n::traducir("registro", "Hay dos campos con la clave «%1»."), {c.clave});
        if (c.tipo == TipoCampo::Opcion && c.opciones.size() < 2)
            return ba::i18n::llenar(ba::i18n::traducir("registro", "El campo «%1» es de opciones: hacen falta al menos dos."),
                                    {c.etiqueta.empty() ? c.clave : c.etiqueta});
    }
    // La ficha va entera en cada pedido al modelo: larga, el modelo local tarda más en cada
    // respuesta (y se dice en voz alta lo que haga falta, no un folleto).
    if (t.ficha.size() > 4000)
        return ba::i18n::traducir("registro", "La ficha del evento es muy larga (hasta 4000 letras): dejá lo que se pregunta "
                                              "en la puerta.");
    return {};
}

std::string conDatos(const std::string& texto, const std::string& evento, const std::string& nombre,
                     const std::string& idioma) {
    const ba::i18n::En e = enIdioma(idioma);
    std::string s = texto;
    // {hola} con el nombre de pila: «Hola Ana» suena a persona; «Hola Ana María
    // Pérez Gutiérrez», a sistema.
    const std::string pila = nombre.substr(0, nombre.find(' '));
    reemplazar(s, "{hola}", pila.empty() ? e.traducir("totem", "Hola") : ba::i18n::llenar(e.traducir("totem", "Hola %1"), {pila}));
    reemplazar(s, "{nombre}", nombre);
    reemplazar(s, "{evento}", evento.empty() ? e.traducir("totem", "el evento") : evento);
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
    if (porId(t.id) != nullptr) return ba::i18n::llenar(ba::i18n::traducir("registro", "Ya hay un tótem con el id «%1»."), {t.id});
    if (const std::string e = validar(t); !e.empty()) return e;
    lista_.push_back(std::move(t));
    return {};
}

std::string Totems::cambiar(const std::string& id, const json& cambios) {
    auto it = std::find_if(lista_.begin(), lista_.end(), [&id](const Totem& t) { return t.id == id; });
    if (it == lista_.end()) return ba::i18n::llenar(ba::i18n::traducir("registro", "No hay ningún tótem «%1»."), {id});
    if (!cambios.is_object()) return ba::i18n::traducir("registro", "Los cambios tienen que ser un objeto.");
    // Se arma el resultado entero y se valida ANTES de tocar nada: un cambio a
    // medias (el nombre sí, los campos no) deja un tótem que nadie pidió.
    json j = it->aJson();
    for (auto c = cambios.begin(); c != cambios.end(); ++c) {
        if (c.key() == "id") continue; // el id no cambia: es la url y el secreto
        if (!j.contains(c.key())) return ba::i18n::llenar(ba::i18n::traducir("registro", "No conozco «%1» en un tótem."), {c.key()});
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
        if (err != nullptr) *err = ba::i18n::llenar(ba::i18n::traducir("registro", "no se pudo leer %1"), {archivo.string()});
        return {};
    }
    if (j.is_discarded()) {
        if (ok != nullptr) *ok = false;
        if (err != nullptr)
            *err = ba::i18n::llenar(ba::i18n::traducir("registro", "la configuración de registro está rota (JSON inválido): %1"),
                                    {archivo.string()});
        return {};
    }
    return Totems::deJson(j);
}

bool guardarTotems(const fs::path& carpeta, const Totems& t, std::string* err) {
    if (carpeta.empty()) {
        if (err != nullptr) *err = ba::i18n::traducir("registro", "no hay un evento abierto");
        return false;
    }
    std::error_code ec;
    fs::create_directories(carpeta, ec);
    const fs::path archivo = carpeta / kArchivo;
    const fs::path temporal = carpeta / "registro.json.tmp";
    {
        std::ofstream out(temporal, std::ios::trunc);
        if (!out) {
            if (err != nullptr) *err = ba::i18n::llenar(ba::i18n::traducir("registro", "no se pudo escribir %1"), {temporal.string()});
            return false;
        }
        out << t.aJson().dump(2, ' ', false, json::error_handler_t::replace) << '\n';
        if (!out) {
            if (err != nullptr)
                *err = ba::i18n::llenar(ba::i18n::traducir("registro", "no se pudo terminar de escribir %1"), {temporal.string()});
            return false;
        }
    }
    fs::rename(temporal, archivo, ec);
    if (ec) {
        if (err != nullptr)
            *err = ba::i18n::llenar(ba::i18n::traducir("registro", "no se pudo reemplazar la configuración de registro: %1"),
                                    {ec.message()});
        return false;
    }
    return true;
}

} // namespace ba::registro
