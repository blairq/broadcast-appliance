// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/InformesResultado.hpp"
#include "i18n/I18n.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ba::app::asamblea {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("v")
        .toVariant();
}

std::string leerArchivo(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool escribirArchivo(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        o << s;
        if (!o) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

/// El id de un nombre: minúsculas, letras y números, guiones entre palabras.
std::string idDe(const std::string& nombre) {
    std::string out;
    for (unsigned char c : nombre) {
        if (c >= 0x80) continue; // (las tildes se caen: el nombre de verdad queda en el índice)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("plantilla") : out.substr(0, 40);
}

/// Un campo: "a.b.c" en el contexto (de adentro para afuera); "." es el de adentro.
const json* buscar(const std::vector<const json*>& pila, const std::string& nombre) {
    if (nombre == ".") return pila.back();
    std::vector<std::string> partes;
    std::stringstream ss(nombre);
    for (std::string p; std::getline(ss, p, '.');) partes.push_back(p);
    for (auto it = pila.rbegin(); it != pila.rend(); ++it) {
        const json* j = *it;
        if (!j->is_object() || !j->contains(partes[0])) continue;
        j = &(*j)[partes[0]];
        // (lista.0 es el primero: {{#puntos.0}}…{{/puntos.0}} sale una vez si hay alguno.)
        for (size_t k = 1; k < partes.size() && j; ++k) {
            const std::string& p = partes[k];
            if (j->is_array() && !p.empty() && std::all_of(p.begin(), p.end(), ::isdigit))
                j = std::stoul(p) < j->size() ? &(*j)[std::stoul(p)] : nullptr;
            else
                j = j->is_object() && j->contains(p) ? &(*j)[p] : nullptr;
        }
        return j;
    }
    return nullptr;
}

bool verdadero(const json* j) {
    if (!j || j->is_null()) return false;
    if (j->is_boolean()) return j->get<bool>();
    if (j->is_array() || j->is_object()) return !j->empty();
    if (j->is_string()) return !j->get<std::string>().empty();
    if (j->is_number()) return j->get<double>() != 0;
    return true;
}

std::string comoTexto(const json* j) {
    if (!j || j->is_null()) return {};
    if (j->is_string()) return j->get<std::string>();
    if (j->is_number_integer()) return std::to_string(j->get<long long>());
    if (j->is_number()) {
        std::ostringstream o;
        o << j->get<double>();
        return o.str();
    }
    if (j->is_boolean()) return j->get<bool>() ? ba::i18n::traducirEvento("asamblea", "sí") : ba::i18n::traducirEvento("asamblea", "no");
    return {};
}

std::string recortar(const std::string& s) {
    const auto a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

/// ¿El tag que va de `a` a `b` está solo en su renglón? Entonces se lleva el renglón
/// (si no, cada {{#lista}} en su línea deja un renglón vacío y rompe las tablas).
bool soloEnSuRenglon(const std::string& t, size_t a, size_t b, size_t& desde, size_t& hasta) {
    size_t i = a;
    while (i > 0 && (t[i - 1] == ' ' || t[i - 1] == '\t')) --i;
    if (i > 0 && t[i - 1] != '\n') return false;
    size_t j = b;
    while (j < t.size() && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r')) ++j;
    if (j < t.size() && t[j] != '\n') return false;
    desde = i;
    hasta = j < t.size() ? j + 1 : j;
    return true;
}

void rellenarEn(const std::string& t, std::vector<const json*>& pila, std::string& out) {
    size_t i = 0;
    while (i < t.size()) {
        const size_t a = t.find("{{", i);
        if (a == std::string::npos) {
            out += t.substr(i);
            return;
        }
        const size_t b = t.find("}}", a + 2);
        if (b == std::string::npos) {
            out += t.substr(i);
            return;
        }
        const std::string dentro = recortar(t.substr(a + 2, b - a - 2));
        const char tipo = dentro.empty() ? ' ' : dentro[0];
        if (tipo != '#' && tipo != '^') {
            out += t.substr(i, a - i);
            if (tipo != '/' && tipo != '!') out += comoTexto(buscar(pila, dentro));
            i = b + 2;
            continue;
        }
        // Una sección: hasta su {{/nombre}}, contando las del mismo nombre de adentro.
        const std::string nombre = recortar(dentro.substr(1));
        size_t abre0 = a, abre1 = b + 2;
        size_t d0 = 0, d1 = 0;
        if (soloEnSuRenglon(t, a, b + 2, d0, d1)) abre0 = d0, abre1 = d1;
        out += t.substr(i, abre0 - i);
        int nivel = 1;
        size_t k = b + 2, cierra0 = std::string::npos, cierra1 = std::string::npos;
        while (nivel > 0) {
            const size_t x = t.find("{{", k);
            if (x == std::string::npos) break;
            const size_t y = t.find("}}", x + 2);
            if (y == std::string::npos) break;
            const std::string tag = recortar(t.substr(x + 2, y - x - 2));
            if (tag.empty()) {
                k = y + 2;
                continue;
            }
            if ((tag[0] == '#' || tag[0] == '^') && recortar(tag.substr(1)) == nombre) ++nivel;
            else if (tag[0] == '/' && recortar(tag.substr(1)) == nombre && --nivel == 0) {
                cierra0 = x;
                cierra1 = y + 2;
                if (soloEnSuRenglon(t, x, y + 2, d0, d1)) cierra0 = d0, cierra1 = d1;
            }
            k = y + 2;
        }
        if (cierra0 == std::string::npos) return; // sin cerrar: se corta ahí
        const std::string cuerpo = t.substr(abre1, std::max(abre1, cierra0) - abre1);
        const json* v = buscar(pila, nombre);
        if (tipo == '^') {
            if (!verdadero(v)) rellenarEn(cuerpo, pila, out);
        } else if (v && v->is_array()) {
            for (const auto& item : *v) {
                pila.push_back(&item);
                rellenarEn(cuerpo, pila, out);
                pila.pop_back();
            }
        } else if (verdadero(v)) {
            pila.push_back(v);
            rellenarEn(cuerpo, pila, out);
            pila.pop_back();
        }
        i = cierra1;
    }
}

} // namespace

InformesResultado::InformesResultado(const fs::path& datos, std::function<json()> contenido, std::function<fs::path()> salida,
                                     APdf aPdf, QObject* parent)
    : QObject(parent),
      carpeta_(datos / "Config" / "informes"),
      contenido_(std::move(contenido)),
      salida_(std::move(salida)),
      aPdf_(std::move(aPdf)) {}

std::string InformesResultado::rellenar(const std::string& plantilla, const json& datos) {
    std::vector<const json*> pila{&datos};
    std::string out;
    rellenarEn(plantilla, pila, out);
    return out;
}

json InformesResultado::campos() {
    const auto T = [](const char* t) { return ba::i18n::traducir("asamblea", t); };
    return json::array({
        {{"campo", "{{cuerpo}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "el nombre del cuerpo"))}},
        {{"campo", "{{evento}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "el nombre del evento"))}},
        {{"campo", "{{fecha}} {{hora}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "la fecha y la hora del evento"))}},
        {{"campo", "{{generado}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "cuándo se generó el informe"))}},
        {{"campo", "{{quorum.estado}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "«Hay quórum» o «Sin quórum»"))}},
        {{"campo", "{{quorum.presentes}} {{quorum.necesarios}} {{quorum.miembros}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "los números del quórum"))}},
        {{"campo", "{{padron.presentes}} {{padron.total}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "presentes y anotados en el padrón"))}},
        {{"campo", "{{#presentes}}{{nombre}} {{papel}} {{por_donde}} {{hora}}{{/presentes}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "los que llegaron, uno por uno"))}},
        {{"campo", "{{#puntos}}{{n}} {{expediente}} {{titulo}} {{estado}} {{resultado}}{{/puntos}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "los puntos del orden del día"))}},
        {{"campo", "{{#votaciones}}{{asunto}} {{afirmativos}} {{negativos}} {{abstenciones}} {{presentes}} {{mayoria}} {{resultado}}{{/votaciones}}"},
         {"que", T(QT_TRANSLATE_NOOP("asamblea", "las votaciones oficiales"))}},
        {{"campo", "{{#palabra}}{{nombre}} {{tiempo}}{{/palabra}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "el uso de la palabra, de más a menos"))}},
        {{"campo", "{{^puntos}}…{{/puntos}}"}, {"que", T(QT_TRANSLATE_NOOP("asamblea", "lo de adentro sale solo si la lista está vacía"))}},
    });
}

json InformesResultado::indice() const {
    std::ifstream f(carpeta_ / "plantillas.json");
    const json j = json::parse(f, nullptr, false);
    return j.is_array() ? j : json::array();
}

void InformesResultado::guardarIndice(const json& i) const { escribirArchivo(carpeta_ / "plantillas.json", i.dump(1)); }

void InformesResultado::asegurarUna() {
    if (!indice().empty()) return;
    std::string err;
    // Sin aviso: esto corre adentro de leer la lista (plantillasUi, el READ de la
    // propiedad), que ya devuelve la recién creada; avisar ahí relee el enlace que
    // está leyendo y QML lo marca como ciclo («Binding loop … lista»).
    agregar(ba::i18n::traducirEvento("asamblea", "Resultado de la sesión"), {}, &err);
}

json InformesResultado::plantillas() const {
    const_cast<InformesResultado*>(this)->asegurarUna();
    return indice();
}

std::string InformesResultado::texto(const std::string& id) const {
    for (const auto& p : plantillas())
        if (p.value("id", std::string()) == id) return leerArchivo(carpeta_ / (id + ".md"));
    return {};
}

std::string InformesResultado::crear(const std::string& nombre, const std::string& texto, std::string* error) {
    const std::string id = agregar(nombre, texto, error);
    if (!id.empty()) emit cambio();
    return id;
}

std::string InformesResultado::agregar(const std::string& nombre, const std::string& texto, std::string* error) {
    const std::string n = recortar(nombre);
    if (n.empty()) {
        if (error) *error = ba::i18n::traducir("asamblea", "falta el nombre de la plantilla");
        return {};
    }
    json idx = indice();
    std::string id = idDe(n);
    for (int k = 2;; ++k) {
        const bool usado = std::any_of(idx.begin(), idx.end(), [&](const json& p) { return p.value("id", std::string()) == id; });
        if (!usado) break;
        id = idDe(n) + "-" + std::to_string(k);
    }
    std::string t = texto;
    if (t.empty()) {
        const std::string ev = ba::i18n::idiomaDelEvento();
        t = ev.empty() ? ba::i18n::leer("plantillas/informe-resultado.md")
                       : ba::i18n::En(ev).leer("plantillas/informe-resultado.md");
    }
    if (t.empty()) t = "# {{cuerpo}}\n\n{{#puntos}}- {{n}}. {{titulo}}: {{estado}} {{resultado}}\n{{/puntos}}";
    if (!escribirArchivo(carpeta_ / (id + ".md"), t)) {
        if (error) *error = ba::i18n::traducir("asamblea", "no se pudo guardar la plantilla");
        return {};
    }
    idx.push_back({{"id", id}, {"nombre", n}});
    guardarIndice(idx);
    return id;
}

std::string InformesResultado::guardar(const std::string& id, const std::string& texto, const std::string& nombre) {
    json idx = indice();
    for (auto& p : idx)
        if (p.value("id", std::string()) == id) {
            if (!escribirArchivo(carpeta_ / (id + ".md"), texto)) return ba::i18n::traducir("asamblea", "no se pudo guardar la plantilla");
            if (!recortar(nombre).empty()) {
                p["nombre"] = recortar(nombre);
                guardarIndice(idx);
            }
            emit cambio();
            return {};
        }
    return ba::i18n::llenar(ba::i18n::traducir("asamblea", "no hay una plantilla «%1»"), {id});
}

std::string InformesResultado::quitar(const std::string& id) {
    json idx = indice();
    const auto antes = idx.size();
    idx.erase(std::remove_if(idx.begin(), idx.end(), [&](const json& p) { return p.value("id", std::string()) == id; }), idx.end());
    if (idx.size() == antes) return ba::i18n::llenar(ba::i18n::traducir("asamblea", "no hay una plantilla «%1»"), {id});
    std::error_code ec;
    fs::remove(carpeta_ / (id + ".md"), ec);
    guardarIndice(idx);
    emit cambio();
    return {};
}

std::string InformesResultado::vista(const std::string& id) const {
    json d = contenido_ ? contenido_() : json::object();
    d["generado"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString();
    return rellenar(texto(id), d);
}

json InformesResultado::generar(const std::string& id) {
    const std::string t = texto(id);
    if (t.empty()) return {{"error", ba::i18n::llenar(ba::i18n::traducir("asamblea", "no hay una plantilla «%1»"), {id})}};
    const fs::path dir = salida_ ? salida_() : fs::path();
    if (dir.empty()) return {{"error", ba::i18n::traducir("asamblea", "no hay un evento abierto: el informe va en su carpeta")}};
    const std::string md = vista(id);
    const std::string base = id + "-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm").toStdString();
    const fs::path destino = dir / "informes" / (base + ".pdf");
    if (!escribirArchivo(dir / "informes" / (base + ".md"), md))
        return {{"error", ba::i18n::traducir("asamblea", "no se pudo escribir en la carpeta del evento")}};
    std::string nombre = id;
    for (const auto& p : indice())
        if (p.value("id", std::string()) == id) nombre = p.value("nombre", id);
    const QString e = aPdf_ ? aPdf_(QString::fromStdString(md), QString::fromStdString(nombre), QString::fromStdString(destino.string()))
                            : QString::fromStdString(ba::i18n::traducir("asamblea", "no hay conversor a PDF"));
    if (!e.isEmpty()) return {{"error", e.toStdString()}, {"markdown", (dir / "informes" / (base + ".md")).string()}};
    return {{"archivo", destino.string()}, {"markdown", (dir / "informes" / (base + ".md")).string()}};
}

QVariantList InformesResultado::plantillasUi() const { return aQv(plantillas()).toList(); }
QVariantList InformesResultado::camposUi() const { return aQv(campos()).toList(); }

QVariantMap InformesResultado::crearUi(const QString& nombre) {
    std::string err;
    const std::string id = crear(nombre.toStdString(), {}, &err);
    if (id.empty()) return {{"error", QString::fromStdString(err)}};
    return {{"id", QString::fromStdString(id)}};
}

QString InformesResultado::guardarUi(const QString& id, const QString& texto, const QString& nombre) {
    return QString::fromStdString(guardar(id.toStdString(), texto.toStdString(), nombre.toStdString()));
}

QVariantMap InformesResultado::generarUi(const QString& id) { return aQv(generar(id.toStdString())).toMap(); }

} // namespace ba::app::asamblea
