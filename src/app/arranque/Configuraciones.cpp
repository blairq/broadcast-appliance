// src/app/arranque/Configuraciones.cpp — ver Configuraciones.hpp

#include "arranque/Configuraciones.hpp"

#include "evento/FotosConfig.hpp"
#include "platform/Paths.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include <fstream>

namespace ba::app {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path deU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string aU8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

json leerJson(const fs::path& p) {
    std::ifstream f(p);
    json j = json::parse(f, nullptr, false);
    return j.is_object() ? j : json::object(); // sin archivo (o roto): vacío, nunca null
}

void escribirJson(const fs::path& p, const json& j) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p) << j.dump(1);
}

std::uintmax_t bytesDe(const fs::path& dir) {
    std::uintmax_t n = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) n += it->file_size(ec);
    return n;
}

/// Copia una configuración entera, menos lo que no se lleva (grabaciones, lo que
/// compartieron los invitados, lo preparado para restaurar).
std::string copiar(const fs::path& de, const fs::path& a, bool conGrabaciones) {
    std::error_code ec;
    fs::create_directories(a, ec);
    for (const auto& e : fs::directory_iterator(de, ec)) {
        const std::string n = aU8(e.path().filename());
        if (n == "configuracion.json" || n == "Shares" || n == "Logs" || (!conGrabaciones && n == "Recordings")) continue;
        std::error_code e2;
        if (e.is_directory())
            fs::copy(e.path(), a / e.path().filename(), fs::copy_options::recursive | fs::copy_options::skip_existing, e2);
        else
            fs::copy_file(e.path(), a / e.path().filename(), fs::copy_options::skip_existing, e2);
        if (e2) return "no se pudo copiar " + n + ": " + e2.message();
    }
    fs::remove_all(a / "Config" / ".restaurar", ec);
    return {};
}

/// Lo de fábrica que trae el programa (Graficos/, Extensiones/) y le falta.
void sembrar(const fs::path& a) {
    for (const char* d : {"Graficos", "Extensiones"}) {
        std::error_code ec;
        const fs::path de = ba::platform::bundleRoot() / d;
        if (fs::is_directory(de, ec))
            fs::copy(de, a / d, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    }
}
} // namespace

Configuraciones::Configuraciones(std::function<QString()> reiniciar, QObject* parent)
    : QObject(parent), reiniciar_(std::move(reiniciar)) {}

fs::path Configuraciones::datos() const { return ba::platform::dataRoot(); }
fs::path Configuraciones::carpeta(const std::string& nombre) const { return datos() / "Configuraciones" / deU8(nombre); }

std::string Configuraciones::nombreValido(const std::string& n) {
    if (n.empty() || n.size() > 60) return "el nombre tiene que tener entre 1 y 60 letras";
    if (n[0] == '.' || n.find_first_of("/\\:*?\"<>|") != std::string::npos || n.find("..") != std::string::npos)
        return "el nombre no puede tener / \\ : * ? \" < > | ni empezar con punto";
    return {};
}

std::string Configuraciones::activa() const { return ba::platform::configActiva(datos()); }

json Configuraciones::listar() const {
    json out = json::array();
    std::error_code ec;
    const std::string act = activa();
    for (const auto& e : fs::directory_iterator(datos() / "Configuraciones", ec)) {
        const std::string n = aU8(e.path().filename());
        if (!e.is_directory() || n.empty() || n[0] == '.') continue;
        const json meta = leerJson(e.path() / "configuracion.json");
        out.push_back({{"nombre", n},
                       {"activa", n == act},
                       {"descripcion", meta.value("descripcion", std::string())},
                       {"creada_ms", meta.value("creada_ms", 0LL)},
                       {"desde", meta.value("desde", std::string())},
                       {"bytes", bytesDe(e.path())}});
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a["nombre"] < b["nombre"]; });
    return out;
}

json Configuraciones::demos() const {
    json out = json::array();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(ba::platform::bundleRoot() / "Demos", ec)) {
        if (!e.is_directory()) continue;
        const json meta = leerJson(e.path() / "configuracion.json");
        out.push_back({{"nombre", aU8(e.path().filename())}, {"descripcion", meta.value("descripcion", std::string())}});
    }
    return out;
}

std::string Configuraciones::crear(const std::string& nombre, const std::string& desde, const std::string& descripcion,
                                   bool conGrabaciones) {
    if (const std::string e = nombreValido(nombre); !e.empty()) return e;
    const fs::path dst = carpeta(nombre);
    std::error_code ec;
    if (fs::exists(dst, ec)) return "ya hay una configuración «" + nombre + "»";
    std::string err;
    std::string desc = descripcion;
    if (desde.empty() || desde == "vacia") {
        fs::create_directories(dst / "Config", ec);
        if (ec) return "no se pudo crear " + aU8(dst) + ": " + ec.message();
    } else if (desde.rfind("demo:", 0) == 0) {
        const fs::path de = ba::platform::bundleRoot() / "Demos" / deU8(desde.substr(5));
        if (!fs::is_directory(de, ec)) return "no hay ninguna demo «" + desde.substr(5) + "»";
        err = copiar(de, dst, true);
        if (desc.empty()) desc = leerJson(de / "configuracion.json").value("descripcion", std::string());
    } else if (desde.rfind("evento:", 0) == 0) {
        // evento:<carpeta del evento>/<inicio|cierre|ultima>: su foto de Config/ + lo demás de la actual.
        const std::string resto = desde.substr(7);
        const auto barra = resto.rfind('/');
        if (barra == std::string::npos) return "evento:<carpeta>/<inicio|cierre|ultima>";
        const fs::path eventos = ba::platform::configRoot() / "Eventos";
        const json foto = leerJson(eventos / deU8(resto.substr(0, barra)) / "config" / (resto.substr(barra + 1) + ".json"));
        if (!foto.contains("archivos")) return "no encuentro esa foto de la configuración del evento";
        err = copiar(ba::platform::configRoot(), dst, false);
        if (err.empty()) {
            fs::remove_all(dst / "Config", ec);
            evento::FotosConfig fc(ba::platform::configRoot() / "Config", eventos / ".objetos");
            for (const auto& [rel, info] : foto["archivos"].items()) {
                const json contenido = fc.leer(info.value("sha", std::string()));
                if (contenido.is_null()) continue;
                // Lo tapado (secretos) toma el valor de la configuración de ahora.
                const json actual = leerJson(ba::platform::configRoot() / "Config" / deU8(rel));
                escribirJson(dst / "Config" / deU8(rel),
                             evento::FotosConfig::conTapadosDe(json{{"x", contenido}}, json{{"x", actual}})["x"]);
            }
        }
    } else {
        // "actual" u otra configuración por su nombre.
        const fs::path de = desde == "actual" ? ba::platform::configRoot() : carpeta(desde);
        if (!fs::is_directory(de, ec)) return "no hay ninguna configuración «" + desde + "»";
        err = copiar(de, dst, conGrabaciones);
    }
    if (!err.empty()) {
        fs::remove_all(dst, ec);
        return err;
    }
    sembrar(dst);
    escribirJson(dst / "configuracion.json", {{"descripcion", desc},
                                              {"creada_ms", QDateTime::currentMSecsSinceEpoch()},
                                              {"desde", desde.empty() ? std::string("vacia") : desde}});
    emit cambio();
    return {};
}

std::string Configuraciones::activar(const std::string& nombre) {
    std::error_code ec;
    if (!fs::is_directory(carpeta(nombre), ec)) return "no hay ninguna configuración «" + nombre + "»";
    escribirJson(datos() / "configuraciones.json", {{"activa", nombre}});
    emit cambio();
    return {};
}

std::string Configuraciones::renombrar(const std::string& de, const std::string& a) {
    if (const std::string e = nombreValido(a); !e.empty()) return e;
    if (de == activa()) return "la activa no se renombra (cambiá a otra primero)";
    std::error_code ec;
    if (!fs::is_directory(carpeta(de), ec)) return "no hay ninguna configuración «" + de + "»";
    if (fs::exists(carpeta(a), ec)) return "ya hay una configuración «" + a + "»";
    fs::rename(carpeta(de), carpeta(a), ec);
    if (ec) return "no se pudo renombrar: " + ec.message();
    emit cambio();
    return {};
}

std::string Configuraciones::describir(const std::string& nombre, const std::string& descripcion) {
    std::error_code ec;
    if (!fs::is_directory(carpeta(nombre), ec)) return "no hay ninguna configuración «" + nombre + "»";
    json meta = leerJson(carpeta(nombre) / "configuracion.json");
    meta["descripcion"] = descripcion;
    escribirJson(carpeta(nombre) / "configuracion.json", meta);
    emit cambio();
    return {};
}

std::string Configuraciones::borrar(const std::string& nombre) {
    if (nombre == activa()) return "la activa no se borra (cambiá a otra primero)";
    std::error_code ec;
    if (nombreValido(nombre) != "" || !fs::is_directory(carpeta(nombre), ec)) return "no hay ninguna configuración «" + nombre + "»";
    const fs::path pap = datos() / "Configuraciones" / ".papelera" /
                         deU8(nombre + "-" + std::to_string(QDateTime::currentSecsSinceEpoch()));
    fs::create_directories(pap.parent_path(), ec);
    fs::rename(carpeta(nombre), pap, ec);
    if (ec) return "no se pudo mover a la papelera: " + ec.message();
    emit cambio();
    return {};
}

namespace {
QVariantList aQvl(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).array().toVariantList();
}
} // namespace

QVariantList Configuraciones::listaUi() const { return aQvl(listar()); }
QVariantList Configuraciones::demosUi() const { return aQvl(demos()); }
QString Configuraciones::crearUi(const QString& n, const QString& d, const QString& desc) {
    return QString::fromStdString(crear(n.toStdString(), d.toStdString(), desc.toStdString()));
}
QString Configuraciones::cambiarUi(const QString& n) {
    if (n.toStdString() == activa()) return "ya es la activa";
    if (const std::string e = activar(n.toStdString()); !e.empty()) return QString::fromStdString(e);
    return reiniciar();
}
QString Configuraciones::renombrarUi(const QString& de, const QString& a) {
    return QString::fromStdString(renombrar(de.toStdString(), a.toStdString()));
}
QString Configuraciones::describirUi(const QString& n, const QString& d) {
    return QString::fromStdString(describir(n.toStdString(), d.toStdString()));
}
QString Configuraciones::borrarUi(const QString& n) { return QString::fromStdString(borrar(n.toStdString())); }

} // namespace ba::app
